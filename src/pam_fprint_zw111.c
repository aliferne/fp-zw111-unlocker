#define PAM_SM_AUTH
#define PAM_SM_ACCOUNT

#ifdef PAM_USE_LOG_H
#include "log.h"
#else // notice that you should declare `pamh` before call them
#include <syslog.h>
#define log_debug(...) pam_syslog(pamh, LOG_DEBUG, __VA_ARGS__)
#define log_info(...) pam_syslog(pamh, LOG_INFO, __VA_ARGS__)
#define log_warn(...) pam_syslog(pamh, LOG_WARNING, __VA_ARGS__)
#define log_error(...) pam_syslog(pamh, LOG_ERR, __VA_ARGS__)
#endif

#include "protocol.h"
#include "waiter.h"
#include <security/pam_ext.h>
#include <security/pam_modules.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/file.h>
#include <termios.h>
#include <unistd.h>

#define FINGER_MOD "/dev/FP_ZW111"
#define DEFAULT_BAUDRATE (B57600)
#define MAX_TIMEOUT (10 * 1000U)
#define FP_IDENTIFY_MAX_RETRY (5)
#define FP_ERR_MAX_RETRY (5)

struct pam_dev_ctx {
  int *fd;
  pam_handle_t *pamh;
};

static enum wait_stat pam_open_dev(void *ctx) {
  struct pam_dev_ctx *pCtx = ctx;
  int *fd = pCtx->fd;
  enum wait_stat ret = WAIT_DONE;
  const pam_handle_t *pamh = pCtx->pamh;

  *fd = fp_open(FINGER_MOD, DEFAULT_BAUDRATE);
  if (*fd >= 0) {
    log_info("device opened!");
    return ret;
  }
  ret = (*fd == -2) ? WAIT_BLOCKED : WAIT_AGAIN;

  *fd = -1;
  return ret;
}

static enum wait_stat pam_remove_finger(void *ctx) {
  static bool warned = false;
  enum wait_stat ret = WAIT_DONE;
  struct pam_dev_ctx *pCtx = ctx;
  const pam_handle_t *pamh = pCtx->pamh;

  uint8_t dev_status = fp_get_image(*(pCtx->fd));
  /* 如果检测有手指就提醒，其他情况均认为是函数返回失败 */
  if (FP_IS_STATUS(dev_status, FP_OK)) {
    ret = WAIT_AGAIN;
    if (warned == false) {
      log_warn("Please remove your finger");
      pam_info(pCtx->pamh, "请先移开你的手指");
      warned = true;
    }
  } else if (!FP_IS_STATUS(dev_status, FP_NO_FINGER)) {
    ret = WAIT_FAIL;
  }

  return ret;
}

/*
 * FIXME: 指纹解锁成功后快速锁上，在一段窗口期内会再次解锁，即便没有做指纹验证
 *        窗口期大约从解锁成功后开始到绿灯熄灭后的 1s
 *        这个问题似乎不是我代码的问题，是 kscreenlocker 的问题
 *        我直接在下面那个 auth 函数 return PAM_SUCCESS 之后解锁再快速锁屏，
 *        这个 bug 依然会触发，这个问题不解决了
 */

