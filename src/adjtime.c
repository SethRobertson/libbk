/*
 * Dedicated to the public.
 *
 * Example usage: adjtime `/usr/sbin/ntpdate  -d timex.cs.columbia.edu 2>/dev/null | egrep ^offset | sed 's/\./ /' | awk '{print $2,$3}'`
 */

#include <libbk.h>

int main(int argc, char *argv[], char *envp[])
{
  struct timeval forward;
  struct timeval oldforward;

  if (argc != 3)
  {
    fprintf(stderr, "Usage: %s <seconds offset> <microseconds offset>\n", argv[0]);
    exit(1);
  }

  forward.tv_sec = atoi(argv[1]);
  forward.tv_usec = atoi(argv[2]);

  if (forward.tv_sec > 2000)
  {
    printf("Maximum adjustment is 2000 or so, resetting maximum from %ld\n", forward.tv_sec);
    forward.tv_sec = 2000;
    forward.tv_usec = 0;
  }
  if (forward.tv_sec < -2000)
  {
    printf("Minimum adjustment is -2000 or so, resetting minimum from %ld\n", forward.tv_sec);
    forward.tv_sec = -2000;
    forward.tv_usec = 0;
  }

  printf("Adjusting time by %ld.%06ld seconds\n", forward.tv_sec, forward.tv_usec);
  if (adjtime(&forward, &oldforward) < 0)
  {
    perror("adjtime() failure");
    exit(2);
  }

  printf("Old adjustment was %ld.%06ld seconds\n", oldforward.tv_sec, oldforward.tv_usec);
  exit(0);
}
