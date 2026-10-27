/* 声明本模块将要实现的 PAM 接口类型 */
// compile with: `gcc -fPIC -shared -o pam_ignore.so pam_ignore.c`
// 这里是一个测试，实际上可以根据你需要的功能自行选择需要加入什么
// 比如你要 AUTH 就只写和 AUTH 相关的即可
// 本文件只做演示，不参与后续测试
#define PAM_SM_AUTH
#define PAM_SM_ACCOUNT
#define PAM_SM_SESSION
#define PAM_SM_PASSWORD

#include <security/pam_appl.h>
#include <security/pam_modules.h>

/* 以下函数必须全部存在，即使你的模块暂时不处理某个阶段 */
PAM_EXTERN int pam_sm_authenticate(pam_handle_t *pamh, int flags, int argc,
                                   const char **argv) {
  return PAM_IGNORE;
}

PAM_EXTERN int pam_sm_setcred(pam_handle_t *pamh, int flags, int argc,
                              const char **argv) {
  return PAM_IGNORE;
}

PAM_EXTERN int pam_sm_acct_mgmt(pam_handle_t *pamh, int flags, int argc,
                                const char **argv) {
  return PAM_IGNORE;
}

PAM_EXTERN int pam_sm_open_session(pam_handle_t *pamh, int flags, int argc,
                                   const char **argv) {
  return PAM_IGNORE;
}

PAM_EXTERN int pam_sm_close_session(pam_handle_t *pamh, int flags, int argc,
                                    const char **argv) {
  return PAM_IGNORE;
}

PAM_EXTERN int pam_sm_chauthtok(pam_handle_t *pamh, int flags, int argc,
                                const char **argv) {
  return PAM_IGNORE;
}
