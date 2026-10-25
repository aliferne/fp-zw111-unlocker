#define PAM_SM_AUTH
#define PAM_SM_ACCOUNT

#include "log.h"
#include "protocol.h"
#include <math.h>
#include "serial.h"
#include <errno.h>
#include <fcntl.h>
#include <security/pam_ext.h>
#include <security/pam_modules.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#define RECV_BUF_SIZE 64U
#define FINGER_MOD "/dev/FP_ZW111"
#define DEFAULT_BAUDRATE (B57600)
#define MAX_TIMEOUT (10.0f)
#define MAX_RETRY (5)

#define GET_TIME(ts) clock_gettime(CLOCK_MONOTONIC, (ts))

/* ms 级的时间检测 */
static bool is_time_reached(const struct timespec *start, double timeout) {
  struct timespec now;
  if (GET_TIME(&now) != 0) {
    return false;
  }

  double time_diff = (double)(now.tv_sec - start->tv_sec) +
                     (double)(now.tv_nsec - start->tv_nsec) / 1E9;
  return timeout - time_diff <= 1E-3;
}

static void delay_ms(unsigned int ms) {
  struct timespec req, rem;
  req.tv_sec = ms / 1000;
  req.tv_nsec = (ms % 1000) * 1E6;

  while (nanosleep(&req, &rem) == -1) {
    if (errno == EINTR)
      req = rem;
    else
      break;
  }
}

/* return fd or -1 if failed */
static int pam_open_fp() {
  int fd;
  int retries = 0;
  struct timespec start;

  if (GET_TIME(&start) != 0) {
    log_error("can not get time!");
    return -1;
  }

  log_info("now openning device: %s...", FINGER_MOD);
  while ((fd = fp_open(FINGER_MOD, DEFAULT_BAUDRATE)) == -1) {
    sleep(1);

    if (is_time_reached(&start, MAX_TIMEOUT)) {
      if (GET_TIME(&start) != 0) {
        log_error("can not get time!");
        return -1;
      }

      if (++retries > MAX_RETRY) {
        log_error("open failed, max retry exceeded!");
        return -1;
      }
      log_warn("trying openning device %s for %d/%d time(s) now", FINGER_MOD,
               retries, MAX_RETRY);
    }
  }

  log_info("device opened!");
  return fd;
}

/*
 * 下面这个代码就已经是原型了，但是需要完善如下的一些点：
 * - 重试机制：验证失败之后告知结果并等待一段时间后重新开放指纹验证
 * - 上锁：串口是独占资源，可能出现几个 sudo 要求共用验证的情况
 *          学学 flock
 *          需要让密码和指纹验证都能通过（并行化，类似于 kde 锁屏）
 *
 * FIXME: 指纹解锁成功后快速锁上，在一段窗口期内会再次解锁，即便没有做指纹验证
 *        窗口期大约从解锁成功后开始到绿灯熄灭后的 1s
 *        这个问题似乎不是我代码的问题，是 kscreenlocker 的问题
 *        我直接在下面那个 auth 函数 return PAM_SUCCESS 之后解锁再快速锁屏，
 *        这个 bug 依然会触发，这个问题不解决了
 *
 * 其他暂时想不到了，然后是命令行功能基本要照着 fprint 那个来做吧
 */

PAM_EXTERN int pam_sm_authenticate(pam_handle_t *pamh, int flags, int argc,
                                   const char **argv) {
  log_info("pam_sm_authenticate called with flags: %02X, argc: %d", flags,
           argc);

  enum fp_status status;
  int fd = pam_open_fp();
  if (fd == -1)
    goto err_io;

  int retry = 0;
  log_info("please place your finger on the module");

  while ((status = fp_auto_identify(fd, 2, 0xFFFF, true, false)) != FP_OK) {
    delay_ms(500);

    if (status == FP_ERR_TIMEOUT) {
      log_warn("module identify timeout, retrying...");
      continue;
    } else if (status == FP_ERR_IO) {
      log_error("device is removed!");
      fp_close(fd);
      if ((fd = pam_open_fp()) == -1)
        goto err_io;
    }

    // tried to unlock by finger not enrolled
    if (status == FP_ERR_FINGER) {
      if (++retry > MAX_RETRY)
        goto err_max_retry;

      // it'll show a QtWidget with the text
      pam_error(pamh, "你还有 %d 次重试机会", MAX_RETRY - retry);
      log_warn("you still have %d chance(s) to identify.", MAX_RETRY - retry);
      // 避免过快重新开始循环导致浪费一次识别机会
      delay_ms(500); 
    }
  }

  log_info("identified!");
  fp_close(fd);
  return PAM_SUCCESS;

err_io:
  log_error("can not open device %s!", FINGER_MOD);
  return PAM_AUTHINFO_UNAVAIL;

err_max_retry:
  log_error("identify failed, max retry exceeded!");
  fp_close(fd);
  return PAM_AUTH_ERR;
}

// 这个函数会被 kwin_wayland_wrapper 调用，大概率是有用的
PAM_EXTERN int pam_sm_setcred(pam_handle_t *pamh, int flags, int argc,
                              const char **argv) {
  // flags 返回 0x10 => PAM_REFRESH_CRED
  log_info("pam_sm_setcred called with flags: %02X, argc: %d", flags, argc);

  return PAM_IGNORE;
}

PAM_EXTERN int pam_sm_acct_mgmt(pam_handle_t *pamh, int flags, int argc,
                                const char **argv) {
  log_info("pam_sm_acct_mgmt called with flags: %02X, argc: %d", flags, argc);

  return PAM_IGNORE;
}
