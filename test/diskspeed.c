/*
 * Find the speed of a disk (through the filesystem)
 *
 * Seth Robertson, <seth@ctr.columbia.edu>
 * Copyright (c) 1993 Seth Robertson
 * All rights reserved
 *
 * <TODO>This should be updated to use autoconf settings</TODO>
 *
 * gcc -O -o diskspeed diskspeed.c
 *
 * Symbols which I define below:
 *
 * -DHRTIME	(if the OS has gethrtime())
 * -DTIMEOFDAY	(if we have BSDish gettimeofday())
 * -DRUSAGE	(if we have BSDish getrusage())
 * -DTIMES	(if we have SYSVish times())
 * -DTIME	(if we have SYSVish time() -- XXX only second resolution)
 *
 * If you wish to override my defaults provide below, use:
 * -DOVERRIDE
*/

#if !defined(OVERRIDE)

#undef HRTIME
#undef TIMEOFDAY
#undef RUSAGE
#undef TIMES
#undef TIME

#if defined(__sgi__)		/* Could this be Irix 5.1 only? */
#define TIMEOFDAY
#define TIMES
#elif defined(__sun__) && defined(__svr4__) /* Solaris 2.x */
#define HRTIME
#define TIMES
#elif defined(__sun__)		/* SunOS 4.x */
#define TIMEOFDAY
#define RUSAGE
#elif defined(__svr4__)		/* Generic SYSV box */
#define TIME
#define TIMES
#else  				/* Generic BSD box */
#define TIMEOFDAY
#define RUSAGE
#endif

#endif /*OVERRIDE*/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>

#if defined(TIMES)
#include <sys/times.h>
#include <limits.h>
#endif

#if defined(RUSAGE)
#include <sys/resource.h>
#endif

struct timer_info
{
#if defined(HRTIME)
  hrtime_t		real;	/* Nanoseconds */
#elif defined(TIMEOFDAY)
  struct timeval	real;	/* Sec and ms */
#else
  time_t		real;	/* Seconds (YUK) */
#endif
#if defined(TIMES)
  struct tms		systime; /* System time */
#else
  struct rusage		systime; /* System time */
#endif
} start, stop, difft, rsum, wsum;

void prep_timer(void);
void read_timer(struct timer_info *diffT);
void print_timer(int style, unsigned long long bytes, struct timer_info *diffT);
void zero_timer(struct timer_info *timer);
void add_timer(struct timer_info *sum, struct timer_info *x, struct timer_info *y);
void sub_timer(struct timer_info *diffT, struct timer_info *x, struct timer_info *y);
void timeval_add(struct timeval *sum, struct timeval *y, struct timeval *x);
void timeval_sub(struct timeval *sum, struct timeval *y, struct timeval *x);

/* Style bits */
#define ONELINE 0
#define MULTILINE 2
#define READ 0
#define WRITE 1

char *prog;

