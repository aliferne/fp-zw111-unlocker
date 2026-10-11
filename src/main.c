#include "log.h"

#include "protocol.h"
#include "serial.h"
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define LEN(x) (sizeof((x)) / sizeof((x)[0]))

#define FINGER_MOD "/dev/ttyACM1"
#define DEFAULT_BAUDRATE (B57600)

static void dump(const char *name, const uint8_t *b, size_t n) {
  log_info("%s", name);
  for (size_t i = 0; i < n; i++)
    printf("%02X ", b[i]);
  printf("\n");
  fflush(stdout);
}

void fp_read_sys_para(int fd) {
  struct fp_pack p_send = {0};
  struct fp_pack p_recv = {0};
  p_recv.pack_len = 0x13;
  fp_pack_init(&p_send, FP_DEFAULT_ADDR, FP_CMD_PACK, 3, 0x0F, NULL, 0x0013);

  uint8_t buf_send[TOTAL_PACK_SIZE(p_send.pack_len)];
  uint8_t buf[TOTAL_PACK_SIZE(p_recv.pack_len)];
  memset(buf, 0xAA, LEN(buf));

  parse_fp_pack(&p_send, buf_send, LEN(buf_send));
  dump("buf send:", buf_send, LEN(buf_send));
  write(fd, buf_send, LEN(buf_send));

  read(fd, buf, LEN(buf));
  dump("ReadSysParam:", buf, LEN(buf));

  build_fp_pack(&p_recv, buf, LEN(buf));
  log_info("p_recv->code: %u", p_recv.code);
}

int main() {
  log_info("now openning device %s...\n", FINGER_MOD);
  int fd = open(FINGER_MOD, O_RDWR | O_NOCTTY | O_NDELAY);
  if (fd == -1) {
    log_error("can not open device %s!\n", FINGER_MOD);
    return -1;
  }
  log_info("device %s opened\n"
           "now configuring terminal parameters...\n",
           FINGER_MOD);

  fcntl(fd, F_SETFL, 0);
  if (serial_set_attr(fd, DEFAULT_BAUDRATE, DATABIT_8, STOPBIT_1, PARITY_NON,
                      150, 225) != 0) {
    return -1;
  }
  fp_read_sys_para(fd);

#if 1
  //                1  2  3  4  5  6  7  8  9  10 11 12
  // [22:13:53.805] EF 01 FF FF FF FF 01 00 03 35 00 39
  // [22:13:53.823] EF 01 FF FF FF FF 07 00 03 00 00 0A

  char send[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
                 0x01, 0x00, 0x03, 0x35, 0x00, 0x39};
  uint8_t buf[12] = {0};
  uint8_t recv_buf[12] = {0};

  struct fp_pack p_recv = {0};
  fp_pack_init(&p_recv, FP_DEFAULT_ADDR, FP_CMD_PACK, 3, 0x35, NULL, 0x0039);

  int bw = write(fd, send, 12);
  if (bw < 0) {
    log_error("write negative bytes!\n");
    return -1;
  }
  log_info("write %d bytes", bw);

  int br = read(fd, buf, LEN(buf));
  if (br < 0) {
    log_error("read negative bytes!\n");
    return -1;
  } else if (br == 0) {
    log_warn("read timeout!\n");
    return -1;
  }

  log_info("read %d bytes", br);
  dump("Read: ", buf, LEN(buf));
  build_fp_pack(&p_recv, buf, LEN(buf));

#endif

  close(fd);

  return 0;
}
