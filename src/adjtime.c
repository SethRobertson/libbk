/*
 * Dedicated to the public.
 *
 * Example usage:
 *
 * adjtime `ntpdate -d pool.ntp.org | sed -n '/^offset/s/offset\(.*\)\./\1 /p'`
 *
 * ntpdate -B is simpler as long as offset is within +/-2146s limit but this
 * program will apply maximum offset in that case, whereas ntpdate fails...
 */

#include <libbk.h>

#define MILLION 1000000

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


  if (forward.tv_sec > (INT_MAX / MILLION - 2))
  {
    printf("Limiting to maximum positive offset adjustment\n");
    forward.tv_sec = INT_MAX / MILLION - 2;
    forward.tv_usec = 999999;
  }
  else if (forward.tv_sec < (INT_MIN / MILLION + 2))
  {
    printf("Limiting to maximum negative offset adjustment\n");
    forward.tv_sec = INT_MIN / MILLION + 2;
    forward.tv_usec = -999999;
  }
  else if (forward.tv_sec < 0 && forward.tv_usec > 0)
  {
    forward.tv_usec = -forward.tv_usec;
  }

  printf("Adjusting time by %f seconds\n", forward.tv_sec + forward.tv_usec / (double) MILLION);

  if (adjtime(&forward, &oldforward) < 0)
  {
    perror("adjtime() failure");
    exit(2);
  }

  printf("Old adjustment was %f seconds\n", oldforward.tv_sec + oldforward.tv_usec / (double) MILLION);
  exit(0);
}
