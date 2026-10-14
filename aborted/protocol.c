#if 0
#include "protocol.h"
#include "log.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define PROTOCOL_ERROR(...) log_error(__VA_ARGS__)
#ifdef PROTOCOL_DEBUG_ON
#define PROTOCOL_LOG(...) log_debug(__VA_ARGS__)
#else
#define PROTOCOL_LOG(...)
#endif

// TODO: 依旧是处理长度问题和 pdata 的拷贝问题

static uint16_t pack_data_len(uint16_t pack_len, uint8_t pack_id) {
  return pack_len - (IS_CODE_VALID(pack_id) ? 3U : 2U);
}

static uint16_t get_checksum(struct fp_pack *pack) {
  if (pack == NULL) {
    return 0;
  }

  // sum of every byte from pack_id to checksum(exclusive), low 16 bits
  uint16_t sum = pack->pack_id;
  sum += (pack->pack_len >> 8) & 0xFF;
  sum += pack->pack_len & 0xFF;
  if (IS_CODE_VALID(pack->pack_id)) {
    sum += pack->code;
  }
  if (pack->pdata != NULL) {
    uint16_t n = pack_data_len(pack->pack_len, pack->pack_id);
    for (uint16_t i = 0; i < n; i++) {
      sum += pack->pdata[i];
    }
  }
  PROTOCOL_LOG("calculated checksum: 0x%04X, pack->checksum: 0x%04X", sum,
               pack->checksum);
  return sum;
}

static bool verify_checksum(struct fp_pack *pack) {
  return get_checksum(pack) == pack->checksum;
}

void fp_pack_init(struct fp_pack *pack, uint32_t dev_addr, enum pack_id pid,
                  uint16_t pack_len, uint8_t code, uint8_t *data,
                  uint16_t checksum) {
  if (pack == NULL) {
    return;
  }

  memset(pack, 0, sizeof(*pack));
  pack->header = FP_PACK_START;
  pack->dev_addr = dev_addr;
  pack->pack_id = pid;
  pack->pack_len = pack_len;
  pack->code = code;
  pack->pdata = data;
  pack->checksum = checksum;
}

void parse_fp_pack(struct fp_pack *pack, uint8_t *buf, size_t len) {
  if (pack == NULL || buf == NULL) {
#if 0
   pack->pdata == NULL;
          PROTOCOL_LOG("can not build fp_pack, check these parameters:\n"
                       "\tpack: %s\n"
                       "\tbuf: %s\n"
                       "\tbuf_len < fp_pack_size: %d",
                       (pack == NULL) ? "NULL" : "<struct fp_pack *pack>",
                       (buf == NULL) ? "NULL" : "<const uint8_t *buf>",
                       len < FP_PACK_SIZE(pack));
#endif
    return;
  }

  uint16_t min = IS_CODE_VALID(pack->pack_id) ? 3U : 2U;
  if (pack->pack_len < min || len < TOTAL_PACK_SIZE(pack->pack_len)) {
    return;
  }

  uint16_t p_size = TOTAL_PACK_SIZE(pack->pack_len);
  uint16_t data_len = pack_data_len(pack->pack_len, pack->pack_id);

  buf[0] = (pack->header >> 8) & 0xFF;
  buf[1] = pack->header & 0xFF;
  buf[2] = (pack->dev_addr >> 24) & 0xFF;
  buf[3] = (pack->dev_addr >> 16) & 0xFF;
  buf[4] = (pack->dev_addr >> 8) & 0xFF;
  buf[5] = pack->dev_addr & 0xFF;
  buf[6] = pack->pack_id;
  buf[7] = (pack->pack_len >> 8) & 0xFF;
  buf[8] = pack->pack_len & 0xFF;

  if (IS_CODE_VALID(pack->pack_id)) {
    buf[9] = pack->code;
    // memcpy(buf + 10, pack->pdata, data_len);
  } else {
    // memcpy(buf + 9, pack->pdata, data_len);
  }

  buf[p_size - 2] = (pack->checksum >> 8) & 0xFF;
  buf[p_size - 1] = pack->checksum & 0xFF;
}

void build_fp_pack(struct fp_pack *pack, const uint8_t *buf, size_t len) {
  if (pack == NULL || buf == NULL || len < PROTOCOL_HEADER_SIZE + 2) {
    return;
  }
  if (((buf[0] << 8) | buf[1]) != FP_PACK_START) {
    PROTOCOL_LOG("bad header: %02X%02X", buf[0], buf[1]);
    return;
  }

  uint8_t pid = buf[6];
  uint16_t pack_len = (buf[7] << 8) | buf[8];
  uint16_t min = IS_CODE_VALID(pid) ? 3U : 2U;
  if (pack_len < min || len < TOTAL_PACK_SIZE(pack_len)) {
    PROTOCOL_ERROR("bad pack_len: %u (buf has %zu)", pack_len, len);
    return;
  }

  pack->header = FP_PACK_START;
  pack->dev_addr = buf[2] << 24 | buf[3] << 16 | buf[4] << 8 | buf[5];
  pack->pack_id = pid;
  pack->pack_len = pack_len;
  if (IS_CODE_VALID(pid)) {
    pack->code = buf[9];
    pack->pdata = buf + 10;
  } else {
    pack->code = 0;
    pack->pdata = buf + 9;
  }
  pack->checksum = (buf[TOTAL_PACK_SIZE(pack_len) - 2] << 8) |
                   buf[TOTAL_PACK_SIZE(pack_len) - 1];

  if (!verify_checksum(pack)) {
    PROTOCOL_ERROR("checksum mismatch");
  }
}

