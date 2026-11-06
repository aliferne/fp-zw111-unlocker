/* compile it with:
 * cc ./rd_loop.c ../../src/log.c ../../src/protocol.c ../../src/serial.c \
 * -I../../inc -Wall -Wextra -g -DLOG_USE_COLOR -o wr_test
 */
#include "log.h"
#include "pub.h"
#include <complex.h>
#include <unistd.h>

static uint8_t fp_send_cmd(int fd, const uint8_t *data, size_t data_len) {
  size_t n = data_len;
  if (n == 0) {
    return FP_FAIL;
  }

  ssize_t bw = write(fd, data, n);
  if (bw == -1) {
    log_error("write failed: %s", strerror(errno));
    return FP_IO_ERR;
  } else if ((size_t)bw != n) {
    log_error("write failed: %s, bw: %d, n: %d", strerror(errno), bw, n);
    return FP_IO_ERR;
  }
  log_debug("write %zu bytes", bw);
  dump("write: ", data, n);

  return FP_OK;
}

#include <sys/poll.h>

static uint8_t fp_recv_pack(int fd, uint8_t *data, size_t data_len) {
  size_t n = data_len;
  if (n == 0) {
    return FP_FAIL;
  }  
  
  struct pollfd pfd = {.fd = fd, .events = POLLIN | POLLPRI};

  size_t got = 0;
  size_t want = 12;
  ssize_t br;
  // 这个算法基本上就是 AI 给到的方法，太复杂了
  // 实际上我们可以借助 vtime & vmin 完成超时判断
  // 不需要 poll
  while (got < want) {
      int ready = poll(&pfd, 1, 1000);
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
        return FP_IO_ERR;
      }
      
    br = read(fd, data + got, n - got);
    if (br == -1) {
      if (errno == EINTR)
        continue;
      else
        log_error("read failed: %s", strerror(errno));

      if (br == 0) {
        break;
      }
    }
    got += br;
    log_debug("n: %d, br: %d", n, br);
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

  bool delay = false;
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
