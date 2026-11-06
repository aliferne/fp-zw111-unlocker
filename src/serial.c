#include "serial.h"
#include <errno.h>
#include <sys/ioctl.h>
#include <termios.h>

#ifdef SERIAL_DEBUG_ON
#include "log.h"
#else
#define log_debug(...)
#define log_info(...)
#define log_error(...)
#define log_warn(...)
#endif

static void serial_set_baudrate(struct termios *opt, unsigned int baudrate) {
  cfsetispeed(opt, (speed_t)baudrate);
  cfsetospeed(opt, (speed_t)baudrate);
}

static void serial_set_stopbit(struct termios *opt, enum stopbit sb) {
  if (sb == STOPBIT_2) {
    opt->c_cflag |= CSTOPB;
  } else {
    opt->c_cflag &= ~CSTOPB;
  }
}

static void serial_set_databit(struct termios *opt, enum databit db) {
  opt->c_cflag &= ~CSIZE;
  switch (db) {
  case DATABIT_8:
    opt->c_cflag |= CS8;
    break;
  case DATABIT_7:
    opt->c_cflag |= CS7;
    break;
  case DATABIT_6:
    opt->c_cflag |= CS6;
    break;
  case DATABIT_5:
    opt->c_cflag |= CS5;
    break;
  default:
    opt->c_cflag |= CS8;
    break;
  }
}

static void serial_set_parity(struct termios *opt, enum parity p) {
  if (p == PARITY_NON) {
    opt->c_cflag &= ~PARENB;
    return;
  }

  opt->c_cflag |= PARENB;
  opt->c_cflag =
      (p == PARITY_EVEN) ? opt->c_cflag & ~PARODD : opt->c_cflag | PARODD;
}

bool serial_is_dev_hangup(int fd) {
  struct termios opt;

  return (tcgetattr(fd, &opt) == -1) && (errno == EIO);
}

int serial_set_attr(int fd, unsigned int baudrate, enum databit db,
                    enum stopbit sb, enum parity p, int vtime, int vmin) {

  struct termios opt;
  if (tcgetattr(fd, &opt) == -1) {
    log_error("can not get attributes of fd: %d", fd);
    return -1;
  }

  // notice that some values should go see
  // termios.h & serial.h to get the full message
  log_debug("request from fd: %d, "
            "set baudrate: %u, databit: %d, stopbit: %d, "
            "parity: %d, vtime: %.2fs, vmin: %d",
            fd, baudrate, db, sb, p, vtime / 10.0f, vmin);

  serial_set_baudrate(&opt, baudrate);
  opt.c_cflag |= CLOCAL | CREAD; /* | CRTSCTS */

  serial_set_databit(&opt, db);
  serial_set_parity(&opt, p);
  serial_set_stopbit(&opt, sb);

  // 不需要软件流控
  opt.c_iflag &= ~(IXON | IXOFF | IXANY);
  // 不需要规范模式，我们只要接收二进制流即可
  opt.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
  // 不处理数据
  opt.c_oflag &= ~OPOST;

  opt.c_cc[VTIME] = vtime;
  opt.c_cc[VMIN] = vmin;

  tcflush(fd, TCIFLUSH); // 丢掉已经接收到设备的数据
  // 设置属性并立即生效
  return (tcsetattr(fd, TCSANOW, &opt));
}

int serial_set_vtime_vmin(int fd, int vtime, int vmin) {
  struct termios opt;
  if (tcgetattr(fd, &opt) == -1) {
    log_error("can not get attributes of fd: %d", fd);
    return -1;
  }

  opt.c_cc[VTIME] = vtime;
  opt.c_cc[VMIN] = vmin;

  tcflush(fd, TCIFLUSH);
  return (tcsetattr(fd, TCSANOW, &opt));
}
