#include "waiter.h"
#include <errno.h>
#include <time.h>
#include <unistd.h>

long mono_ms() {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    return 0;

  return (now.tv_sec) * 1000 + (now.tv_nsec) / 1E6;
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

enum wait_stat exec_fn_until(long timeout_ms, unsigned int interval,
                             callback fn, void *ctx) {
  enum wait_stat ret;
  long maxtime = mono_ms() + timeout_ms;

  while (1) {
    ret = fn(ctx);
    if (ret != WAIT_AGAIN)
      return ret;

    delay_ms(interval);
    if (timeout_ms != 0 && mono_ms() > maxtime)
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
