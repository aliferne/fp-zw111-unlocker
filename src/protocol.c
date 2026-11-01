#include "protocol.h"
#include "serial.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

#define FP_PACK_START (0xEF01)
#define FP_DEV_ADDR (0xFFFFFFFF)
#define FP_HEAD_SIZE (2U)
#define FP_DEVADDR_SIZE (4U)
#define FP_PACKID_SIZE (1U)
#define FP_PACKLEN_SIZE (2U)
#define FP_CHECKSUM_SIZE (2U)
/* the total size of these: header, dev_addr, pack_id, pack_len */
#define FP_PACKHEADER_SIZE                                                     \
  (FP_HEAD_SIZE + FP_DEVADDR_SIZE + FP_PACKID_SIZE + FP_PACKLEN_SIZE)

/* field offsets inside a frame */
#define FP_OFF_HEAD (0U)
#define FP_OFF_DEVADDR (FP_OFF_HEAD + FP_HEAD_SIZE)
#define FP_OFF_PACKID (FP_OFF_DEVADDR + FP_DEVADDR_SIZE)
#define FP_OFF_PACKLEN (FP_OFF_PACKID + FP_PACKID_SIZE)
#define FP_OFF_CODE (FP_PACKHEADER_SIZE)
#define FP_OFF_DATA (FP_OFF_CODE + 1U)

/* checksum covers bytes [FP_OFF_CHECKSUM_START, total_len - FP_CHECKSUM_SIZE)
 */
#define FP_OFF_CHECKSUM_START (FP_OFF_PACKID)

/* max time to wait between two chunks of the same frame */
#define RECV_TIMEOUT_SEC 1
#define RECV_BUF_SIZE 64
#define SEND_BUF_SIZE (FP_GET_PACK_SIZE(0xFF))
#define DEFAULT_TIMEOUT (1.5f)

/* the buffer length of fp_pack to send (pack_len already includes checksum) */
#define FP_GET_PACK_SIZE(pack_len) (FP_PACKHEADER_SIZE + (pack_len))

/* a command/slave pack carries a code byte right after the pack header */
#define IS_CODE_VALID(pack_id) ((pack_id) == 1U || (pack_id) == 7U)

#define LEN(buf) (sizeof((buf)) / sizeof((buf)[0]))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLAMP(x, min, max) MIN((max), MAX((x), (min)))

#ifdef PROTOCOL_USE_LOG
#include "log.h"
static void dump(const char *name, const uint8_t *b, size_t n) {
  printf("%-14s", name);
  for (size_t i = 0; i < n; i++)
    printf("%02X ", b[i]);
  printf("\n");
  fflush(stdout);
}

#else
#define log_info(...)
#define log_debug(...)
#define log_warn(...)
#define log_error(...)
static void dump(const char *name, const uint8_t *b, size_t n) {
  (void)name;
  (void)b;
  (void)n;
}
#endif

uint16_t fp_get_checksum(const uint8_t *buf, size_t len) {
  if (buf == NULL || len < FP_PACKHEADER_SIZE + FP_CHECKSUM_SIZE) {
    return 0;
  }

  size_t end = len - FP_CHECKSUM_SIZE;
  uint16_t sum = 0;
  for (size_t i = FP_OFF_CHECKSUM_START; i < end; i++) {
    sum += buf[i];
  }
  return sum;
}

/*
 * Fill `buf` with a whole frame and return its size in bytes (0 on error).
 * The key parameters are `pack_id`, `code` (only used by command/slave packs)
 * and the `data`/`data_len` payload. Header, device address, pack length and
 * checksum are derived automatically. `buf` must be at least
 * FP_GET_PACK_SIZE(data_len + FP_CHECKSUM_SIZE [+ 1 when code is valid]).
 */
