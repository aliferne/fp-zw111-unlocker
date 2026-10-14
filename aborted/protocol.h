#if 0
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <sys/cdefs.h>
#include <sys/types.h>

/*
 * 说实话这个协议的代码有点过度设计了…
 * 变长结构体和一堆长度转来转去的成面条代码了，还不如直接写死那几个常量呢
 */

#define FP_PACK_START (0xEF01)
#define FP_DEFAULT_ADDR (0xFFFFFFFF)

/* the total size of these: header, dev_addr, pack_id, pack_len */
#define PROTOCOL_HEADER_SIZE (9U)
/* the length of a pack in a uint8_t array */
#define TOTAL_PACK_SIZE(pack_len) (PROTOCOL_HEADER_SIZE + (pack_len))

/* pack id */
enum pack_id {
  FP_CMD_PACK = 1U,
  FP_SLAVE_PACK = 7U,
  FP_DATA_PACK = 2U,
  FP_DATAEND_PACK = 8U,
};
/* pack id end */

#define FP_PACK_SIZE(pack) (sizeof(*(pack)) + (pack)->data_len)
#define IS_FP_OK(status_code) ((status_code) == FP_OK)
#define IS_CODE_VALID(pack_id)                                                 \
  ((pack_id) == FP_CMD_PACK || (pack_id) == FP_SLAVE_PACK)

typedef enum {
  FP_OK = 0x00,
  FP_ERR_RECV = 0x01,
  FP_ERR_NO_FINGER = 0x02,
  FP_ERR_ENROLL_FAIL = 0x03,
  FP_ERR_IMG_TOO_DRY = 0x04,
  FP_ERR_IMG_TOO_WET = 0x05,
  FP_ERR_IMG_TOO_MESSY = 0x06,
  FP_ERR_TOO_FEW_FEATURES = 0x07,
  FP_ERR_NOT_MATCH = 0x08,
  FP_ERR_NOT_FOUND = 0x09,
  FP_ERR_MERGE_FAIL = 0x0A,
  FP_ERR_ADDR_OUT_OF_RANGE = 0x0B,
  FP_ERR_READ_TEMPLATE = 0x0C,
  FP_ERR_UPLOAD_FEATURE = 0x0D,
  FP_ERR_RECV_PACKET = 0x0E,
  FP_ERR_UPLOAD_IMG = 0x0F,
  FP_ERR_DELETE_TEMPLATE = 0x10,
  FP_ERR_CLEAR_LIBRARY = 0x11,
  FP_ERR_LOW_POWER = 0x12,
  FP_ERR_WRONG_PASSWORD = 0x13,
  FP_ERR_RESET_FAIL = 0x14,
  FP_ERR_NO_VALID_IMG = 0x15,
  // 0x16 未定义
  FP_ERR_RESIDUAL_FINGER = 0x17,
  FP_ERR_FLASH_READ_WRITE = 0x18,
  // 0x19 未定义
  FP_ERR_INVALID_REG = 0x1A,
  FP_ERR_REG_CONTENT = 0x1B,
  FP_ERR_NOTE_PAGE = 0x1C,
  FP_ERR_PORT_OP = 0x1D,
  FP_ERR_AUTO_ENROLL = 0x1E,
  FP_ERR_LIBRARY_FULL = 0x1F,
  FP_ERR_DEVICE_ADDR = 0x20,
  FP_ERR_PASSWORD = 0x21,
  FP_ERR_TEMPLATE_NOT_EMPTY = 0x22,
  FP_ERR_TEMPLATE_EMPTY = 0x23,
  FP_ERR_LIBRARY_EMPTY = 0x24,
  FP_ERR_ENROLL_COUNT = 0x25,
  FP_ERR_TIMEOUT = 0x26,
  FP_ERR_ALREADY_EXISTS = 0x27,
  FP_ERR_TEMPLATE_RELATED = 0x28,
  FP_ERR_SENSOR_INIT = 0x29,
  FP_ERR_MODULE_INFO_NOT_EMPTY = 0x2A,
  FP_ERR_MODULE_INFO_EMPTY = 0x2B,
  // 0x2C ~ 0x32 未定义
  FP_ERR_IMG_AREA_SMALL = 0x33,
  FP_ERR_IMG_UNAVAILABLE = 0x34,
  FP_ERR_ILLEGAL_DATA = 0x35,
  // 0x36 ~ 0x3F 未定义
  FP_ERR_ENROLL_TIMES = 0x40
} fpstat_t;

struct fp_pack {
  /* default 0xEF01 */
  uint16_t header;
  /* 0xFFFFFFFF if not set,
    otherwise it should be set as the addr of your device */
  uint32_t dev_addr;
  /* 1: command pack, 2: data pack(with data behind)
     8: data pack(no more data)
     7: this pack is sent from the slave(ack pack) */
  uint8_t pack_id;
  /* the length of data plus 2 (number of bytes of checksum)
     when `code` is valid, it should plus 1 again
     **DO NOT USE** this to iterate data array */
  uint16_t pack_len;
  /* if the `code` not in use, `pack_len = data_len + 2`
     otherwise `pack_len = data_len + 3`,
     **USE** this to iterate the data array */
  uint16_t data_len;
  /* command when this is a cmd pack
     status_code when this is a slave pack
     if neither of them, this is not in use */
  uint8_t code;
  uint16_t checksum;
  /* data that relys outside buffer */
  const uint8_t *pdata;
};

/*
 * 基本用法上目前应该初定为：
 *  1. `init_fp_pack` 出两个结构体，一个接收一个发送
 *      这一步会根据协议内容确定两个结构体的 data_len
 *  2. `parse_fp_pack` 发送的结构体，将二进制流发给指纹模块
 *  3. `build_fp_pack` 接收的结构体，根据结构体内信息进一步除了
 *  4. `deinit_fp_pack` 掉两个结构体
 */

void fp_pack_init(struct fp_pack *pack, uint32_t dev_addr, enum pack_id pid,
                  uint16_t pack_len, uint8_t code, uint8_t *data,
                  uint16_t checksum);

// TODO: 这个初始化方式对于只需要接收的来说有点太复杂了，不需要那么多参数
// 用个宏包装一下
struct fp_pack *init_fp_pack(uint32_t dev_addr, enum pack_id pid,
                             uint16_t pack_len, uint8_t code, uint8_t *data,
                             uint16_t checksum);
#define INIT_RECV_FP_PACK(pack, pack_len)                                      \
  fp_pack_init((pack), FP_DEFAULT_ADDR, FP_SLAVE_PACK, (pack_len), 0x00, NULL, \
               0x0000)

void deinit_fp_pack(struct fp_pack *pack);
void build_fp_pack(struct fp_pack *pack, const uint8_t *buf, size_t len);
void parse_fp_pack(struct fp_pack *pack, uint8_t *buf, size_t len);

#endif
#endif
