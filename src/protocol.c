#include "protocol.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#ifdef PROTOCOL_DEBUG_ON
#include "log.h"
#define PROTOCOL_LOG(...) log_debug(__VA_ARGS__)
#else
#define PROTOCOL_LOG(...)
#endif

// TODO: 协议解析还是有点问题，主要是针对长度的处理上的问题

/* `data_len` depends on the frame you want to send */
struct fp_pack *init_fp_pack(uint32_t dev_addr, enum pack_id pid, uint8_t code,
                             uint16_t checksum, uint8_t *data,
                             uint16_t data_len) {
  if (pid == FP_SLAVE_PACK) {
    PROTOCOL_LOG("it makes no sense to initialize a slave pack in master");
    return NULL;
  }

  data_len -= 2; // skip length of checksum
  struct fp_pack *pack = calloc(1, sizeof(struct fp_pack) + data_len);
  if (pack != NULL) {
    PROTOCOL_LOG("fp_pack allocation success, initializing now...");
    pack->header = FP_PACK_START;
    pack->pack_id = pid;
    pack->dev_addr = dev_addr;
    pack->data_len = data_len;
    pack->checksum = checksum;

    if (data != NULL) {
      memcpy(pack->data, data, data_len);
    }

    if (pid == FP_CMD_PACK) {
      pack->code = code;
    }
  }

  return pack;
}

void deinit_fp_pack(struct fp_pack *pack) {
  if (pack) {
    free(pack);
    PROTOCOL_LOG("fp_pack free");
  }
}

/* build fp_pack from the buffer,
   used when you want to parse the binary stream, the argument `len` is useless,
   only for ensuring `buf` is large enough to build the whole fp_pack */
void build_fp_pack(struct fp_pack *pack, const uint8_t *buf, size_t len) {
  // slave pack e.g.: EF 01 FF FF FF FF 07 00 03 00 00 0A

  if (pack == NULL || buf == NULL) {
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
  pack->data_len = buf[7] << 8 | buf[8];

  if ((pack->pack_id == FP_CMD_PACK) || (pack->pack_id == FP_SLAVE_PACK)) {
    pack->code = buf[9];
    memcpy(pack->data, buf + 10, pack->data_len);
    pack->checksum =
        (buf[10 + pack->data_len] << 8) | buf[10 + pack->data_len + 1];
  } else {
    memcpy(pack->data, buf + 9, pack->data_len);
    pack->checksum =
        (buf[9 + pack->data_len] << 8) | buf[9 + pack->data_len + 1];
  }
}

/* parse fp_pack to build the buffer,
   used when you want to send the binary stream
   `len` is the length of `buf` */
void parse_fp_pack(struct fp_pack *pack, uint8_t *buf, size_t len) {
  // len should >= fp_pack_size
  // PROTOCOL_LOG("%u", FP_PACK_SIZE(pack));

  if (pack == NULL || pack->header != FP_PACK_START || buf == NULL) {
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
  buf[7] = (pack->data_len >> 8) & 0xFF;
  buf[8] = (pack->data_len & 0xFF);

  if ((pack->pack_id == FP_CMD_PACK) || (pack->pack_id == FP_SLAVE_PACK)) {
    buf[9] = pack->code;
    memcpy(buf + 10, pack->data, pack->data_len);
    memset(buf + 10 + pack->data_len, pack->checksum, sizeof(pack->checksum));
  } else {
    memcpy(buf + 9, pack->data, pack->data_len);
    memset(buf + 9 + pack->data_len, pack->checksum, sizeof(pack->checksum));
  }
}
