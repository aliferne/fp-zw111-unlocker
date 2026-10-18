/* case 1 investigation: does fp_recv_pack lose payload when the header is
 * split across reads?
 *
 * A) small ack frame (pack_len=0x0003), split after 3 bytes
 * B) large data frame (pack_len=0x0103), split after 8 bytes (last byte of
 *    the pack_len field has not arrived yet)
 *
 * build: gcc -Iinc ai/repro_recv.c src/log.c -lutil -o /tmp/repro_recv
 */
#include "../inc/protocol.h"
#include "../inc/serial.h"

#include "../src/serial.c"
#include "../src/protocol.c"

#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int run_case(const char *label, const uint8_t *frame, size_t flen,
                    size_t split, size_t buflen) {
  int master, slave;
  openpty(&master, &slave, NULL, NULL, NULL);
  serial_set_attr(slave, 57600, DATABIT_8, STOPBIT_1, PARITY_NON, 0, 0);

  pid_t pid = fork();
  if (pid == 0) {
    close(slave);
    usleep(200 * 1000); /* let the host read the first chunk */
    write(master, frame + split, flen - split);
    usleep(100 * 1000);
    _exit(0);
  }

  write(master, frame, split); /* partial header */
  uint8_t *buf = calloc(1, buflen);
  int n = fp_recv_pack(slave, buf, buflen, 1.0f);

  int ok = (n == (int)flen) && memcmp(buf, frame, flen) == 0;
  printf("[%s] split after %zu bytes, frame=%zu -> recv returned %d : %s\n",
         label, split, flen, n,
         ok ? "ok" : "FAIL (payload truncated / wrong)");

  free(buf);
  close(master);
  close(slave);
  waitpid(pid, NULL, 0);
  return ok;
}

int main(void) {
  /* A: normal ack, pack_len = 0x0003 */
  const uint8_t ack[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
                         0x07, 0x00, 0x03, 0x00, 0x00, 0x0A};

  /* B: data frame, pack_len = 0x0103 (257 data + 2 checksum), total 268 */
  uint8_t big[9 + 0x0103];
  memset(big, 0xAA, sizeof(big));
  big[0] = 0xEF;
  big[1] = 0x01;
  big[2] = big[3] = big[4] = big[5] = 0xFF;
  big[6] = 0x08; /* data pack */
  big[7] = 0x01;
  big[8] = 0x03; /* pack_len = 0x0103 */
  big[sizeof(big) - 2] = 0x12;
  big[sizeof(big) - 1] = 0x34;

  run_case("A small", ack, sizeof(ack), 3, 64);
  run_case("B large", big, sizeof(big), 8, sizeof(big));
  return 0;
}
