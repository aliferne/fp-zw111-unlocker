#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <unistd.h>

// #define FILE_NAME "flock_test.txt"
#define FILE_NAME "/dev/null"
#define errExit(msg, ...)                                                      \
  do {                                                                         \
    fprintf(stderr, msg "\n", ##__VA_ARGS__);                                  \
    exit(EXIT_FAILURE);                                                        \
  } while (0)

int main(int argc, char *argv[]) {
  int fd;
  int lock = LOCK_EX;

#ifdef USE_SHARED_LOCK
  lock = LOCK_SH;
#endif

#ifdef NONBLOCK
  lock |= LOCK_NB;
#endif

  fd = open(FILE_NAME, O_RDONLY | O_CREAT);
  if (fd == -1)
    errExit("open");

  if (flock(fd, lock) == -1) {
    if (errno == EWOULDBLOCK)
      errExit("PID: %ld locked, bye!", (long)getpid());
    else
      errExit("flock (PID: %ld)", (long)getpid());
  }

  printf("PID %ld: granted %s\n", (long)getpid(), FILE_NAME);
  sleep((argc >= 2) ? atoi(argv[1]) : 5);
  printf("PID %ld: releasing %s\n", (long)getpid(), FILE_NAME);

  if (flock(fd, LOCK_UN) == -1)
    errExit("flock_unlock (PID: %ld)", (long)getpid());

  exit(EXIT_SUCCESS);
}
