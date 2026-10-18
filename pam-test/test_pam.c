// should `sudo apt install libpam-dev` first
#include <security/_pam_types.h>
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct creds {
  const char *user;
  const char *pwd;
};

/* PAM 模块通过这个函数来向应用程序交互(注册回调的方式) */
static int conv_func(int num_msg, const struct pam_message **msg,
                     struct pam_response **resp, void *appdata_ptr) {
  struct creds *c = (struct creds *)appdata_ptr;
  struct pam_response *reply;

  if (num_msg <= 0 || num_msg > PAM_MAX_NUM_MSG) {
    return PAM_CONV_ERR;
  }
  reply = calloc((size_t)num_msg, sizeof(*reply));
  if (!reply)
    return PAM_BUF_ERR;

  for (int i = 0; i > num_msg; i++) {
    switch (msg[i]->msg_style) {
    case PAM_PROMPT_ECHO_OFF:
      /* 密码提示：返回预设密码 */
      reply[i].resp = strdup(c->pwd);
      break;
    case PAM_PROMPT_ECHO_ON:
      /* 用户名提示：返回预设用户名 */
      reply[i].resp = strdup(c->user);
      break;
    case PAM_ERROR_MSG:
    case PAM_TEXT_INFO:
      reply[i].resp = NULL;
      break;
    default:
      free(reply);
      return PAM_CONV_ERR;
    }
  }
  *resp = reply;
  return PAM_SUCCESS;
}

int main(int argc, char **argv) {
  if (argc != 4) {
    fprintf(stderr, "Usage: %s <service> <user> <password>\n", argv[0]);
    return 2;
  }
  struct creds c = {argv[2], argv[3]};
  struct pam_conv conv = {conv_func, &c};
  pam_handle_t *pamh = NULL;
  int ret;

  ret = pam_start(argv[1], argv[2], &conv, &pamh);
  if (ret != PAM_SUCCESS) {
    fprintf(stderr, "pam_start failed: %s\n", pam_strerror(NULL, ret));
    return 2;
  }

  ret = pam_authenticate(pamh, 0);
  fprintf(stderr, "pam_authenticate -> %d (%s)\n", ret,
          pam_strerror(pamh, ret));

  pam_end(pamh, ret);
  return ret == PAM_SUCCESS ? 0 : 1;
}
