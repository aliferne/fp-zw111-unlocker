#ifndef __WAITER_H
#define __WAITER_H

enum wait_stat {
  WAIT_DONE = 0,
  WAIT_AGAIN, // try again
  WAIT_FAIL,
  WAIT_BLOCKED, // blocked by other processes
  WAIT_TIMEOUT,
};

typedef enum wait_stat (*callback)(void *ctx);

/* get monotic ms */
long mono_ms();

/* delay for ms */
void delay_ms(unsigned int ms);

/* execute `fn(ctx)` until `timeout_ms` reached, each loop waiting for the
   `interval`(ms), notice that `fn(ctx)` should return `WAIT_AGAIN` to loop
   again until it reached the caller's expectation, if `timeout_ms == 0`,
   it'll wait until `fn(ctx)` returns `WAIT_DONE` */
enum wait_stat exec_fn_until(long timeout_ms, unsigned int interval,
                             callback fn, void *ctx);

/* turn wait_stat to string literal */
const char *wstatstr(enum wait_stat wstat);

#endif
