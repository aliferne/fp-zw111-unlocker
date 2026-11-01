#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static void display_usage() {
  printf("\n    Format: cmd lock start length [whence]\n\n");
  printf("      'cmd' is 'g' (GETLK), 's' (SETLK), or 'w' (SETLKW)\n");
  printf("      'lock' is 'r' (READ), 'w' (WRITE), or 'u' (UNLOCK)\n");
  printf("      'start' and 'length' specify byte range to lock\n");
  printf("      'whence' is 's' (SEEK_SET, default), 'c' (SEEK_CUR), or 'e' "
         "(SEEK_END)\n\n");
}

#define MAX_LINE 100

int main(int argc, char *argv[]) {
  int fd, numRead, cmd, status;
  char lock, cmdCh, whence, line[MAX_LINE];
  long long len, st;

  if (argc != 2 || strcmp(argv[1], "--help") == 0)
    errExit("%s usage error", argv[0]);

  fd = open(FILE_NAME, O_RDWR | O_CREAT);
  if (fd == -1)
    errExit("open (%s)", argv[1]);

  printf("Enter ? for help\n");

  for (;;) {
    printf("[PID=%ld]> ", (long)getpid());
    fflush(stdout);

    if (fgets(line, MAX_LINE, stdin) == NULL)
      exit(EXIT_SUCCESS);

    line[strlen(line) - 1] = '\0'; // rm '\n'

    if (*line == '\0')
      continue; // skip blank lines

    if (line[0] == '?') {
      display_usage();
      continue;
    }

    whence = 's';
    numRead =
        sscanf(line, "%c %c %lld %lld %c", &cmdCh, &lock, &st, &len, &whence);
    if (numRead < 4 || strchr("gsw", cmdCh) == NULL ||
        strchr("rwu", lock) == NULL || strchr("sce", whence) == NULL) {
      printf("Invalid command!\n");
      continue;
    }

    cmd = (cmdCh == 'g') ? F_GETLK : (cmdCh == 's') ? F_SETLK : F_SETLKW;

    struct flock fl;
    fl.l_type = (lock == 'r') ? F_RDLCK : (lock == 'w') ? F_WRLCK : F_UNLCK;
    fl.l_start = st;
    fl.l_whence = (whence == 's')   ? SEEK_SET
                  : (whence == 'c') ? SEEK_CUR
                                    : SEEK_END;
    fl.l_len = len;
    fl.l_pid = getpid();

    status = fcntl(fd, cmd, &fl);

    if (cmd == F_GETLK) { /* ... and see what happened */
      if (status == -1)
        errExit("fcntl - F_GETLK");
      else {
        if (fl.l_type == F_UNLCK)
          printf("[PID=%ld] Lock can be placed\n", (long)getpid());
        else /* Locked out by someone else */
          printf("[PID=%ld] Denied by %s lock on %lld:%lld "
                 "(held by PID %ld)\n",
                 (long)getpid(), (fl.l_type == F_RDLCK) ? "READ" : "WRITE",
                 (long long)fl.l_start, (long long)fl.l_len, (long)fl.l_pid);
      }
    } else { /* F_SETLK, F_SETLKW */
      if (status == 0)
        printf("[PID=%ld] %s\n", (long)getpid(),
               (lock == 'u') ? "unlocked" : "got lock");
      else if (errno == EAGAIN || errno == EACCES) /* F_SETLK */
        printf("[PID=%ld] failed (incompatible lock)\n", (long)getpid());
      else if (errno == EDEADLK) /* F_SETLKW */
        printf("[PID=%ld] failed (deadlock)\n", (long)getpid());
      else
        errExit("fcntl - F_SETLK(W)");
    }
  }

#if 0
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
#endif

  close(fd);

  exit(EXIT_SUCCESS);
}
