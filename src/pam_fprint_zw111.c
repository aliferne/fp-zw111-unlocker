#include <fcntl.h>
#include <security/_pam_types.h>
#define PAM_SM_AUTH
#define PAM_SM_ACCOUNT

#include "log.h"
#include <security/pam_appl.h>
#include <security/pam_ext.h>
#include <security/pam_modules.h>

#define LOG_INFO(...) log_info(__VA_ARGS__)
#define LOG_DEBUG(...) log_debug(__VA_ARGS__)
#define LOG_ERROR(...) log_error(__VA_ARGS__)

#include "protocol.h"
#include "serial.h"
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#define RECV_BUF_SIZE 64U
#define FINGER_MOD "/dev/FP_ZW111"
#define DEFAULT_BAUDRATE (B57600)

/*
 * 下面这个代码就已经是原型了，但是需要完善如下的一些点：
 * - 重试机制：验证失败之后告知结果并等待一段时间后重新开放指纹验证
 * - 上锁：串口是独占资源，这里最好用协作调度方式
 * - 固定串口文件设备：有时是 /dev/ttyUSB0, 有时是 USB1，为开发带来了一些麻烦
 *
 * 其他暂时想不到了，然后是命令行功能基本要照着 fprint 那个来做吧
 */

PAM_EXTERN int pam_sm_authenticate(pam_handle_t *pamh, int flags, int argc,
                                   const char **argv) {

  LOG_INFO("pam_sm_authenticate called");

  log_info("now openning device %s...", FINGER_MOD);
  int fd = 0;
  int count = 0;
  while ((fd = open(FINGER_MOD, O_RDWR | O_NOCTTY | O_NDELAY)) == -1) {
    log_error("can not open device %s! retrying...", FINGER_MOD);
    sleep(1);
    if (++count > 30) {
      return -1;
    }
  }
  count = 0;
  // if (fd == -1) {
  //   log_error("can not open device %s!", FINGER_MOD);
  //   return -1;
  // }
  log_info("device %s opened, "
           "now configuring terminal parameters...",
           FINGER_MOD);

  fcntl(fd, F_SETFL, 0);
  /* VMIN=1, VTIME=0: read returns as soon as data is available, the frame
     length is handled by fp_recv_pack instead of the tty layer */
  if (serial_set_attr(fd, DEFAULT_BAUDRATE, DATABIT_8, STOPBIT_1, PARITY_NON, 0,
                      0) != 0) {
    log_error("failed to configure the serial port!");
    close(fd);
    return -1;
  }

  while (fp_auto_identify(fd, 2, 0xFFFF, true, true) != 0x00) {
    sleep(1); // wait for next retry
    if (++count > 30) {
      return -1;
    }
  }

  return PAM_SUCCESS;
}

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
