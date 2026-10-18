/* case 2 proof: does VMIN=225/VTIME=150 make read() wait?
 * case 4 proof: does the un-cleared ICRNL mangle a 0x0D byte in the stream?
 *
 * uses the real serial_set_attr() from src/serial.c, driven by a pty pair.
 *
 * build: gcc -Iinc ai/repro_tty.c -lutil -o /tmp/repro_tty
 */
#include "../inc/serial.h"
#include "../src/serial.c"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pty.h>

static double now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* write `clen` bytes to the "module" side, then time how long read(9) takes */
static void timed_read(const char *label, int master, int slave, int vtime,
                       int vmin, const uint8_t *chunk, size_t clen) {
  serial_set_attr(slave, 57600, DATABIT_8, STOPBIT_1, PARITY_NON, vtime, vmin);
  tcflush(slave, TCIOFLUSH);

  uint8_t buf[16] = {0};
  write(master, chunk, clen);

  double t0 = now_ms();
  ssize_t n = read(slave, buf, 9);
  double t1 = now_ms();

  printf("[timing] %-9s VMIN=%-3d VTIME=%-3d -> read(9) got %zd in %6.1f ms\n",
         label, vmin, vtime, n, t1 - t0);
}

/* feed one byte on the module side and show what the host actually reads */
static void icrnl_roundtrip(const char *label, int master, int slave,
                            uint8_t send) {
  struct termios t;
  tcgetattr(slave, &t);

  uint8_t got = 0;
  write(master, &send, 1);
  usleep(50 * 1000);
  read(slave, &got, 1);

  printf("[icrnl]  %-9s ICRNL=%s  sent 0x%02X -> read 0x%02X  %s\n", label,
         (t.c_iflag & ICRNL) ? "ON " : "off", send, got,
         got == send ? "ok" : "MANGLED!");
}

int main(void) {
  int master, slave;
  if (openpty(&master, &slave, NULL, NULL, NULL) != 0) {
    perror("openpty");
    return 1;
  }

  /* ---- case 2: read latency vs VMIN/VTIME ---- */
  const uint8_t two[] = {0xEF, 0x01};
  const uint8_t burst[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
                           0x07, 0x00, 0x03, 0x00, 0x00, 0x0A};
  timed_read("frag", master, slave, 150, 225, two, sizeof(two));
  timed_read("burst", master, slave, 150, 225, burst, sizeof(burst));
  timed_read("VMIN=1", master, slave, 0, 1, two, sizeof(two));
  timed_read("raw", master, slave, 0, 0, two, sizeof(two));

  /* ---- case 4: ICRNL translation ---- */
  serial_set_attr(slave, 57600, DATABIT_8, STOPBIT_1, PARITY_NON, 0, 0);
  icrnl_roundtrip("as-is", master, slave, 0x0D);

  struct termios t;
  tcgetattr(slave, &t);
  t.c_iflag &= ~(ICRNL | INLCR | IGNCR);
  tcsetattr(slave, TCSANOW, &t);
  icrnl_roundtrip("cleared", master, slave, 0x0D);

  close(master);
  close(slave);
  return 0;
}
