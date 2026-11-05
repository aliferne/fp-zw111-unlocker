#define PAM_SM_AUTH
#define PAM_SM_ACCOUNT

#include <security/pam_appl.h>
#include <security/pam_ext.h>
#include <security/pam_modules.h>
#include <stdio.h>
#include <string.h>

// 不建议明文存储到代码里，这里只用于学习用
#define TEST_USER "testusr"
#define TEST_PASSWD "testpwd"

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

  retval = pam_get_authtok(pamh, PAM_AUTHTOK, &password, "Password: ");
  if (retval != PAM_SUCCESS) {
    fprintf(stderr, "[pam_testauth] pam_get_authtok failed: %d\n", retval);
    return retval;
  }

  fprintf(stderr, "[pam_testauth] got user='%s' pass='%s'\n", user, password);

  if (strcmp(user, TEST_USER) == 0 && strcmp(password, TEST_PASSWD) == 0) {
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
