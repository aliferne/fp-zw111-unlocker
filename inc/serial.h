#ifndef SERIAL_H
#define SERIAL_H

#include <stdbool.h>
#include <termios.h>

enum stopbit { STOPBIT_1, STOPBIT_15, STOPBIT_2 };
enum databit { DATABIT_5, DATABIT_6, DATABIT_7, DATABIT_8 };
enum parity { PARITY_NON, PARITY_EVEN, PARITY_ODD };

bool serial_is_dev_hangup(int fd);
/* return -1 if failed */
int serial_set_attr(int fd, unsigned int baudrate, enum databit db,
                    enum stopbit sb, enum parity p, int vtime, int vmin);

/* usually used to set vtime and vmin again, return -1 if failed */
int serial_set_vtime_vmin(int fd, int vtime, int vmin);

#endif /* SERIAL_H */