static size_t fp_fill_pack(uint8_t *buf, uint8_t pack_id, uint8_t code,
                           const uint8_t *data, size_t data_len) {
  if (buf == NULL || (data == NULL && data_len > 0)) {
    return 0;
  }

  bool has_code = IS_CODE_VALID(pack_id);
  size_t pack_len = data_len + FP_CHECKSUM_SIZE + (has_code ? 1U : 0U);
  size_t total_len = FP_GET_PACK_SIZE(pack_len);

  buf[FP_OFF_HEAD] = (FP_PACK_START >> 8) & 0xFF;
  buf[FP_OFF_HEAD + 1] = FP_PACK_START & 0xFF;
  buf[FP_OFF_DEVADDR] = (FP_DEV_ADDR >> 24) & 0xFF;
  buf[FP_OFF_DEVADDR + 1] = (FP_DEV_ADDR >> 16) & 0xFF;
  buf[FP_OFF_DEVADDR + 2] = (FP_DEV_ADDR >> 8) & 0xFF;
  buf[FP_OFF_DEVADDR + 3] = FP_DEV_ADDR & 0xFF;
  buf[FP_OFF_PACKID] = pack_id;
  buf[FP_OFF_PACKLEN] = (pack_len >> 8) & 0xFF;
  buf[FP_OFF_PACKLEN + 1] = pack_len & 0xFF;

  size_t data_off = FP_OFF_CODE;
  if (has_code) {
    buf[FP_OFF_CODE] = code;
    data_off = FP_OFF_DATA;
  }
  if (data_len > 0) {
    memcpy(buf + data_off, data, data_len);
  }

  uint16_t checksum = fp_get_checksum(buf, total_len);
  buf[total_len - FP_CHECKSUM_SIZE] = (checksum >> 8) & 0xFF;
  buf[total_len - 1] = checksum & 0xFF;

  log_debug("fill pack: id=%u code=%02X data_len=%zu total_len=%zu "
            "checksum=%04X",
            pack_id, code, data_len, total_len, checksum);
  return total_len;
}

uint8_t fp_get_status_code(const uint8_t *recv_buf) {
  if (recv_buf == NULL) {
    return 0;
  }
  return recv_buf[FP_OFF_CODE];
}

/* build a command pack from `code` + `data` and write it to the device */
static uint8_t fp_send_cmd(int fd, uint8_t code, const uint8_t *data,
                           size_t data_len) {
  uint8_t buf[SEND_BUF_SIZE];
  size_t n = fp_fill_pack(buf, 1U, code, data, data_len);
  if (n == 0) {
    log_error("failed to build command pack (code=%02X)!", code);
    return FP_FAIL;
  }

  dump("send: ", buf, n);
  size_t off = 0;
  while (off < n) {
    ssize_t bw = write(fd, buf + off, n - off);
    if (bw < 0) {
      if (errno == EINTR)
        continue;
      log_error("write failed: %s", strerror(errno));
      return FP_IO_ERR;
    }
    if (bw == 0) {
      log_error("write returned 0");
      return FP_IO_ERR;
    }
    off += (size_t)bw;
  }
  log_debug("write %zu bytes", n);
  return FP_OK;
}

/* nbr: number of bytes read */
static uint8_t fp_recv_pack(int fd, uint8_t *buf, size_t len, int *nbr,
                            float timeout) {
  size_t got = 0;
  size_t want = FP_PACKHEADER_SIZE;
  struct pollfd pfd = {.fd = fd, .events = POLLIN | POLLPRI};

  /* a frame is [9-byte header][pack_len bytes], so read the header first,
     learn the total length from pack_len, then read the rest
     !!! the condition of while is important, basically we just need `read`
     to recv so many bytes, if we wait for whole frame, then the timeout is
     useless
     the key point to write blog, how to parse a frame */
  while (got < want) {
    int ready = poll(&pfd, 1, timeout * 1000);
    if (ready < 0) {
      if (errno == EINTR)
        continue;
      log_error("poll failed: %s", strerror(errno));
      return FP_IO_ERR;
    }
    if (ready == 0) {
      log_warn("recv timeout, got %zu/%zu bytes", got, want);
      return FP_TIMEOUT;
    }
    /* POLLHUP/POLLERR shows up when the tty is torn down (device unplugged) */
    if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
      log_error("device hangup/error (revents=0x%x)", (unsigned)pfd.revents);
      return FP_TIMEOUT;
    }

    ssize_t br = read(fd, buf + got, want - got);
    if (br < 0) {
      if (errno == EINTR)
        continue;
      log_error("read failed: %s", strerror(errno));
      return FP_IO_ERR;
    }
    if (br == 0) {
      log_error("read EOF (device gone?)");
      return FP_IO_ERR;
    }
    got += (size_t)br;

    /* only trust the length field once the whole header has arrived */
    if (want == FP_PACKHEADER_SIZE && got >= FP_PACKHEADER_SIZE) {
      uint16_t pack_len =
          ((uint16_t)buf[FP_OFF_PACKLEN] << 8) | buf[FP_OFF_PACKLEN + 1];
      want = FP_PACKHEADER_SIZE + pack_len;
      if (want > len) { // TODO: 按道理来说这种的可能性还挺小的
        log_error("frame too large: %zu > %zu", want, len);
        return FP_FAIL;
      }
    }
  }
  *nbr = got;
  dump("recv: ", buf, got);

  return FP_OK;
}

