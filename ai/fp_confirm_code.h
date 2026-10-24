#ifndef FP_CONFIRM_CODE_H
#define FP_CONFIRM_CODE_H

/* ZW111 指令应答确认码（用户手册 V1.5.1，3.2 指令应答）
 *
 * 注意：不要和 inc/protocol.h 里的 enum fp_status 混用：
 *   - enum fp_status  是本地 API 的返回值（含负数 IO/超时错误）；
 *   - enum fp_confirm_code 是模组应答包里的 1 字节确认码。
 * 所以这里统一用 FP_ACK_ 前缀，避免与 FP_OK / FP_ERR_TIMEOUT 冲突。
 *
 * 手册未定义 0x16，故留空。
 */
enum fp_confirm_code {
  FP_ACK_OK = 0x00,                        /* 指令执行完毕或 OK */
  FP_ACK_PACK_ERR = 0x01,                  /* 数据包接收错误 */
  FP_ACK_NO_FINGER = 0x02,                 /* 传感器上没有手指 */
  FP_ACK_ENROLL_FAILED = 0x03,             /* 录入指纹图像失败 */
  FP_ACK_IMG_TOO_DRY = 0x04,               /* 图像太干、太淡，生不成特征 */
  FP_ACK_IMG_TOO_WET = 0x05,               /* 图像太湿、太糊，生不成特征 */
  FP_ACK_IMG_TOO_MESSY = 0x06,             /* 图像太乱，生不成特征 */
  FP_ACK_TOO_FEW_FEATURES = 0x07,          /* 特征点太少（或面积太小） */
  FP_ACK_NOT_MATCH = 0x08,                 /* 指纹不匹配 */
  FP_ACK_NOT_FOUND = 0x09,                 /* 没搜索到指纹 */
  FP_ACK_MERGE_FAILED = 0x0A,              /* 特征合并失败 */
  FP_ACK_PAGE_OUT_OF_RANGE = 0x0B,         /* 地址序号超出指纹库范围 */
  FP_ACK_READ_TEMPLATE_ERR = 0x0C,         /* 读模板出错或无效 */
  FP_ACK_UPLOAD_FEATURE_FAILED = 0x0D,     /* 上传特征失败 */
  FP_ACK_CANNOT_RECV_MORE = 0x0E,          /* 模组不能接收后续数据包 */
  FP_ACK_UPLOAD_IMG_FAILED = 0x0F,         /* 上传图像失败 */
  FP_ACK_DELETE_FAILED = 0x10,             /* 删除模板失败 */
  FP_ACK_EMPTY_FAILED = 0x11,              /* 清空指纹库失败 */
  FP_ACK_ENTER_LOW_POWER_FAILED = 0x12,    /* 不能进入低功耗状态 */
  FP_ACK_WRONG_PASSWORD = 0x13,            /* 口令不正确 */
  FP_ACK_SYS_RESET_FAILED = 0x14,          /* 系统复位失败 */
  FP_ACK_NO_VALID_IMG = 0x15,              /* 缓冲区无有效原始图，生不成图像 */
  FP_ACK_RESIDUAL_FINGER = 0x17,           /* 残留指纹或两次采集间手指未移动 */
  FP_ACK_FLASH_ERR = 0x18,                 /* 读写 FLASH 出错 */
  FP_ACK_INVALID_REG = 0x1A,               /* 无效寄存器号 */
  FP_ACK_REG_CONTENT_ERR = 0x1B,           /* 寄存器设定内容错误 */
  FP_ACK_INVALID_NOTE_PAGE = 0x1C,         /* 记事本页码指定错误 */
  FP_ACK_PORT_OP_FAILED = 0x1D,            /* 端口操作失败 */
  FP_ACK_AUTO_ENROLL_FAILED = 0x1E,        /* 自动注册失败 */
  FP_ACK_LIB_FULL = 0x1F,                  /* 指纹库满 */
  FP_ACK_WRONG_DEV_ADDR = 0x20,            /* 设备地址错误 */
  FP_ACK_WRONG_PASS = 0x21,                /* 密码有误 */
  FP_ACK_TEMPLATE_NOT_EMPTY = 0x22,        /* 指纹模板非空 */
  FP_ACK_TEMPLATE_EMPTY = 0x23,            /* 指纹模板为空 */
  FP_ACK_LIB_EMPTY = 0x24,                 /* 指纹库为空 */
  FP_ACK_WRONG_ENROLL_TIMES = 0x25,        /* 录入次数设置错误 */
  FP_ACK_TIMEOUT = 0x26,                   /* 超时 */
  FP_ACK_ALREADY_EXISTS = 0x27,            /* 指纹已存在 */
  FP_ACK_TEMPLATE_RELATED = 0x28,          /* 指纹模板有关联 */
  FP_ACK_SENSOR_INIT_FAILED = 0x29,        /* 传感器初始化失败 */
  FP_ACK_MODULE_INFO_NOT_EMPTY = 0x2A,     /* 模组信息非空 */
  FP_ACK_MODULE_INFO_EMPTY = 0x2B,         /* 模组信息为空 */
  FP_ACK_IMG_TOO_SMALL = 0x33,             /* 图像面积小 */
  FP_ACK_IMG_INVALID = 0x34,               /* 图像不可用 */
  FP_ACK_ILLEGAL_DATA = 0x35,              /* 非法数据 */
  FP_ACK_ENROLL_TIMES_NOT_ENOUGH = 0x40,   /* 注册次数少于规定次数 */
};

/* 目前只需要的两个判断宏，参数是应答包里的确认码 */
#define IS_FP_OK(status_code) ((status_code) == FP_ACK_OK)
#define IS_FP_TIMEOUT(status_code) ((status_code) == FP_ACK_TIMEOUT)

#endif /* FP_CONFIRM_CODE_H */
