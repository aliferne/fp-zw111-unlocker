#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

/*
 * ```bash
 * ./fd-lock 1 >! log1-locked.txt 2>&1 &
 * ./fd-lock 2 >! log2-locked.txt 2>&1 &
 * wait
 * ```
 *
 * 会发现进程之间是互斥的，先跑完第一个再跑第二个
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

  int fd = open(FILENAME, O_RDWR | O_CREAT | O_TRUNC, 0644);
  if (fd == -1)
    return 1;

  char buf[100];
  int nb = 0;

  for (int i = 0; i < 10; i++) {
    if (flock(fd, LOCK_EX) == -1) {
      perror("flock");
      break;
    }

    if ((nb = write(fd, str, strlen(str))) != -1)
      printf("write '%s' into %s (nbytes: %d)\n", str, FILENAME, nb);
    sleep(1);

    lseek(fd, 0, SEEK_SET);
    if ((nb = read(fd, buf, strlen(str))) != -1)
      printf("read %s into buf: %s (nbytes: %d, errno: %d)\n", FILENAME, buf,
             nb, errno);
    sleep(1);

    flock(fd, LOCK_UN);
  }

  close(fd);

  return 0;
}
