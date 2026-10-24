#define PAM_SM_AUTH
#define PAM_SM_ACCOUNT

#include "log.h"
#include "protocol.h"
#include "serial.h"
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
#define MAX_TIMEOUT (5.0f)
#define MAX_RETRY (3)

#define LOG_INFO(...) log_info(__VA_ARGS__)
#define LOG_DEBUG(...) log_debug(__VA_ARGS__)
#define LOG_ERROR(...) log_error(__VA_ARGS__)

#define GET_TIME(ts) clock_gettime(CLOCK_MONOTONIC, (ts))

/* ms 级的时间检测 */
static bool is_time_reached(const struct timespec *start, double timeout) {
  struct timespec now;
  if (GET_TIME(&now) != 0) {
    return true; // 如果无法拿到时间就永不等待
  }

  double time_diff = (double)(now.tv_sec - start->tv_sec) +
                     (double)(now.tv_nsec - start->tv_nsec) / 1E9;
  return timeout - time_diff <= 1E-3;
}

/* return fd, never fail but stuck at the loop when fd == -1 */
static int pam_open_fp() {
  int fd;

  log_info("now openning device: %s...", FINGER_MOD);
  while ((fd = fp_open(FINGER_MOD, DEFAULT_BAUDRATE)) == -1) {
    sleep(1);
  }

  log_info("device opened!");
  return fd;
}

/*
 * 下面这个代码就已经是原型了，但是需要完善如下的一些点：
 * - 重试机制：验证失败之后告知结果并等待一段时间后重新开放指纹验证
 * - 上锁：串口是独占资源，这里最好用协作调度方式
 *
 * FIXME: 指纹解锁成功后快速锁上，在一段窗口期内会再次解锁，即便没有做指纹验证
 *        窗口期大约从解锁成功后开始到绿灯熄灭后的 1s
 *
 * 其他暂时想不到了，然后是命令行功能基本要照着 fprint 那个来做吧
 */

PAM_EXTERN int pam_sm_authenticate(pam_handle_t *pamh, int flags, int argc,
                                   const char **argv) {

  LOG_INFO("pam_sm_authenticate called");

  enum fp_status stat;
  int pam_code = PAM_SUCCESS;
  int fd = pam_open_fp();

  int retries = 0;
  while ((stat = fp_auto_identify(fd, 2, 0xFFFF, true, true)) != FP_OK) {
    sleep(1); // wait for next retry

    if (stat == FP_ERR_TIMEOUT) {
      continue;
    }
    else if (stat == FP_ERR_IO) {
      fp_close(fd);
      fd = pam_open_fp();
      if (fd == -1) {
        pam_code = PAM_AUTHINFO_UNAVAIL;
        goto out;
      }
    }

    // 这里应该根据次数来记录 => MAX_RETRY
    if (++retries >= MAX_RETRY) {
      log_error("identify failed, max retry exceeded!");
      pam_code = PAM_AUTH_ERR;
      goto out;
    }
    log_warn("you still have %d chance(s) to identify", MAX_RETRY - retries);
  }

  log_info("identified!");
  pam_code = PAM_SUCCESS;

out:
  if (fd != -1)
    fp_close(fd);
  return pam_code;
}

// 这个函数会被 kwin_wayland_wrapper 调用，大概率是有用的
PAM_EXTERN int pam_sm_setcred(pam_handle_t *pamh, int flags, int argc,
                              const char **argv) {
  LOG_INFO("pam_sm_setcred called");

  return PAM_IGNORE;
}

PAM_EXTERN int pam_sm_acct_mgmt(pam_handle_t *pamh, int flags, int argc,
                                const char **argv) {
  LOG_INFO("pam_sm_acct_mgmt called");

  return PAM_IGNORE;
}
