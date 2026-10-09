#ifndef SERIAL_H
#define SERIAL_H

#include <termios.h>

enum stopbit { STOPBIT_1, STOPBIT_15, STOPBIT_2 };
enum databit { DATABIT_5, DATABIT_6, DATABIT_7, DATABIT_8 };
enum parity { PARITY_NON, PARITY_EVEN, PARITY_ODD };

int serial_set_attr(int fd, unsigned int baudrate, enum databit db,
                    enum stopbit sb, enum parity p, int vtime, int vmin);

#endif /* SERIAL_H */
