#if 0
#include "log.h"
#include "protocol.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define PROTOCOL_ERROR(...) log_error(__VA_ARGS__)
#ifdef PROTOCOL_DEBUG_ON
#define PROTOCOL_LOG(...) log_debug(__VA_ARGS__)
#else
#define PROTOCOL_LOG(...)
#endif

/*
 * 这个版本不使用变长结构体：
 *   - 结构体大小固定，载荷由 `pdata` 指向外部缓冲区，不再内联 data[]；
 *   - 不再保存 data_len，数据长度由 pack_len 推导；
 *   - 不再 malloc/free，结构体由调用者持有（栈上即可），所以没有 deinit。
 *
 * 调用示例：
 *
 *   #include "protocol.h"
 *
 *   // ---- 1) 发送一条命令 ----
 *   uint8_t params[2] = {0x00, 0x01};
 *   struct fp_pack send;
 *   fp_pack_init(&send, FP_DEFAULT_ADDR, FP_CMD_PACK, 5, 0x35, params, 0);
 *   send.checksum = fp_pack_checksum(&send);      // 由字段算出校验和
 *
 *   uint8_t tx[64];
 *   parse_fp_pack(&send, tx, sizeof(tx));         // tx = 待写串口的字节流
 *   write(fd, tx, TOTAL_PACK_SIZE(send.pack_len));
 *
 *   // ---- 2) 接收一条应答 ----
 *   uint8_t rx[64];
 *   ssize_t n = read(fd, rx, sizeof(rx));
 *
 *   struct fp_pack recv;                           // recv.pdata 指向 rx 内部
 *   build_fp_pack(&recv, rx, (size_t)n);
 *   if (IS_FP_OK(recv.code)) {
 *     uint16_t ret_len = recv.pack_len - 3;        // 返回参数长度
 *     const uint8_t *ret = recv.pdata;             // 返回参数
 *   }
 *
 * 注意：build_fp_pack 之后 recv.pdata 指进 rx，要用就得保证 rx 还活着。
 */

/* 数据长度不再存字段，直接由 pack_len 推导 */

/* 校验和 = pack_id + pack_len(2 字节) + [code] + data，取低 16 位 */
uint16_t fp_pack_checksum(const struct fp_pack *pack) {
  if (pack == NULL) {
    return 0;
  }

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

  return sum;
}

static bool verify_checksum(const struct fp_pack *pack) {
  return pack != NULL && fp_pack_checksum(pack) == pack->checksum;
}

/* 用外部参数初始化一个调用者持有的结构体（不分配内存） */
void fp_pack_init(struct fp_pack *pack, uint32_t dev_addr, enum pack_id pid,
                  uint16_t pack_len, uint8_t code, const uint8_t *data,
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

/* 结构体 -> 字节流（发送），`len` 是 buf 的容量 */
void parse_fp_pack(struct fp_pack *pack, uint8_t *buf, size_t len) {
  if (pack == NULL || buf == NULL || pack->pdata == NULL) {
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
    memcpy(buf + 10, pack->pdata, data_len);
  } else {
    memcpy(buf + 9, pack->pdata, data_len);
  }
  buf[p_size - 2] = (pack->checksum >> 8) & 0xFF;
  buf[p_size - 1] = pack->checksum & 0xFF;
}

/* 字节流 -> 结构体（接收），`len` 是实际收到的字节数，pdata 指进 buf */
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
#endif