PAM_EXTERN int pam_sm_authenticate(pam_handle_t *pamh, int flags, int argc,
                                   const char **argv) {
  log_info("pam_sm_authenticate called with flags: 0x%02X, argc: %d", flags,
           argc);

  int ret = PAM_SUCCESS;
  uint8_t status;
  int fd = -1;
  enum wait_stat wst;
  struct pam_dev_ctx pCtx = {.fd = &fd, .pamh = pamh};

  wst = exec_fn_until(MAX_TIMEOUT, 1000, pam_open_dev, &pCtx);
  if (wst != WAIT_DONE) {
    log_error("can not open device %s (err: %s)", FINGER_MOD, wstatstr(wst));
    pam_info(pamh, "无法连接设备");
    ret = PAM_AUTHINFO_UNAVAIL;
    goto cleanup;
  }

  int err_retry = 0;
  int identify_retry = 0;
  int resp_max_retry = 0;

  pam_info(pamh, "指纹模块已连接，可以指纹识别");
  log_info("please place your finger on the module");

  /* 此处表示以置信度 2 (较为常用) 的等级遍历所有指纹，只要找到符合的就返回
     FP_OK 这意味着只要有人录入指纹，他就可以解锁 Linux PC
     上任何一个用户，这当然是不安全的，
     但我的场景只是自用，因此不考虑这些安全问题。
     毕竟如果真要安全的话就不会选择自己 DIY 了(doge) */
  while ((status = fp_auto_identify(fd, 2, 0xFFFF, true, false, 15000)) !=
         FP_OK) {
    delay_ms(100);

    switch (status) {
    // 这种情况是设备/poll 超时
    case FP_TIMEOUT:
      log_warn("identify timeout, retrying...");
      if (++resp_max_retry >= 3) { // wait for circa 20s
        log_error("max retry for timeout exceeded!");
        ret = PAM_AUTH_ERR;
        goto cleanup;
      }
      continue;

    case FP_IO_ERR:
      log_warn("device is removed!");
      pam_info(pamh, "指纹模块已断开连接，尝试重连中...");

      fp_close(fd);

      wst = exec_fn_until(MAX_TIMEOUT, 1000, pam_open_dev, &pCtx);
      if (wst != WAIT_DONE) {
        log_error("can not open device %s (err: %s)", FINGER_MOD,
                  wstatstr(wst));
        pam_info(pamh, "重连失败");
        ret = PAM_AUTHINFO_UNAVAIL;
        goto cleanup;
      }

      log_info("device reconnected");
      pam_info(pamh, "指纹模块已连接，可以指纹识别");
      continue;

      // tried to unlock by finger not enrolled
    case FP_FINGER_NOT_MATCHED:
      if (++identify_retry >= FP_IDENTIFY_MAX_RETRY) {
        log_error("identify failed, max retry exceeded!");
        ret = PAM_AUTH_ERR;
        goto cleanup;
      }

      wst = exec_fn_until(MAX_TIMEOUT, 500, pam_remove_finger, &pCtx);
      if (wst != WAIT_DONE) {
        log_error("ask for removing finger failed (wstatstr: %s)",
                  wstatstr(wst));
        ret = PAM_AUTHINFO_UNAVAIL;
        goto cleanup;
      }

      int times_left = FP_IDENTIFY_MAX_RETRY - identify_retry;
      // it'll show a QtWidget with the text
      pam_info(pamh, "你还有 %d 次重试机会", times_left);
      log_warn("you still have %d chance(s) to identify.", times_left);
      continue;

    case FP_FINGER_STORAGE_EMPTY:
    case FP_FINGER_TEMPLATE_EMPTY:
      log_error("something went wrong(errcode: %d, errstr: %s)", status,
                fp_strcode(status));
      ret = PAM_AUTHINFO_UNAVAIL;
      goto cleanup;

    case FP_FAIL:
    default:
      log_error("something went wrong(errcode: %d, errstr: %s)", status,
                fp_strcode(status));
      if (++err_retry >= FP_ERR_MAX_RETRY) {
        log_error("fp_err_max_retry exceeded!");
        ret = PAM_AUTH_ERR;
        goto cleanup;
      }
      break;
    }
  }

  log_info("identified!");

cleanup:
  if (fd >= 0) {
    if (ret != PAM_SUCCESS) {
      fp_cancel(fd);
    }
    fp_close(fd);
  }

  return ret;
}

// 这个函数会被 kwin_wayland_wrapper 调用，大概率是有用的
PAM_EXTERN int pam_sm_setcred(pam_handle_t *pamh, int flags, int argc,
                              const char **argv) {
  // flags 返回 0x10 => PAM_REFRESH_CRED
  log_info("pam_sm_setcred called with flags: 0x%02X, argc: %d", flags, argc);
  (void)pamh;
  (void)argv;

  return PAM_IGNORE;
}

PAM_EXTERN int pam_sm_acct_mgmt(pam_handle_t *pamh, int flags, int argc,
                                const char **argv) {
  log_info("pam_sm_acct_mgmt called with flags: 0x%02X, argc: %d", flags, argc);
  (void)pamh;
  (void)argv;

  return PAM_IGNORE;
}