/* run one command/response transaction. On success sets *dev_status to the
 * device confirm code and returns FP_OK; on local failure returns a negative
 * uint8_t. */
static uint8_t fp_transact(int fd, uint8_t code, const uint8_t *send,
                           size_t send_len, uint8_t *recv, size_t recv_len,
                           float timeout, const char *fail_msg) {
  int nbr = 0;
  uint8_t st;

  if ((st = fp_send_cmd(fd, code, send, send_len)) != FP_OK ||
      (st = fp_recv_pack(fd, recv, recv_len, &nbr, timeout)) != FP_OK) {
    log_error("send or recv failed(err: %s)", fp_strcode(st));
    goto out;
  }

  st = fp_get_status_code(recv);
  log_debug("nbr: %d, p_recv->code: 0x%02x", nbr, st);
  return st;

out:
  if (FP_IS_STATUS(st, FP_FAIL))
    log_error("%s (errcode: %d, errstr: %s)", fail_msg, st, fp_strcode(st));
  return st;
}

/* return fd when successfully opened, -1 if not */
int fp_open(const char *devname, unsigned int baudrate) {
  log_info("now openning device %s...", devname);

  int fd = open(devname, O_RDWR | O_NOCTTY | O_NDELAY);
  if (fd == -1) {
    log_error("can not open device %s!", devname);
    return -1;
  }

  if (isatty(fd) == 0) {
    log_error("the device %s is not a tty, we can not set serial for it!",
              devname);
    return -1;
  }

  log_info("device %s opened, "
           "now configuring terminal parameters...",
           devname);

  if (lockf(fd, F_TLOCK, 0) == -1) {
    if (errno == EACCES || errno == EAGAIN) {
      log_error("file: %s (fd: %d) locked!", devname, fd);
    }
    return -1;
  }
  /* VMIN=0, VTIME=0: read returns whatever is available at once; the waiting
     is driven by poll() in fp_recv_pack, so its timeout is authoritative. */
  int ret =
      serial_set_attr(fd, baudrate, DATABIT_8, STOPBIT_1, PARITY_NON, 0, 0);
  if (ret != 0) {
    log_error("failed to configure the serial port!"
              "(ret: %d, errno: %s)",
              ret, strerror(errno));
    fp_close(fd);
    return -1;
  }
  return fd;
}

int fp_close(int fd) {
  if ((fd != -1) || (lockf(fd, F_ULOCK, 0) != -1) || (close(fd) != -1))
    return 0;

  log_error("unlock file or close file failed(fd: %d, err: %s)", fd,
            strerror(errno));
  return -1;
}

uint8_t fp_get_image(int fd) {
  uint8_t buf[RECV_BUF_SIZE] = {0};

  return fp_transact(fd, 0x01, NULL, 0, buf, RECV_BUF_SIZE, DEFAULT_TIMEOUT,
                     "get image failed");
}

uint8_t fp_read_sys_para(int fd) {
  uint8_t buf[RECV_BUF_SIZE] = {0};

  return fp_transact(fd, 0x0F, NULL, 0, buf, RECV_BUF_SIZE, DEFAULT_TIMEOUT,
                     "read sys para failed");
}

uint8_t fp_check_module_state(int fd) {
  uint8_t buf[RECV_BUF_SIZE] = {0};

  return fp_transact(fd, 0x35, NULL, 0, buf, RECV_BUF_SIZE, DEFAULT_TIMEOUT,
                     "check module failed");
}

uint8_t fp_check_sensor_state(int fd) {
  uint8_t buf[RECV_BUF_SIZE] = {0};

  return fp_transact(fd, 0x36, NULL, 0, buf, RECV_BUF_SIZE, DEFAULT_TIMEOUT,
                     "check sensor failed");
}

uint8_t fp_reset_settings(int fd) {
  uint8_t buf[RECV_BUF_SIZE] = {0};

  return fp_transact(fd, 0x38, NULL, 0, buf, RECV_BUF_SIZE, DEFAULT_TIMEOUT,
                     "reset settings failed");
}

uint8_t fp_auto_enroll(int fd, uint16_t id, uint8_t enroll_times, bool led_ctrl,
                       bool return_status, bool allow_overwrite,
                       bool allow_duplicate, bool require_remove) {
  // TODO: max id limit if (id >)
  /* theoretically enrolling time can be larger than 5(max to 255),
     but better not */
  if (enroll_times <= 1 || enroll_times > 5) {
    log_error("enroll time not in range (1, 5] (current time: %u)",
              enroll_times);
    return FP_FAIL;
  }

  /* bit1, 6~15 is reserved */
  return_status = false; // never open it
  uint16_t param =
      (((!require_remove) << 5) | ((!allow_duplicate) << 4) |
       (allow_overwrite << 3) | ((!return_status) << 2) | (led_ctrl << 0));

  /* should send id(2 bytes), enroll times(1 byte) and param(2 bytes) */
  uint8_t send[] = {id >> 8, id & 0xFF, enroll_times, param >> 8, param & 0xFF};
  uint8_t buf[RECV_BUF_SIZE] = {0};

  return fp_transact(fd, 0x31, send, LEN(send), buf, RECV_BUF_SIZE, 15,
                     "enroll failed");
}

