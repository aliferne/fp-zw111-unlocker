#include "protocol.h"
#include "log.h"
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#define LEN(buf) (sizeof((buf)) / sizeof((buf)[0]))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLAMP(x, min, max) MIN((max), MAX((x), (min)))

/* max time to wait between two chunks of the same frame */
#define RECV_TIMEOUT_SEC 1
#define RECV_BUF_SIZE 64
#define SEND_BUF_SIZE (FP_GET_PACK_SIZE(0xFF))
#define DEFAULT_TIMEOUT (1.5f)

#ifdef PROTOCOL_DEBUG_ON
#define LOG_DEBUG(...) log_debug(__VA_ARGS__)

static void dump(const char *name, const uint8_t *b, size_t n) {
  printf("%-14s", name);
  for (size_t i = 0; i < n; i++)
    printf("%02X ", b[i]);
  printf("\n");
  fflush(stdout);
}
#else
#define LOG_DEBUG(...)
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

  LOG_DEBUG("fill pack: id=%u code=%02X data_len=%zu total_len=%zu "
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
static int fp_send_cmd(int fd, uint8_t code, const uint8_t *data,
                       size_t data_len) {
  uint8_t buf[SEND_BUF_SIZE];
  size_t n = fp_fill_pack(buf, FP_CMD_PACK, code, data, data_len);
  if (n == 0) {
    log_error("failed to build command pack (code=%02X)!", code);
    return -1;
  }

  dump("send: ", buf, n);
  ssize_t bw = write(fd, buf, n);
  if (bw < 0) {
    log_error("write negative bytes!");
    return -1;
  }
  log_info("write %d bytes", bw);
  return bw;
}

static int fp_recv_pack(int fd, uint8_t *buf, size_t len, float timeout) {
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
      if (errno == EINTR) // can retry
        continue;
      log_error("read poll request failed!");
      return -1;
    }
    if (ready == 0) {
      log_warn("recv timeout, got %zu/%zu bytes", got, want);
      return -1;
    }

    ssize_t br = read(fd, buf + got, want - got);
    if (br <= 0) {
      log_error("read failed!");
      return -1;
    }
    got += (size_t)br;

    if (want == FP_PACKHEADER_SIZE) {
      uint16_t pack_len =
          ((uint16_t)buf[FP_OFF_PACKLEN] << 8) | buf[FP_OFF_PACKLEN + 1];
      want = FP_PACKHEADER_SIZE + pack_len;
      if (want > len) {
        log_error("frame too large: %zu > %zu", want, len);
        return -1;
      }
    }
  }
  dump("recv: ", buf, got);
  return (int)got;
}

/* public api to build fp_cmd, returns numbers of bytes read when success
    -1 when failed, it will set status_code of the slave */
static int fp_transact(int fd, uint8_t code, const uint8_t *send,
                       size_t send_len, uint8_t *recv, size_t recv_len,
                       float timeout, uint8_t *dev_status) {
  if (fp_send_cmd(fd, code, send, send_len) < 0) {
    log_error("Send Failed");
    return -1;
  }

  int nbr = fp_recv_pack(fd, recv, recv_len, timeout);
  if (nbr == -1) {
    log_error("Recv Failed");
    return -1;
  }

  *dev_status = fp_get_status_code(recv);
  LOG_DEBUG("nbr: %d, p_recv->code: 0x%02x", nbr, *dev_status);
  return (int)nbr;
}

uint8_t fp_read_sys_para(int fd) {
  uint8_t buf[RECV_BUF_SIZE] = {0};
  uint8_t status_code = 0;
  int ret = fp_transact(fd, 0x0F, NULL, 0, buf, RECV_BUF_SIZE, DEFAULT_TIMEOUT,
                        &status_code);

  if (ret < 0) {
    log_error("check sensor failed(transact ret: %d)", ret);
  }
  return status_code;
}

uint8_t fp_check_sensor_state(int fd) {
  uint8_t buf[RECV_BUF_SIZE] = {0};
  uint8_t status_code = 0;
  int ret = fp_transact(fd, 0x35, NULL, 0, buf, RECV_BUF_SIZE, DEFAULT_TIMEOUT,
                        &status_code);

  if (ret < 0) {
    log_error("check sensor failed(transact ret: %d)", ret);
  }
  return status_code;
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
    return 0xFF;
  }
  log_info("----------------------------------------------------");
  /* bit1, 6~15 is reserved */
  return_status = false; // never open it
  uint16_t param =
      (((!require_remove) << 5) | ((!allow_duplicate) << 4) |
       (allow_overwrite << 3) | ((!return_status) << 2) | (led_ctrl << 0));

  /* should send id(2 bytes), enroll times(1 byte) and param(2 bytes) */
  uint8_t send[] = {id >> 8, id & 0xFF, enroll_times, param >> 8, param & 0xFF};
  uint8_t buf[RECV_BUF_SIZE] = {0};
  uint8_t status_code = 0;
  int ret = 0;

  ret = fp_transact(fd, 0x31, send, LEN(send), buf, RECV_BUF_SIZE, 15,
                    &status_code);
  if (ret < 0) {
    log_error("auto enroll failed(transact ret: %d)", ret);
    goto end;
  }

  if (status_code == 0x00) {
    log_info("enroll success!");
  } else if (status_code == 0x0B) {
    log_error("id is invalid!");
  } else if (status_code == 0x25) {
    log_error("enroll time is invalid!");
  } else if (status_code == 0x1F) {
    log_error("the storage is full! use `allow_overwrite` to overwrite the "
              "finger storage or delete some fingers!");
  } else if (status_code == 0x22) {
    log_error("the id: %u already exists!", id);
  } else if (status_code == 0x0A) {
    log_error("can not merge template!");
  } else if (status_code == 0x27) {
    log_error("this finger is already enrolled!");
  } else if (status_code == 0x01) {
    log_error("enroll failed for unknown reason!");
  }

