#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FP_PACK_START (0xEF01)
#define FP_DEV_ADDR (0xFFFFFFFF)

/* command results:
 *   FP_OK           success (device confirm code 0x00)
 *   FP_ERR_IO       transport/IO error, e.g. the device was unplugged
 *   FP_ERR_TIMEOUT  no response from the device within the timeout
 *   FP_ERR_PROTO    malformed response (bad length/...)
 *   FP_FAIL         generic failure (device confirm code != 0x00, bad args...)
 * Failure details are written to the log. */
enum fp_status {
  FP_ERR_PROTO = -3,
  FP_ERR_TIMEOUT = -2,
  FP_ERR_IO = -1,
  FP_OK = 0,
  FP_FAIL = 1,
};

/* sum of the bytes that the checksum covers, takes the frame size as `len` */
uint16_t fp_get_checksum(const uint8_t *buf, size_t len);

/* the confirm code (a.k.a. status code) at offset FP_OFF_CODE */
uint8_t fp_get_status_code(const uint8_t *recv_buf);

/* All command functions below return FP_OK on success and a non-zero
 * enum fp_status on failure. */
enum fp_status fp_read_sys_para(int fd);

enum fp_status fp_check_sensor_state(int fd);

/* cancel auto enroll & auto identify */
enum fp_status fp_cancel(int fd);

enum fp_status fp_sleep(int fd);

/* delete N finger templates start from the specific ID number */
enum fp_status fp_delete_char(int fd, uint16_t id, uint16_t n);

/* delete all finger templates in the flash */
enum fp_status fp_empty(int fd);

/* 多功能三色灯 (指令码 0x3C) */
enum fp_light_mode {
  FP_LIGHT_BREATH = 0x01,   /* 呼吸灯 */
  FP_LIGHT_FLASH = 0x02,    /* 闪烁灯 */
  FP_LIGHT_ON = 0x03,       /* 常开灯 */
  FP_LIGHT_OFF = 0x04,      /* 常闭灯 */
  FP_LIGHT_FADE_IN = 0x05,  /* 渐开灯 */
  FP_LIGHT_FADE_OUT = 0x06, /* 渐灭灯 */
  FP_LIGHT_MARQUEE = 0x07,  /* 跑马灯，仅跑马灯模组适用 */
};

/* 颜色码按位组合：红 | 绿 | 蓝，例如 FP_LIGHT_RED | FP_LIGHT_BLUE */
#define FP_LIGHT_RED (0x04)
#define FP_LIGHT_GREEN (0x02)
#define FP_LIGHT_BLUE (0x01)

int fp_open(const char *devname, unsigned int baudrate);

#define fp_close(fd) close((fd))

/* 控制模组的三色灯。
 *
 * Args:
 *  - fd: file descriptor.
 *  - mode: 灯效，取 enum fp_light_mode.
 *  - start_color: 起始颜色，普通呼吸灯时表示由灭到亮的颜色，
 *                 其他功能与 end_color 保持一致。
 *  - end_color: 结束颜色（仅呼吸灯使用）；闪烁灯时该字节表示占空比，
 *               高四位:低四位，例如 0x38 表示 3:8。
 *  - cycles: 循环次数，0x00 表示无限循环，最大值 0xFF；
 *            仅呼吸灯、闪烁灯、渐开渐闭灯有效。
 *  - period: 周期，1 表示 0.1 秒，范围 1-100，
 *            仅呼吸灯、闪烁灯、渐开渐闭灯有效。
 *
 * Return 0 on success, non-zero on failure
 */
enum fp_status fp_control_light(int fd, enum fp_light_mode mode,
                                uint8_t start_color, uint8_t end_color,
                                uint8_t cycles, uint8_t period);

/* auto enroll finger print, each call enrolls one finger,
 * the module shines green light when success, red blinking light when failed.
 *
 * Args:
 *  - fd: file descriptor.
 *  - id: finger print id to store.
 *  - enroll_times
 *  - led_ctrl: led off when a finger print is enrolled.
 *  - return_status: ask for returning at subprocess.
 *                   that means it'll return data at the key step in the
 *                   flowchart of the documentation, TURN OFF RECOMMENDED
 *                   (This function is not opened, it'll always be false)
 *
 *  - allow_overwrite: overwrite the id.
 *                     notice that id is not equal to the finger,
 *                     you may fail to enroll the same finger even though
 *                     you allowed overwrite the id
 *
 *  - allow_duplicate: reregister the control position.
 *                     control position is the same sematics as finger print
 *                     that is to say, you can overwrite the enrolled finger
 *                     even though there is already one
 *
 *  - require_remove: require remove finger to enter next enroll.
 *                    if this is set, you should remove and put your
 *                    finger again while enrolling the finger print
 *
 * Return 0 on success, non-zero on failure
 */
enum fp_status fp_auto_enroll(int fd, uint16_t id, uint8_t enroll_times,
                              bool led_ctrl, bool return_status,
                              bool allow_overwrite, bool allow_duplicate,
                              bool require_remove);

/* auto identify finger print,
 * the module shines green light when success, red blinking light when failed.
 *
 * Args:
 *  - fd: file descriptor.
 *  - score_level: the level to identify finger, range: [1, 5]
 *                 the larger it is, the harder finger to identify
 *                 (check more features)
 *                 2 is recommend by default.
 *
 *  - id: finger print id to store.
 *  - led_ctrl: led off when a finger print is enrolled.
 *  - return_status: ask for returning at subprocess.
 *                   that means it'll return data at the key step in the
 *                   flowchart of the documentation, TURN OFF RECOMMENDED
 *                   (This function is not opened, it'll always be false)
 *
 * Return 0 on success, non-zero on failure
 */
enum fp_status fp_auto_identify(int fd, uint8_t score_level, uint16_t id,
                                bool led_ctrl, bool return_status);

#endif
