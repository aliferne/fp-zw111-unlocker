#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/file.h>

/*
 * ```bash
 * ./fd 1 >! log1.txt 2>&1 &
 * ./fd 2 >! log2.txt 2>&1 &
 * wait
 * ```
 *
 * 然后读取日志就能知道是否有 race condition
 * 只要 log1 出现 FD2 那就肯定 race condition 了
 */

#define FILENAME "test.txt"

int main(int argc, char *argv[]) {
  if (argc < 2)
    return 1;
  char *number = argv[1];
  char *str;
  if (strcmp(number, "1") == 0) {
    str = "-----THIS IS FD1-------";
  } else {
    str = "-----THIS IS FD2-------";
  }

  int fd = open(FILENAME, O_RDWR | O_CREAT , 0644);
  if (fd == -1)
    return 1;

  char buf[100];
  int nb = 0;

#ifdef USE_FLOCK
  flock(fd, LOCK_EX);
#endif
  for (int i = 0; i < 10; i++) {
    if ((nb = write(fd, str, strlen(str))) != -1)
      printf("write '%s' into %s (nbytes: %d)\n", str, FILENAME, nb);
    lseek(fd, 0, SEEK_SET);
    
    if ((nb = read(fd, buf, strlen(str))) != -1)
      printf("read %s into buf: %s (nbytes: %d, errno: %d)\n", FILENAME, buf,
             nb, errno);
    lseek(fd, 0, SEEK_SET);
    
    sleep(1);
  }
  
#ifdef USE_FLOCK
  flock(fd, LOCK_UN);
#endif

  close(fd);

  return 0;
}