end:
  log_info("----------------------------------------------------");
  return status_code;
}

uint8_t fp_auto_identify(int fd, uint8_t score_level, uint16_t id,
                         bool led_ctrl, bool return_status) {
  log_info("====================================================");
  /* bit1, 6~15 is reserved */
  return_status = false; // never open it
  uint16_t param = ((!return_status) << 2 | led_ctrl << 0);
  
  score_level = CLAMP(score_level, 1, 5);
  /* should send id(2 bytes), enroll times(1 byte) and param(2 bytes) */
  uint8_t send[] = {score_level, id >> 8, id & 0xFF, param >> 8, param & 0xFF};
  uint8_t buf[RECV_BUF_SIZE] = {0};
  uint8_t status_code = 0;
  int ret = 0;

  ret = fp_transact(fd, 0x32, send, LEN(send), buf, RECV_BUF_SIZE, 15,
                    &status_code);
  if (ret < 0) {
    log_error("auto identify failed(transact ret: %d)", ret);
    goto end;
  }

  if (status_code == 0x00) {
    log_info("identified!");
  } else if (status_code == 0x24) {
    log_error("no finger stored!");
  } else if (status_code == 0x0B) {
    log_error("id is invalid!");
  } else if (status_code == 0x23) {
    log_error("template doesn't exist!");
  } else if (status_code == 0x26) {
    log_error("identify timeout!");
  } else if (status_code == 0x09) {
    log_error("can not generate finger features or identify finger!");
  }

end:
  log_info("====================================================");
  return status_code;
}

/* control light band of the module */
uint8_t fp_control_light(int fd, enum fp_light_mode mode, uint8_t start_color,
                         uint8_t end_color, uint8_t cycles, uint8_t period) {
  /* 功能码, 起始颜色, 结束颜色/占空比, 循环次数, 时间 */
  uint8_t send[] = {(uint8_t)mode, start_color, end_color, cycles, period};
  uint8_t buf[RECV_BUF_SIZE] = {0};
  uint8_t status_code = 0;
  int ret = fp_transact(fd, 0x3C, send, LEN(send), buf, RECV_BUF_SIZE,
                        DEFAULT_TIMEOUT, &status_code);

  if (ret < 0) {
    log_error("control light failed(transact ret: %d)", ret);
    return 0xFF;
  }
  if (status_code != 0x00) {
    log_error("control light failed(status code: 0x%02X)!", status_code);
  }
  return status_code;
}

uint8_t fp_cancel(int fd) {
  uint8_t buf[RECV_BUF_SIZE] = {0};
  uint8_t status_code = 0;
  int ret = fp_transact(fd, 0x30, NULL, 0, buf, RECV_BUF_SIZE, DEFAULT_TIMEOUT,
                        &status_code);

  if (ret < 0) {
    log_error("config light failed(transact ret: %d)", ret);
  }
  return status_code;
}

uint8_t fp_sleep(int fd) {
  uint8_t buf[RECV_BUF_SIZE] = {0};
  uint8_t status_code = 0;
  int ret = fp_transact(fd, 0x33, NULL, 0, buf, RECV_BUF_SIZE, DEFAULT_TIMEOUT,
                        &status_code);

  if (ret < 0) {
    log_error("check sensor failed(transact ret: %d)", ret);
  }
  return status_code;
}

uint8_t fp_delete_char(int fd, uint16_t id, uint16_t n) {
  uint8_t buf[RECV_BUF_SIZE] = {0};
  uint8_t status_code = 0;
  uint8_t send[] = {id >> 8, id & 0xFF, n >> 8, n & 0xFF};
  int ret = fp_transact(fd, 0x0C, send, LEN(send), buf, RECV_BUF_SIZE,
                        DEFAULT_TIMEOUT, &status_code);

  if (ret < 0) {
    log_error("delete char/template failed(transact ret: %d)", ret);
  }
  return status_code;
}

uint8_t fp_empty(int fd) {
  uint8_t buf[RECV_BUF_SIZE] = {0};
  uint8_t status_code = 0;
  int ret = fp_transact(fd, 0x0D, NULL, 0, buf, RECV_BUF_SIZE, DEFAULT_TIMEOUT,
                        &status_code);

  if (ret < 0) {
    log_error("empty char/template failed(transact ret: %d)", ret);
  }
  return status_code;
}
