#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
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

int setLock(int fd, int cmd, short ltype, short start, short whence,
            short len) {
  struct flock fl;
  fl.l_type = ltype;
  fl.l_start = start;
  fl.l_whence = whence;
  fl.l_len = len;
  fl.l_pid = getpid();

  return fcntl(fd, cmd, &fl);
}

#define SET_RDLOCK(fd, is_blocking, start, whence, len)                        \
  setLock((fd), (is_blocking) ? F_SETLKW : F_SETLK, F_RDLCK, (start),          \
          (whence), (len))
#define SET_WRLOCK(fd, is_blocking, start, whence, len)                        \
  setLock((fd), (is_blocking) ? F_SETLKW : F_SETLK, F_WRLCK, (start),          \
          (whence), (len))
#define UNLOCK(fd, start, whence, len)                                         \
  setLock((fd), F_SETLKW, F_UNLCK, (start), (whence), (len))

int main(int argc, char *argv[]) {
  int fd;
  bool is_blocking = true;

#ifdef NONBLOCK
  is_blocking = false;
#endif

  fd = open(FILE_NAME, O_RDWR | O_CREAT);
  if (fd == -1)
    errExit("open");

  /* 下面的代码似乎是有前后顺序之分的：
   *  - 如果 RDLOCK 在前， WRLOCK 在后，那么
   *    因为 RD 不会修改数据，因此 syscall 会允许两个进程同时持有锁
   *    然后到了 WR 之后再分前后
   *  - 如果 WRLOCK 在前， RDLOCK 在后
   *    那么此时整个锁就直接给先启动的进程锁上了
   *    等这个进程退出来之后才轮到下一个进程
   */

  if (SET_RDLOCK(fd, is_blocking, 0, SEEK_SET, 0) == -1) {
    errExit("fcntl (PID: %ld)", (long)getpid());
  }

  printf("PID %ld: granted %s\n", (long)getpid(), FILE_NAME);
  sleep((argc >= 2) ? atoi(argv[1]) : 5);
  printf("PID %ld: releasing %s\n", (long)getpid(), FILE_NAME);

  if (UNLOCK(fd, 0, SEEK_SET, 0) == -1)
    errExit("unlock (PID: %ld)", (long)getpid());
  
  printf("--------------Now Write Lock--------------\n");
  
  if (SET_WRLOCK(fd, is_blocking, 0, SEEK_SET, 0) == -1) {
    errExit("fcntl (PID: %ld)", (long)getpid());
  }

  printf("PID %ld: granted %s\n", (long)getpid(), FILE_NAME);
  sleep((argc >= 2) ? atoi(argv[1]) : 5);
  printf("PID %ld: releasing %s\n", (long)getpid(), FILE_NAME);

  if (UNLOCK(fd, 0, SEEK_SET, 0) == -1)
    errExit("unlock (PID: %ld)", (long)getpid());
  

  exit(EXIT_SUCCESS);
}
