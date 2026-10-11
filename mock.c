#include "protocol.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void dump(const char *name, const uint8_t *b, size_t n) {
  printf("%-14s", name);
  for (size_t i = 0; i < n; i++)
    printf("%02X ", b[i]);
  printf("\n");
  fflush(stdout);
}

/* parse a command pack (id=1) with 2 bytes of params.
   wire should be: EF 01 FF FF FF FF 01 00 05 36 01 02 12 34
   pack_len=5 (code1 + data2 + chk2), data_len=2, checksum=0x1234 */
static void test_parse_cmd(void) {
  uint8_t params[2] = {0x01, 0x02};
  struct fp_pack *p =
      init_fp_pack(FP_DEFAULT_ADDR, FP_CMD_PACK, 5, 0x36, params, 0x1234);
  uint8_t out[32];
  memset(out, 0xAA, sizeof(out));
  parse_fp_pack(p, out, sizeof(out));
  dump("parse cmd:", out, 14);
  printf("expected:     EF 01 FF FF FF FF 01 00 05 36 01 02 12 34\n");
  fflush(stdout);
  deinit_fp_pack(p);
}

/* parse a data pack (id=2, no code) with 3 data bytes.
   wire: EF 01 FF FF FF FF 02 00 05 AA BB CC 00 04
   pack_len=5 (data3 + chk2), data_len=3, checksum=0x0004 */
static void test_parse_data(void) {
  uint8_t data[3] = {0xAA, 0xBB, 0xCC};
  struct fp_pack *p =
      init_fp_pack(FP_DEFAULT_ADDR, FP_DATA_PACK, 5, 0, data, 0x0004);
  uint8_t out[32];
  memset(out, 0xAA, sizeof(out));
  parse_fp_pack(p, out, sizeof(out));
  dump("parse data:", out, 14);
  printf("expected:     EF 01 FF FF FF FF 02 00 05 AA BB CC 00 04\n");
  fflush(stdout);
  deinit_fp_pack(p);
}

/* build (parse) a real slave ack that carries 2 bytes of return params.
   wire: EF 01 FF FF FF FF 07 00 05 00 01 02 00 0E
   pack_len=5 (conf1 + data2 + chk2), so data_len must be 2 */
static void test_build_slave(void) {
  uint8_t wire[14] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x07,
                      0x00, 0x05, 0x00, 0x01, 0x02, 0x00, 0x0E};
  struct fp_pack *p = init_fp_pack(FP_DEFAULT_ADDR, FP_CMD_PACK, 3, 0, NULL, 0);
  build_fp_pack(p, wire, sizeof(wire));
  printf("build slave: pack_len=%u data_len=%u code=%02X chk=%04X data=[",
         p->pack_len, p->data_len, p->code, p->checksum);
  for (unsigned i = 0; i < p->data_len; i++)
    printf("%02X ", p->data[i]);
  printf("]\n");
  printf("expected:    pack_len=5 data_len=2 code=00 chk=000E data=[01 02 ]\n");
  fflush(stdout);
  deinit_fp_pack(p);
}

/* init with a data pack then a small pack_len -> underflow of data_len */
static void test_init_underflow(void) {
  struct fp_pack *p =
      init_fp_pack(FP_DEFAULT_ADDR, FP_CMD_PACK, 1, 0x36, NULL, 0);
  printf("underflow:   pack_len=%u data_len=%u (expect reject/NULL)\n",
         p ? p->pack_len : 0, p ? p->data_len : 0);
  fflush(stdout);
  deinit_fp_pack(p);
}

int main(int argc, char **argv) {
  int t = argc > 1 ? atoi(argv[1]) : 0;
  switch (t) {
  case 1:
    test_parse_cmd();
    break;
  case 2:
    test_parse_data();
    break;
  case 3:
    test_build_slave();
    break;
  case 4:
    test_init_underflow();
    break;
  default:
    test_parse_cmd();
    test_parse_data();
    test_build_slave();
    break;
  }
  return 0;
}