int main(int argc,char *argv[])
{
  long iochars = 0;		/* Number of chars i/oed during last i/o operation */
  unsigned long bytes = 0;		/* Number of chars i/oed to date per test */
  unsigned long buffersize = 8192;	/* Size of i/o buffer in chars */
  unsigned long numbuffer = 512;	/* Number of buffers to write per test */
  unsigned long reps = 4;		/* Number of times to repeat test */
  unsigned long rcntr = 0;		/* Current rep count */
  unsigned long bcntr = 0;		/* Current buffer count */
  unsigned long long total_r_chars = 0; /* Total number of characters read */
  unsigned long long total_w_chars = 0; /* Total number of characters written */
  char *filename = "diskspeed.footest";	/* Output file name */
  int warnings=1;			/* Show warning messages */
  int paused=0;				/* Paused before reading or writing */
  int fd;				/* File descriptor or test file */
  char *buffer;				/* Test buffer */
  int c;
  int errflg=0;

  extern char *optarg;
  extern int optind, opterr;

  prog = argv[0];


  /*
   * Parse command line arguments
   *
   */

  while ((c = getopt(argc,argv,"wpf:b:n:r:")) != -1)
    switch(c)
      {
      case 'p':
	paused=!paused;
	break;
      case 'w':
	warnings=!warnings;
	break;
      case 'b':
	buffersize = atoi(optarg);
	break;
      case 'f':
	filename = optarg;
	break;;
      case 'n':
	numbuffer = atoi(optarg);
	break;
      case 'r':
	reps = atoi(optarg);
	break;
      case '?':
	errflg++;
	break;
      }

  if (buffersize == 0)
    {
      fputs("Error: buffer size must be positive\n",stderr);
      errflg++;
    }

  if (numbuffer == 0)
    {
      fputs("Error: number of buffers must be positive\n",stderr);
      errflg++;
    }

  if (reps == 0)
    {
      fputs("Error: number of repetitions must be positive\n",stderr);
      errflg++;
    }

  if ((fd = open(filename,O_RDWR|O_CREAT|O_TRUNC,0666)) < 0)
    {
      perror("open");
      fprintf(stderr,"Cannot open file %s\n",argv[optind]);
      errflg++;
    }
      
  if (errflg)
    {
      fprintf(stderr,"Usage: %s [-w] [-p] [-f filename] [-b buffer size] [-n num buffer/test] [-r num tests ]\n",argv[0]);
      exit(2);
    }


  /*
   * Initialization
   *
   */

  if ((buffer = malloc(buffersize)) == NULL)
    {
      perror("malloc");
      exit(4);
    }

  for(bcntr=0;bcntr<buffersize;bcntr++)
    {
      *(buffer+bcntr) = (char)(random()>>8);
    }


  /*
   * Main outer loop.
   *
   * This loop executes once for every scheduled repetition.
   * (or once for every write cycle and read cycle)
   *
   */

  zero_timer(&wsum);
  zero_timer(&rsum);
  for(rcntr=reps;rcntr>0;rcntr--)
    {

      /*
       * Write test
       *
       * Write numbuffer buffers to disk
       *
       */

      if (lseek(fd,0,SEEK_SET))	/* Seek to the beginning of file */
	{
	  perror("lseek");
	  exit(5);
	}

      if (ftruncate(fd,0))	/* Truncate output file if necessary */
	{
	  perror("ftruncate");
	  exit(5);
	}
      
      bytes = 0;

      /**************************************************/
      prep_timer();		/* Init timers */
      for (bcntr = numbuffer; bcntr > 0; bcntr--)
	{
	  if ((iochars = write(fd, buffer, buffersize)) < 0)
	    {
	      perror("write");
	      exit(5);
	    }

	  if (warnings && (u_long)iochars != buffersize)
	    fprintf(stderr,"\nWarning:  requested (%lu) != actual (%ld)\n",
		    buffersize,iochars);

	  bytes += iochars;
	}
      fsync(fd);		/* Don't leave any write pending */
      read_timer(&difft);
      /**************************************************/

      print_timer(ONELINE|WRITE,(unsigned long long)bytes, &difft);
      total_w_chars += bytes;
      add_timer(&wsum, &wsum, &difft);

      /*
       * Write test
       *
       * Read numbuffer buffers from disk
       *
       */

      if (lseek(fd,0,SEEK_SET))
	{
	  perror("lseek");
	  exit(5);
	}

      bytes = 0;

      /**************************************************/
      prep_timer();
      for (bcntr = numbuffer; bcntr > 0; bcntr--)
	{
	  if ((iochars = read(fd, buffer, buffersize)) < 0)
	    {
	      perror("write");
	      exit(5);
	    }

	  if (warnings && (u_long)iochars != buffersize)
	    fprintf(stderr,"\nWarning:  requested (%lu) != actual (%ld)\n",
		    buffersize,iochars);

	  bytes += iochars;
	}
      read_timer(&difft);
      /**************************************************/

      print_timer(ONELINE|READ,(unsigned long long)bytes, &difft);
      total_r_chars += bytes;
      add_timer(&rsum, &rsum, &difft);
    }

  print_timer(MULTILINE|WRITE,total_w_chars,&wsum);
  print_timer(MULTILINE|READ,total_r_chars,&rsum);
  unlink(filename);
  return(0);
}

/*
 * Timing routines
*/

/*
 *			P R E P _ T I M E R
 */
void
prep_timer()
{
  
#if defined(HRTIME)
  start.real = gethrtime();
#elif defined(TIMEOFDAY)
  gettimeofday(&(start.real),NULL);
#else
  start.real = time(NULL);
#endif

#if defined(TIMES)
  (void)times(&(start.systime));
#else
  (void)getrusage(RUSAGE_SELF, &(start.systime)); 
#endif
}

/*
 *			R E A D _ T I M E R
 * 
 */
void
read_timer(struct timer_info *diffT)
{

#if defined(HRTIME)
  stop.real = gethrtime();
#elif defined(TIMEOFDAY)
  gettimeofday(&(stop.real),NULL);
#else
  stop.real = time(NULL);
#endif

#if defined(TIMES)
  (void)times(&(stop.systime));
#else
  (void)getrusage(RUSAGE_SELF, &(stop.systime)); 
#endif

  sub_timer(diffT,&stop,&start);
  return;
}

