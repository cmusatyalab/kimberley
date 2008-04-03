#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

int
main(int argc, char *argv[]) {
  int err;
  struct timeval tv;
  struct tm *tm;
  char time_str[200];
  long ms;

  memset(&tv, 0, sizeof(struct timeval));
  gettimeofday(&tv, NULL);

  tm = localtime(&tv.tv_sec);
  

  /*
   * Format the date and time, down to a single second. 
   */

  strftime(time_str, 200, "%Y-%m-%d_%H:%M:%S", tm);
  printf("%s.%.6u: ", time_str, tv.tv_usec);

  if(argc == 2)
    printf("%s\n", argv[1]);
  else if(argc > 2)
    printf("too many args\n");
  else
    printf("no message\n");

  exit(EXIT_SUCCESS);
}
