#ifndef __PUB_H
#define __PUB_H

#define LEN(buf) (sizeof((buf)) / sizeof((buf)[0]))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLAMP(x, min, max) MIN((max), MAX((x), (min)))

#endif
