#include "protocol.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void dump(const char *name, const uint8_t *b, size_t n) {
  printf("%-14s", name);
  for (size_t i = 0; i < n; i++)
    printf("%02X ", b[i]);
  printf("\n");
}

static void check(const char *name, const uint8_t *got, const uint8_t *want,
                  size_t n) {
  if (memcmp(got, want, n) == 0) {
    printf("[ok]   %s\n", name);
  } else {
    failures++;
    printf("[FAIL] %s\n", name);
    dump("  got:", got, n);
    dump("  want:", want, n);
  }
  fflush(stdout);
}

/* command pack with 5 params: light control, all on.
   wire: EF 01 FF FF FF FF 01 00 08 3C 03 07 07 00 00 00 56 */
static void test_fill_cmd(void) {
  const uint8_t params[5] = {0x03, 0x07, 0x07, 0x00, 0x00};
  const uint8_t want[17] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00,
                            0x08, 0x3C, 0x03, 0x07, 0x07, 0x00, 0x00, 0x00,
                            0x56};
  uint8_t out[sizeof(want)] = {0};

  size_t n = fp_fill_pack(out, FP_CMD_PACK, 0x3C, params, sizeof(params));
  if (n != sizeof(want)) {
    failures++;
    printf("[FAIL] fill_cmd size: got %zu, want %zu\n", n, sizeof(want));
    return;
  }
  check("fill_cmd", out, want, sizeof(want));
}

/* command pack without params: read sys param.
   wire: EF 01 FF FF FF FF 01 00 03 35 00 39 */
static void test_fill_cmd_noparam(void) {
  const uint8_t want[12] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
                            0x01, 0x00, 0x03, 0x35, 0x00, 0x39};
  uint8_t out[sizeof(want)] = {0};

  size_t n = fp_fill_pack(out, FP_CMD_PACK, 0x35, NULL, 0);
  if (n != sizeof(want)) {
    failures++;
    printf("[FAIL] fill_cmd_noparam size: got %zu, want %zu\n", n,
           sizeof(want));
    return;
  }
  check("fill_cmd_noparam", out, want, sizeof(want));
}

/* data pack with 4 data bytes, no code.
   wire: EF 01 FF FF FF FF 02 00 06 DE AD BE EF 03 40 */
static void test_fill_data(void) {
  const uint8_t data[4] = {0xDE, 0xAD, 0xBE, 0xEF};
  const uint8_t want[15] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x02, 0x00,
                            0x06, 0xDE, 0xAD, 0xBE, 0xEF, 0x03, 0x40};
  uint8_t out[sizeof(want)] = {0};

  size_t n = fp_fill_pack(out, FP_DATA_PACK, 0, data, sizeof(data));
  if (n != sizeof(want)) {
    failures++;
    printf("[FAIL] fill_data size: got %zu, want %zu\n", n, sizeof(want));
    return;
  }
  check("fill_data", out, want, sizeof(want));
}

/* checksum helper over a whole frame */
static void test_checksum(void) {
  const uint8_t frame[17] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00,
                             0x08, 0x3C, 0x03, 0x07, 0x07, 0x00, 0x00, 0x00,
                             0x56};
  uint16_t sum = fp_get_checksum(frame, sizeof(frame));
  if (sum == 0x0056) {
    printf("[ok]   checksum 0x%04X\n", sum);
  } else {
    failures++;
    printf("[FAIL] checksum: got 0x%04X, want 0x0056\n", sum);
  }
  fflush(stdout);
}

/* parse the confirm code out of a slave ack.
   wire: EF 01 FF FF FF FF 07 00 03 02 00 0C (confirm code = 0x02) */
static void test_status_code(void) {
  const uint8_t ack[12] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
                           0x07, 0x00, 0x03, 0x02, 0x00, 0x0C};
  uint8_t code = fp_get_status_code(ack);
  if (code == 0x02) {
    printf("[ok]   status_code 0x%02X\n", code);
  } else {
    failures++;
    printf("[FAIL] status_code: got 0x%02X, want 0x02\n", code);
  }
  fflush(stdout);
}

/* NULL / empty input must be rejected */
static void test_invalid(void) {
  uint8_t out[16];
  size_t n = fp_fill_pack(NULL, FP_CMD_PACK, 0x35, NULL, 0);
  if (n == 0) {
    printf("[ok]   reject NULL buf\n");
  } else {
    failures++;
    printf("[FAIL] NULL buf should be rejected\n");
  }
  n = fp_fill_pack(out, FP_CMD_PACK, 0x35, NULL, 1);
  if (n == 0) {
    printf("[ok]   reject NULL data\n");
  } else {
    failures++;
    printf("[FAIL] NULL data with data_len>0 should be rejected\n");
  }
  fflush(stdout);
}

int main(void) {
  test_fill_cmd();
  test_fill_cmd_noparam();
  test_fill_data();
  test_checksum();
  test_status_code();
  test_invalid();

  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
         failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
