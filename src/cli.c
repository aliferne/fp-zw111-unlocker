/* fp - ZW111 fingerprint module CLI skeleton (getopt_long demo).
 *
 * build: gcc -Wall -Wextra -Iinc ai/fp.c src/protocol.c src/serial.c src/log.c
 * -o fp usage: ./fp --enroll --id 1 --times 3
 *        ./fp --identify
 *        ./fp --light --mode 1 --color 6
 */

#include "log.h"
#include "protocol.h"
#include "serial.h"

#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define DEFAULT_DEV "/dev/FP_ZW111"
#define DEFAULT_BAUD 57600

enum cmd {
  CMD_NONE,
  CMD_ENROLL,
  CMD_IDENTIFY,
  CMD_DELETE,
  CMD_EMPTY,
  CMD_CHECK,
  CMD_SYS,
  CMD_CANCEL,
  CMD_SLEEP,
  CMD_LIGHT,
};

/* long-only options (val >= 256 so they don't clash with short opts) */
enum { OPT_MODE = 256, OPT_COLOR };

static void usage(const char *prog) {
  printf("usage: %s <command> [options]\n\n", prog);
  printf("commands:\n");
  printf("  -e, --enroll         enroll one finger (needs --id, optional "
         "--times)\n");
  printf("  -v, --identify       1:1/1:N search for a finger(optional --id)\n");
  printf("  -d, --delete         delete templates (needs --id, optional "
         "--count)\n");
  printf("  -E, --empty          delete all templates\n");
  printf("  -c, --check          handshake / check module is alive\n");
  printf("  -S, --sys-param      read system basic parameters\n");
  printf("  -C, --cancel         cancel auto enroll / identify\n");
  printf("  -s, --sleep          put module to sleep\n");
  printf("  -L, --light          turn on led (use --mode/--color)\n");
  printf("\noptions:\n");
  printf("  -D, --device PATH    serial device (default %s)\n", DEFAULT_DEV);
  printf("  -i, --id N           finger id / start id (default 0)\n");
  printf("  -t, --times N        enroll times, 2..5 (default 3)\n");
  printf("  -n, --count N        number of templates to delete (default 1)\n");
  printf("      --mode N         led mode 1..7 (default 3 = always on)\n");
  printf("      --color N        led color 0..7 (default 7 = rgb)\n");
  printf("  -h, --help           show this help\n");
}

static int run_command(int fd, enum cmd cmd, uint16_t id, uint8_t times,
                       uint16_t count, enum fp_light_mode mode, uint8_t color) {
  enum fp_status st;

  switch (cmd) {
  case CMD_ENROLL:
    st = fp_auto_enroll(fd, id, times, true, false, true, false, false);
    break;
  case CMD_IDENTIFY:
    st = fp_auto_identify(fd, 2, id, true, false);
    break;
  case CMD_DELETE:
    st = fp_delete_char(fd, id, count);
    break;
  case CMD_EMPTY:
    st = fp_empty(fd);
    break;
  case CMD_CHECK:
    st = fp_check_module_state(fd);
    break;
  case CMD_SYS:
    st = fp_read_sys_para(fd);
    break;
  case CMD_CANCEL:
    st = fp_cancel(fd);
    break;
  case CMD_SLEEP:
    st = fp_sleep(fd);
    break;
  case CMD_LIGHT:
    st = fp_control_light(fd, mode, color, color, 0, 0);
    break;
  default:
    return 2;
  }

  if (st == FP_OK) {
    log_info("done");
    return 0;
  }
  log_error("command failed (status 0x%02X, reason: %s)", st, fp_strstatus(st));
  return 1;
}

int main(int argc, char **argv) {
  enum cmd cmd = CMD_NONE;
  const char *dev = DEFAULT_DEV;
  uint16_t id = 0xFFFF; // default value for validate
  uint8_t times = 3;
  uint16_t count = 1;
  enum fp_light_mode mode = FP_LIGHT_ON;
  uint8_t color = FP_LIGHT_RED | FP_LIGHT_GREEN | FP_LIGHT_BLUE;

  static struct option longopts[] = {
      {"enroll", no_argument, 0, 'e'},
      {"identify", no_argument, 0, 'v'},
      {"delete", no_argument, 0, 'd'},
      {"empty", no_argument, 0, 'E'},
      {"check", no_argument, 0, 'c'},
      {"sys-param", no_argument, 0, 'S'},
      {"cancel", no_argument, 0, 'C'},
      {"sleep", no_argument, 0, 's'},
      {"light", no_argument, 0, 'L'},
      {"device", required_argument, 0, 'D'},
      {"id", required_argument, 0, 'i'},
      {"times", required_argument, 0, 't'},
      {"count", required_argument, 0, 'n'},
      {"mode", required_argument, 0, OPT_MODE},
      {"color", required_argument, 0, OPT_COLOR},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0},
  };

  int opt;
  while ((opt = getopt_long(argc, argv, "evdEcSsLD:i:t:n:h", longopts, NULL)) !=
         -1) {
    switch (opt) {
    case 'e':
      cmd = CMD_ENROLL;
      break;
    case 'v':
      cmd = CMD_IDENTIFY;
      break;
    case 'd':
      cmd = CMD_DELETE;
      break;
    case 'E':
      cmd = CMD_EMPTY;
      break;
    case 'c':
      cmd = CMD_CHECK;
      break;
    case 'S':
      cmd = CMD_SYS;
      break;
    case 'C':
      cmd = CMD_CANCEL;
      break;
    case 's':
      cmd = CMD_SLEEP;
      break;
    case 'L':
      cmd = CMD_LIGHT;
      break;
    case 'D':
      dev = optarg;
      break;
    case 'i':
      id = (uint16_t)strtoul(optarg, NULL, 0);
      break;
    case 't':
      times = (uint8_t)strtoul(optarg, NULL, 0);
      break;
    case 'n':
      count = (uint16_t)strtoul(optarg, NULL, 0);
      break;
    case OPT_MODE:
      mode = (enum fp_light_mode)strtoul(optarg, NULL, 0);
      break;
    case OPT_COLOR:
      color = (uint8_t)strtoul(optarg, NULL, 0);
      break;
    case 'h':
      usage(argv[0]);
      return 0;
    default:
      break;
    }
  }

  if (cmd == CMD_NONE) {
    usage(argv[0]);
    return 2;
  }

  int fd = fp_open(dev, DEFAULT_BAUD);
  if (fd < 0) {
    log_error("can not open device %s!", dev);
    return 1;
  }

  int rc = run_command(fd, cmd, id, times, count, mode, color);

  fp_close(fd);

  return rc;
}
