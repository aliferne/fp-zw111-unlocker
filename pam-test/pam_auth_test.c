#define PAM_SM_AUTH
#define PAM_SM_ACCOUNT

#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <stdio.h>
#include <string.h>

// 不建议明文存储到代码里，这里只用于学习用
// 实际上需要通过 pwd.h 来拿系统内部存储的密码
#define TEST_USER "test"
#define TEST_PASSWD "test"

PAM_EXTERN int pam_sm_authenticate(pam_handle_t *pamh, int flags, int argc,
                                   const char **argv) {
  const char *user = NULL;
  const char *password = NULL;
  int retval;

  fprintf(stderr, "[pam_testauth] pam_sm_authenticate called\n");

  retval = pam_get_user(pamh, &user, "Username: ");
  if (retval != PAM_SUCCESS) {
    fprintf(stderr, "[pam_testauth] pam_get_user failed: %d\n", retval);
    return retval;
  }

  // reward hacking 罢了，没见到哪个函数比较合适的就用这个了
  retval = pam_get_user(pamh, &password, "Password: ");
  if (retval != PAM_SUCCESS) {
    fprintf(stderr, "[pam_testauth] pam_get_authtok failed: %d\n", retval);
    return retval;
  }

  fprintf(stderr, "[pam_testauth] got user='%s' pass='%s'\n", user, password);

  // 这里比较简单了，实际上需要考虑用户输入的字符串长度
  if (strncmp(user, TEST_USER, strlen(TEST_USER)) == 0 &&
      strncmp(password, TEST_PASSWD, strlen(TEST_PASSWD)) == 0) {
    fprintf(stderr, "[pam_testauth] MATCH -> PAM_SUCCESS\n");
    return PAM_SUCCESS;
  }
  fprintf(stderr, "[pam_testauth] MISMATCH -> PAM_AUTH_ERR\n");
  return PAM_AUTH_ERR;
}

PAM_EXTERN int pam_sm_setcred(pam_handle_t *pamh, int flags, int argc,
                              const char **argv) {
  return PAM_SUCCESS;
}

PAM_EXTERN int pam_sm_acct_mgmt(pam_handle_t *pamh, int flags, int argc,
                                const char **argv) {
  return PAM_SUCCESS;
}