#if 0
/* `data_len` depends on the frame you want to send */
struct fp_pack *init_fp_pack(uint32_t dev_addr, enum pack_id pid,
                             uint16_t pack_len, uint8_t code, uint8_t *data,
                             uint16_t checksum) {

  uint16_t min = IS_CODE_VALID(pid) ? 3 : 2;
  if (pack_len < min) {
    PROTOCOL_ERROR("pack_len too small: %u < %u", pack_len, min);
    return NULL;
  }

  uint16_t data_len = pack_len - min;
  PROTOCOL_LOG("pack_len: %u, data_len: %d", pack_len, data_len);

  struct fp_pack *pack = calloc(1, sizeof(struct fp_pack) + data_len);
  if (pack != NULL) {
    PROTOCOL_LOG("fp_pack allocation success, initializing now...");
    pack->header = FP_PACK_START;
    pack->pack_id = pid;
    pack->dev_addr = dev_addr;
    pack->pack_len = pack_len;
    pack->data_len = data_len;
    pack->checksum = checksum;
    pack->code = code;

    if (data != NULL) {
      memcpy(pack->data, data, data_len);
    }
  }

  return pack;
}

void deinit_fp_pack(struct fp_pack *pack) {
  if (pack) {
    free(pack);
    PROTOCOL_LOG("fp_pack freed");
  }
}

/* build fp_pack from the buffer,
   used when you want to parse the binary stream, the argument `len` is useless,
   only for ensuring `buf` is large enough to build the whole fp_pack */
void build_fp_pack(struct fp_pack *pack, const uint8_t *buf, size_t len) {
  // slave pack e.g.: EF 01 FF FF FF FF 07 00 03 00 00 0A

  if (pack == NULL || buf == NULL || len < TOTAL_PACK_SIZE(pack->pack_len)) {
#if 0
    PROTOCOL_LOG("can not build fp_pack, check these parameters:\n"
                 "\tpack: %s\n"
                 "\tbuf: %s\n"
                 "\tbuf_len < fp_pack_size: %d",
                 (pack == NULL) ? "NULL" : "<struct fp_pack *pack>",
                 (buf == NULL) ? "NULL" : "<const uint8_t *buf>",
                 len < FP_PACK_SIZE(pack));
#endif
    return;
  }

  uint16_t header;
  if ((header = buf[0] << 8 | buf[1]) != FP_PACK_START) {
    PROTOCOL_LOG("error happened when parsing header: header != %04X",
                 FP_PACK_START);
    return;
  }

  pack->header = header;
  pack->dev_addr = buf[2] << 24 | buf[3] << 16 | buf[4] << 8 | buf[5];
  pack->pack_id = buf[6];
  pack->pack_len = buf[7] << 8 | buf[8];
  uint16_t p_size = TOTAL_PACK_SIZE(pack->pack_len);

  if (IS_CODE_VALID(pack->pack_id)) {
    pack->code = buf[9];
    pack->data_len = pack->pack_len - 3;
    memcpy(pack->data, buf + 10, pack->data_len);
    pack->checksum = buf[p_size - 2] << 8 | buf[p_size - 1];
  } else {
    pack->data_len = pack->pack_len - 2;
    memcpy(pack->data, buf + 9, pack->data_len);
    pack->checksum = buf[p_size - 2] << 8 | buf[p_size - 1];
  }

  if (!verify_checksum(pack)) {
    PROTOCOL_ERROR("FAILED to verify the checksum, pack maybe broken");
  }
}

/* parse fp_pack to build the buffer,
   used when you want to send the binary stream
   `len` is the length of `buf` */
void parse_fp_pack(struct fp_pack *pack, uint8_t *buf, size_t len) {
  // len should >= fp_pack_size
  // PROTOCOL_LOG("%u", FP_PACK_SIZE(pack));

  if (pack == NULL || pack->header != FP_PACK_START || buf == NULL ||
      len < TOTAL_PACK_SIZE(pack->pack_len)) {
#if 0
// 似乎会超出输出上限导致 core dump
    PROTOCOL_LOG("can not build fp_pack, check these parameters:\n"
                 "\tpack: %s\n"
                 "\theader != %04X: %d\n"
                 "\tbuf: %s\n"
                 "\tbuf_len < fp_pack_size: %d",
                 (pack == NULL) ? "NULL" : "<struct fp_pack *pack>",
                 pack->header != FP_PACK_START,
                 (buf == NULL) ? "NULL" : "<const uint8_t *buf>",
                 len < FP_PACK_SIZE(pack));
#endif
    return;
  }

  buf[0] = (pack->header >> 8) & 0xFF;
  buf[1] = (pack->header & 0xFF);
  buf[2] = (pack->dev_addr >> 24) & 0xFF;
  buf[3] = (pack->dev_addr >> 16) & 0xFF;
  buf[4] = (pack->dev_addr >> 8) & 0xFF;
  buf[5] = (pack->dev_addr & 0xFF);
  buf[6] = pack->pack_id;
  buf[7] = (pack->pack_len >> 8) & 0xFF;
  buf[8] = (pack->pack_len & 0xFF);

  uint16_t p_size = TOTAL_PACK_SIZE(pack->pack_len);
  if (IS_CODE_VALID(pack->pack_id)) {
    buf[9] = pack->code;
    memcpy(buf + 10, pack->data, pack->data_len);
  } else {
    memcpy(buf + 9, pack->data, pack->data_len);
  }

  buf[p_size - 2] = (pack->checksum >> 8) & 0xFF;
  buf[p_size - 1] = pack->checksum & 0xFF;
}
#endif
#endif
