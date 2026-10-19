#define PAM_SM_AUTH
#define PAM_SM_ACCOUNT

#include <stdio.h>
#include <security/pam_appl.h>
#include <security/pam_modules.h>

/* 以下函数必须全部存在，即使你的模块暂时不处理某个阶段 */
PAM_EXTERN int pam_sm_authenticate(pam_handle_t *pamh, int flags, int argc,
                                   const char **argv) {
  fprintf(stderr, "[kde_authtest] called\n");
  return PAM_SUCCESS;
}

PAM_EXTERN int pam_sm_setcred(pam_handle_t *pamh, int flags, int argc,
                              const char **argv) {
  return PAM_SUCCESS;
}

PAM_EXTERN int pam_sm_acct_mgmt(pam_handle_t *pamh, int flags, int argc,
                                const char **argv) {
  return PAM_SUCCESS;
}
