/* compile it with:
 * cc ./rd_nb.c ../../src/log.c ../../src/protocol.c ../../src/serial.c \
 * -I../../inc -Wall -Wextra -g -DLOG_USE_COLOR -o wr_test
 */
#include "log.h"
#include "pub.h"
#include <string.h>

extern bool serial_is_dev_hangup(int fd);

static uint8_t fp_send_cmd(int fd, const uint8_t *data, size_t data_len) {
  size_t n = data_len;
  if (n == 0) {
    return FP_FAIL;
  }

  ssize_t bw = write(fd, data, n);
  if (bw == -1 || (size_t)bw != n) {
    log_error("write failed: %s, bw: %d, n: %d", strerror(errno), bw, n);
    return FP_IO_ERR;
  }
  log_debug("current errno: %d => %s", errno, strerror(errno));
  log_debug("write %zu bytes", bw);
  dump("write: ", data, n);

  return FP_OK;
}

static uint8_t fp_recv_pack(int fd, uint8_t *data, size_t data_len) {
  if (data_len == 0) {
    return FP_FAIL;
  }

  ssize_t br;
  br = read(fd, data, data_len);
  log_debug("current errno: %d => %s", errno, strerror(errno));
  if (br <= 0) {
    log_error("read failed: %s", strerror(errno));
    if (errno == ENOENT) {
      return FP_TIMEOUT;
    }

    if (serial_is_dev_hangup(fd)) {
    // if (errno == EIO || errno == ENXIO) {
      return FP_IO_ERR;
    }
    return FP_FAIL;
  }

  dump("read: ", data, br);
  return FP_OK;
}

#define SEND_CMD(fd, buf) fp_send_cmd((fd), (buf), LEN(buf))

int main() {
  /* O_NDELAY (O_NONBLOCK) 需要搭配 vtime & vmin 一起用
   * 且如果不设置阻塞，则 vtime != 0 || vmin != 0 的情况是非法的
   * 此时会导致读越界并 segfault
   *
   * 对于 autoidentify 这样需要等指纹的， vtime 太小的情况下
   * 会不等到读数据（即不等验证）就直接返回
   */

  bool delay = true;
  bool use_vtime = true;
  bool vtime_small = false;
  bool use_vmin = false;
  bool vmin_small = false;

  int fd = open_fp("/dev/FP_ZW111", B57600, delay,
                   (!delay)        ? 0
                   : (!use_vtime)  ? 0
                   : (vtime_small) ? 1
                                   : 100,
                   (!delay)       ? 0
                   : (!use_vmin)  ? 0
                   : (vmin_small) ? 1
                                  : 100);

  uint8_t data[64] = {0};

  SEND_CMD(fd, checksensor);
  fp_recv_pack(fd, data, LEN(data));

  SEND_CMD(fd, handshake);
  fp_recv_pack(fd, data, LEN(data));

  SEND_CMD(fd, autoidentify);
  // 如果设置 vtime_small 为真则这里会立马返回，不等指纹验证
  fp_recv_pack(fd, data, LEN(data));

  fp_close(fd);
  return 0;
}