/*
 * Zero timer
*/
void
zero_timer(struct timer_info *timer)
{
#if defined(HRTIME) || defined(TIME)
  timer->real = 0;
#else
  timer->real.tv_sec = 0;
  timer->real.tv_usec = 0;
#endif

#if defined(TIMES)
  timer->systime.tms_stime = 0;
#else
  timer->systime.ru_stime.tv_sec = 0;
  timer->systime.ru_stime.tv_usec = 0;
#endif
}  

/*
 * Add two timers together
*/
void
add_timer(struct timer_info *sum, struct timer_info *x, struct timer_info *y)
{
#if defined(HRTIME) || defined(TIME)
  sum->real = x->real + y->real;
#else
  timeval_add(&(sum->real),&(x->real),&(y->real));
#endif

#if defined(TIMES)
  sum->systime.tms_stime = x->systime.tms_stime + y->systime.tms_stime;
#else
  timeval_add(&(sum->systime.ru_stime),&(x->systime.ru_stime),&(y->systime.ru_stime));
#endif
}


/*
 * Subtract two timers
*/
void
sub_timer(struct timer_info *sum, struct timer_info *x, struct timer_info *y)
{
#if defined(HRTIME) || defined(TIME)
  sum->real = x->real - y->real;
#else
  timeval_sub(&(sum->real),&(x->real),&(y->real));
#endif

#if defined(TIMES)
  sum->systime.tms_stime = x->systime.tms_stime - y->systime.tms_stime;
#else
  timeval_sub(&(sum->systime.ru_stime),&(x->systime.ru_stime),&(y->systime.ru_stime));
#endif
}

/*
 * Add two timevals together
*/
void timeval_add(struct timeval *sum, struct timeval *x, struct timeval *y)
{
  sum->tv_sec = x->tv_sec + y->tv_sec;
  sum->tv_usec = x->tv_usec + y->tv_usec;
  if (sum->tv_usec > 1000000)
    {
      sum->tv_sec += sum->tv_usec / 1000000;
      sum->tv_usec = sum->tv_usec % 1000000;
    }
  return;
}

/*
 * Subtract two timevals
*/
void timeval_sub(struct timeval *sum, struct timeval *x, struct timeval *y)
{
  sum->tv_sec = x->tv_sec - y->tv_sec;
  sum->tv_usec = x->tv_usec - y->tv_usec;
  if (sum->tv_usec < 0)
    {
      sum->tv_sec--;
      sum->tv_usec += 1000000;
    }
  return;
}


void print_timer(int style, unsigned long long bytes, struct timer_info *diffT)
{
  double rs;			/* real second */
  double ss;			/* system second */
  double bprs;			/* Bytes per real second */
  double bpss;			/* Bytes per system second */
  char *io[2] = { "(r)", "(w)" };
  char *fio[2] = { "read", "written" };

#if defined(HRTIME)
  rs = (double)diffT->real / 1000000000.0;
#elif defined(TIME)
  rs = (double)diffT->real;
#else
  rs = ((double)diffT->real.tv_sec + (double)diffT->real.tv_usec / 1000000.0);
#endif
  bprs = (double)bytes / rs;

#if defined(TIMES)
  ss = (double)diffT->systime.tms_stime;
#else
  ss = ((double)diffT->systime.ru_stime.tv_sec + (double)diffT->systime.ru_stime.tv_usec / 1000000.0);
#endif
  bpss = (double)bytes / ss;

  if (style & MULTILINE)
    {
      fputs("\n\n",stderr);

      fprintf(stderr,"%s: %9lg bytes %s\n", prog, (double)bytes, fio[style & WRITE] );
      fprintf(stderr,"%9.3lf  CPU sec  = %9lg KB/ cpu sec,  %9lg Kbits/ cpu sec\n",
	       ss, bpss / 1024.0, bpss * 8.0 / 1024.0);
      fprintf(stderr,"%9.3lf real sec  = %9lg KB/real sec,  %9lg Kbits/real sec\n",
	       rs, bprs / 1024.0, bprs * 8.0 / 1024.0);
    }
  else
    {
      fprintf(stderr,"%s %s: %9.0lf bytes in %9.3lf seconds: %9.2lf KB/sec\n",
	      prog,io[style & WRITE], (double)bytes, rs, bprs / 1024.0);
    }
}
