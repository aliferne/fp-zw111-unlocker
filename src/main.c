#include "log.h"

#include "serial.h"
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define LEN(x) (sizeof((x)) / sizeof((x)[0]))

#define FINGER_MOD "/dev/ttyACM0"
#define DEFAULT_BAUDRATE (B57600)
#define DEFAULT_DATABITS (CS8)
// CSTOPB 表示 2bit 的停止位，对其取反以表示 1bit / 1.5bit
// 协议里面为 1bit，因此直接让 c_cflags 与之做与运算即可
#define DEFAULT_STOPBITS (~CSTOPB)
// 协议里面不需要校验，因此直接让 c_cflags 与之做与运算即可
#define DEFAULT_PARITY (~PARENB)

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
  // [22:13:53.805] EF 01 FF FF FF FF 01 00 03 35 00 39
  // [22:13:53.823] EF 01 FF FF FF FF 07 00 03 00 00 0A

  char send[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
                 0x01, 0x00, 0x03, 0x35, 0x00, 0x39};
  uint8_t buf[12] = {0};
  int bw = write(fd, send, 12);
  if (bw < 0) {
    log_error("write negative bytes!\n");
    return -1;
  }
  log_info("write %d bytes\n", bw);

  int br = read(fd, buf, LEN(buf));
  if (br < 0) {
    log_error("read negative bytes!\n");
    return -1;
  } else if (br == 0) {
    log_warn("read timeout!\n");
    return -1;
  }

  log_info("read %d bytes\n", br);

  for (unsigned int i = 0; i < LEN(buf); i++) {
    printf("%02X ", buf[i]);
  }
  printf("\n");

  close(fd);

  return 0;
}
