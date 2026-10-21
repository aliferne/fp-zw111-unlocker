#include "log.h"

#include "protocol.h"
#include "serial.h"
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#define RECV_BUF_SIZE 64U
#define FINGER_MOD "/dev/FP_ZW111"
#define DEFAULT_BAUDRATE (B57600)

int main(void) {
  int fd = fp_open(FINGER_MOD, DEFAULT_BAUDRATE);
  if (fd == -1)
    return -1;

  // fp_check_sensor_state(fd);
  // fp_read_sys_para(fd);
  // fp_empty(fd);
  // fp_auto_enroll(fd, 1, 3, true, false, true, true, true);
  fp_auto_identify(fd, 2, 0xFFFF, true, true);

  /* verify all solid colors, 常开灯, 1s each */
  // static const struct {
  //   const char *name;
  //   uint8_t code;
  // } colors[] = {
  //     {"off", 0x00},
  //     {"blue", FP_LIGHT_BLUE},
  //     {"green", FP_LIGHT_GREEN},
  //     {"red", FP_LIGHT_RED},
  //     {"red+green", FP_LIGHT_RED | FP_LIGHT_GREEN},
  //     {"red+blue", FP_LIGHT_RED | FP_LIGHT_BLUE},
  //     {"green+blue", FP_LIGHT_GREEN | FP_LIGHT_BLUE},
  //     {"red+green+blue", FP_LIGHT_RED | FP_LIGHT_GREEN | FP_LIGHT_BLUE},
  // };

  // for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
  //   log_info("light: %-15s (0x%02X)", colors[i].name, colors[i].code);
  //   fp_control_light(fd, FP_LIGHT_ON, colors[i].code, colors[i].code, 0, 0);
  //   sleep(1);
  // }

  fp_close(fd);
  return 0;
}
