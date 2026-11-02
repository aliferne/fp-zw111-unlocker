#include "waiter.h"
#include <errno.h>
#include <time.h>
#include <unistd.h>

double mono_ms() {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    return 0;

  return (double)(now.tv_sec) * 1E4L + (double)(now.tv_nsec) / 1E6L;
}

void delay_ms(unsigned int ms) {
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

enum wait_stat exec_until(unsigned int timeout_ms, unsigned int interval,
                          callback fn, void *ctx) {
  enum wait_stat ret;
  double maxtime = mono_ms() + (double)timeout_ms / 1000;

  while (1) {
    ret = fn(ctx);
    if (ret != WAIT_AGAIN)
      return ret;

    delay_ms(interval);
    if (mono_ms() > maxtime)
      return WAIT_TIMEOUT;
  }
}

const char *wstatstr(enum wait_stat wstat) {
  static const char *wstat_str[] = {
      [WAIT_DONE] = "fn done",
      [WAIT_AGAIN] = "fn asked for doing again",
      [WAIT_FAIL] = "fn failed",
      [WAIT_BLOCKED] = "fn is blocked by other processes",
      [WAIT_TIMEOUT] = "waiting for fn execution timeout",
  };

  return wstat_str[(int)wstat];
}
