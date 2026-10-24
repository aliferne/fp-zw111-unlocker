#include "serial.h"

#ifdef SERIAL_DEBUG_ON
#include "log.h"
#define SERIAL_LOG(...) log_debug(__VA_ARGS__)
#else
#define SERIAL_LOG(...)
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
      (p == PARITY_EVEN) ? opt->c_cflag & ~PARODD : opt->c_cflag | ~PARODD;
}

int serial_set_attr(int fd, unsigned int baudrate, enum databit db,
                    enum stopbit sb, enum parity p, int vtime, int vmin) {
  struct termios opt;

  tcgetattr(fd, &opt);

  // notice that some values should go see
  // termios.h & serial.h to get the full message
  SERIAL_LOG("request from fd: %d, "
             "set baudrate: %u, databit: %d, stopbit: %d, "
             "parity: %d, vtime: %d, vmin: %d",
             fd, baudrate, db, sb, p, vtime, vmin);

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
