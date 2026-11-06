#pragma once

#include "log.h"
#include "protocol.h"
#include "serial.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#define LEN(buf) (sizeof((buf)) / sizeof((buf)[0]))
static const uint8_t handshake[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
                                    0x01, 0x00, 0x03, 0x35, 0x00, 0x39};
static const uint8_t checksensor[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
                                      0x01, 0x00, 0x03, 0x36, 0x00, 0x3A};
static const uint8_t autoidentify[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
                                       0x01, 0x00, 0x08, 0x32, 0x02, 0xFF,
                                       0xFF, 0x00, 0x05, 0x02, 0x40};
static const uint8_t getimage[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
                                   0x07, 0x00, 0x03, 0x00, 0x00, 0x0A};

static void dump(const char *name, const uint8_t *b, size_t n) {
  printf("%-14s", name);
  for (size_t i = 0; i < n; i++)
    printf("%02X ", b[i]);
  printf("\n");
  fflush(stdout);
}

/* return fd when successfully opened, -1 if not, -2 if locked */
static int open_fp(const char *devname, unsigned int baudrate, bool delay,
                   int vtime, int vmin) {
  log_info("now openning device %s...", devname);

  int flags = O_RDWR | O_NOCTTY;
  if (delay == false)
    flags |= O_NDELAY;

  int fd = open(devname, flags);
  if (fd == -1) {
    log_error("can not open device %s!", devname);
    return -1;
  }

  if (isatty(fd) == 0) {
    log_error("the device %s is not a tty, we can not set serial for it!",
              devname);
    fp_close(fd);
    return -1;
  }

  log_info("device %s opened, "
           "now configuring terminal parameters...",
           devname);

  if (lockf(fd, F_TLOCK, 0) == -1) {
    if (errno == EACCES || errno == EAGAIN) {
      log_error("file: %s (fd: %d) locked!", devname, fd);
    }
    fp_close(fd);
    return -2;
  }
  /* VMIN=0, VTIME=0: read returns whatever is available at once; the waiting
     is driven by poll() in fp_recv_pack, so its timeout is authoritative. */
  int ret;
  ret = serial_set_attr(fd, baudrate, DATABIT_8, STOPBIT_1, PARITY_NON, vtime,
                        vmin);
  if (ret != 0) {
    log_error("failed to configure the serial port!"
              "(ret: %d, errno: %s)",
              ret, strerror(errno));
    fp_close(fd);
    return -1;
  }

  return fd;
}