uint8_t fp_auto_identify(int fd, uint8_t score_level, uint16_t id,
                         bool led_ctrl, bool return_status) {
  /* bit1, 6~15 is reserved */
  return_status = false; // never open it
  uint16_t param = ((!return_status) << 2 | led_ctrl << 0);

  score_level = CLAMP(score_level, 1, 5);
  /* should send id(2 bytes), enroll times(1 byte) and param(2 bytes) */
  uint8_t send[] = {score_level, id >> 8, id & 0xFF, param >> 8, param & 0xFF};
  uint8_t buf[RECV_BUF_SIZE] = {0};

  /* 模块的 timeout 大概在 11s 左右 */
  return fp_transact(fd, 0x32, send, LEN(send), buf, RECV_BUF_SIZE, 15,
                     "auto identify failed");
}

/* control light band of the module */
uint8_t fp_control_light(int fd, enum fp_light_mode mode, uint8_t start_color,
                         uint8_t end_color, uint8_t cycles, uint8_t period) {
  /* 功能码, 起始颜色, 结束颜色/占空比, 循环次数, 时间 */
  uint8_t send[] = {(uint8_t)mode, start_color, end_color, cycles, period};
  uint8_t buf[RECV_BUF_SIZE] = {0};

  return fp_transact(fd, 0x3C, send, LEN(send), buf, RECV_BUF_SIZE,
                     DEFAULT_TIMEOUT, "control light failed");
}

uint8_t fp_cancel(int fd) {
  uint8_t buf[RECV_BUF_SIZE] = {0};

  return fp_transact(fd, 0x30, NULL, 0, buf, RECV_BUF_SIZE, DEFAULT_TIMEOUT,
                     "cancel failed");
}

uint8_t fp_sleep(int fd) {
  uint8_t buf[RECV_BUF_SIZE] = {0};

  return fp_transact(fd, 0x33, NULL, 0, buf, RECV_BUF_SIZE, DEFAULT_TIMEOUT,
                     "sleep failed");
}

uint8_t fp_delete_char(int fd, uint16_t id, uint16_t n) {
  uint8_t buf[RECV_BUF_SIZE] = {0};
  uint8_t send[] = {id >> 8, id & 0xFF, n >> 8, n & 0xFF};

  return fp_transact(fd, 0x0C, send, LEN(send), buf, RECV_BUF_SIZE,
                     DEFAULT_TIMEOUT, "delete char/template failed");
}

uint8_t fp_empty(int fd) {
  uint8_t buf[RECV_BUF_SIZE] = {0};

  return fp_transact(fd, 0x0D, NULL, 0, buf, RECV_BUF_SIZE, DEFAULT_TIMEOUT,
                     "empty char/template failed");
}

const char *fp_strcode(uint8_t s) {
/* some errcodes I do not want to expose */
#define FP_ENROLL_TIME_INVALID 0x25
#define FP_MERGE_TEMPLATE_FAILED 0x0A

  switch (s) {
  case FP_OK:
    return "Success";
  case FP_NO_FINGER:
    return "Finger not on the sensor";
  case FP_FINGER_NOT_MATCHED:
    return "Finger not matched";
  case FP_ID_OUT_OF_STORAGE:
    return "ID out of storage";
  case FP_STORAGE_FULL:
    return "Finger storage is full, use `allow_overwrite` to overwrite the "
           "finger storage or delete some fingers to enroll new one";
  case FP_ID_ALREADY_EXISTS:
    return "ID already exists";
  case FP_TIMEOUT:
    return "Device/Syscall Timeout";
  case FP_FINGER_ALREADY_ENROLLED:
    return "Finger already enrolled";
  case FP_ENROLL_TIME_INVALID:
    return "Enroll time invalid";
  case FP_MERGE_TEMPLATE_FAILED:
    return "Merge template failed";

    /* customized errcodes */
  case FP_IO_ERR:
    return "IO Error, perhaps device is popped";
    /* end customized errcodes */
  case FP_FAIL:
  default:
    return "Failed";
  }
}
