#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: testrandidea.c,v 1.9 2003/04/16 23:39:54 seth Exp $";
static const char libbk__copyright[] = "Copyright (c) 2001";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright LIBBK++
 *
 * Copyright (c) 2001 The Authors.  All rights reserved.
 *
 * This source code is licensed to you under the terms of the file
 * LICENSE.TXT in this release for further details.
 *
 * Mail <projectbaka@baka.org> for further information
 *
 * --Copyright LIBBK--
 */

/**
 * @file
 *
 * Program to test various methods of implementing a truerand random
 * number generator similar to what AT&T Bell Labs conceived of.  The
 * purpose is to use OS scheduling jitter to generate random numbers.
 *
 * We implemented this concept in three ways.  First was to use
 * pure system calls, second to use the walltime alarm timer, third
 * was to use the virtualtime alarm timer.
 *
 * The alarm timers generated quite good random numbers, with
 * approximately 6 bits per byte of randomness.  However, they
 * were slower at 3 ms per random number--this exact rate depends
 * on the clock frequency--generally 4 bytes of output per hertz.
 *
 * The system call timer generated randomness in proportion to the
 * amount of time you wanted to spend.  The more time, the more
 * entropy per byte you got, with essentially a linear scale during the
 * normal range.  However, this does not mean that it is effective to
 * spend more time on each number.  Further study:
 *
 * for l in 16 32 64 128 512 1024;
 *  do
 *   for t in 10 100 1000 10000 100000 1000000;
 *    do
 *     (
 *      echo -n $l $t :
 *	time ../testrandidea -b -l $l -T 0 -t $t | ent
 *     ) 2>&1
 *    done;
 *  done >/tmp/randinfo
 *
 * egrep 'elapsed|Entropy' /tmp/randinfo | perl5 -ne 'if (/^(\d+) (\d+).*0:([0-9.]+)elapsed/) { $l = $1; $T = $2; $b = $1 * 4; $t = $3; } if (/([0-9.]+) bits per byte/) { $out = $b * $1; $r = $out/$t; print "$r $t $out $b $l $T\n"; }' | sort -n
 *
 * Reveiled that, on my linux laptop, the entropy measurement program
 * cannot be trusted to accurately measure low-entropy data :-)
 *
 * In any case, I decided that the magic parameter was "10000" usec
 * for the timer, and it produces approximately 4 bits per byte of
 * entropy (mostly due to the high order bytes being zero).
 *
 * A revised version of this test, which includes the time of day in
 * the random number mix, proved to be even more effective, producing
 * thousands of high-quality random numbers per cycle.
 *
 * The alarm calls produced approximately the same rate, but
 * due to the additional implementation complexity (e.g.
 * save and restoral in a OS portable way of the signal handler)
 * was not chosen for the actual random number generator.
 */
#include <libbk.h>


#define ERRORQUEUE_DEPTH 32			///< Default depth


volatile int exiter = 0;
void case1(int sig);


void case1(int sig)
{
  exiter++;
}



/**
 * Information of international importance to everyone
 * which cannot be passed around.
 */
struct global_structure
{
} Global;



/**
 * Information about basic program runtime configuration
 * which must be passed around.
 */
struct program_config
{
  u_int32_t		pc_testnum;		///< Generator type
  u_int32_t		pc_numusec;		///< Number of seconds
  u_int32_t		pc_loops;		///< Number of loops to generate
  bk_flags		pc_flags;		///< Flags are fun!
#define PC_VERBOSE	0x1			///< Verbose output
#define PC_BINARY	0x2			///< Binary output
};



/**
 * Program entry point
 *
 *	@param argc Number of argv elements
 *	@param argv Program name and arguments
 *	@param envp Program environment
 *	@return <i>0</i> Success
 *	@return <br><i>254</i> Initialization failed
 */
int
main(int argc, char **argv, char **envp)
{
  bk_s B = NULL;				/* Baka general structure */
  BK_ENTRY_MAIN(B, __FUNCTION__, __FILE__, "SIMPLE");
  char c;
  int getopterr=0;
  extern char *optarg;
  extern int optind;
  struct program_config Pconfig, *pconfig=NULL;
  poptContext optCon=NULL;
  const struct poptOption optionsTable[] =
  {
    {"debug", 'd', POPT_ARG_NONE, NULL, 'd', "Turn on debugging", NULL },
    {"binary", 'b', POPT_ARG_NONE, NULL, 'b', "Binary output", NULL },
    {"test", 'T', POPT_ARG_INT, NULL, 'T', "Set the generator test type", "test" },
    {"time", 't', POPT_ARG_INT, NULL, 't', "Set the time for the test to run", "test" },
    {"loops", 'l', POPT_ARG_INT, NULL, 'l', "Set the number of loops it runs", "test" },
    {"verbose", 'v', POPT_ARG_NONE, NULL, 'v', "Turn on verbose message", NULL },
    POPT_AUTOHELP
    POPT_TABLEEND
  };

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD(B, "BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, LOG_USER, 0)))
  {
    fprintf(stderr,"Could not perform basic initialization\n");
    exit(254);
  }
  bk_fun_reentry(B);

  pconfig = &Pconfig;
  memset(pconfig,0,sizeof(*pconfig));
  pconfig->pc_loops = 10;

  if (!(optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not initialize options processing\n");
    bk_exit(B,254);
  }

  while ((c = poptGetNextOpt(optCon)) >= 0)
  {
    switch (c)
    {
    case 'd':
      bk_error_config(B, BK_GENERAL_ERROR(B), 0, stderr, 0, BK_ERR_DEBUG, BK_ERROR_CONFIG_FH);	// Enable output of all error logs
      bk_general_debug_config(B, stderr, BK_ERR_NONE, 0);					// Set up debugging, from config file
      bk_debug_printf(B, "Debugging on\n");
      break;
    case 'b':
      BK_FLAG_SET(pconfig->pc_flags, PC_BINARY);
      break;
    case 'T':
      pconfig->pc_testnum = atoi(poptGetOptArg(optCon));
      break;
    case 't':
      pconfig->pc_numusec = atoi(poptGetOptArg(optCon));
      break;
    case 'l':
      pconfig->pc_loops = atoi(poptGetOptArg(optCon));
      break;
    case 'v':
      BK_FLAG_SET(pconfig->pc_flags, PC_VERBOSE);
      bk_error_config(B, BK_GENERAL_ERROR(B), ERRORQUEUE_DEPTH, stderr, BK_ERR_NONE, BK_ERR_ERR, 0);
      break;
    default:
      getopterr++;
      break;
    }
  }

  if (c < -1 || getopterr)
  {
    if (c < -1)
    {
      fprintf(stderr, "%s\n", poptStrerror(c));
    }
    poptPrintUsage(optCon, stderr, 0);
    bk_exit(B,254);
  }

  switch (pconfig->pc_testnum)
  {
  case 0:					// gettimeofday usec counter
    for (;pconfig->pc_loops;pconfig->pc_loops--)
    {
      struct timeval end, cur;
      volatile u_int cntr;
      static int last=0,diff;

      gettimeofday(&end,NULL);
      end.tv_usec += pconfig->pc_numusec;
      BK_TV_RECTIFY(&end);

      for (cntr=0;;cntr++)
      {
	gettimeofday(&cur, NULL);
	if (BK_TV_CMP(&end,&cur) < 0)
	  break;
      }
      diff = last-cntr;
      last = cntr;
      if (BK_FLAG_ISSET(pconfig->pc_flags, PC_BINARY))
	printf("%c%c%c%c",((char *)&diff)[0],((char *)&diff)[1],((char *)&diff)[2],((char *)&diff)[3]);
      else
	printf("cntr %u diff %d\n",cntr,diff);
    }
    break;

  case 1:
    signal(SIGALRM, case1);
    for (;pconfig->pc_loops;pconfig->pc_loops--)
    {
      volatile u_int cntr;
      struct itimerval itv;

      itv.it_value.tv_sec = itv.it_value.tv_usec = 0;
      itv.it_interval.tv_sec = itv.it_interval.tv_usec = 0;
      itv.it_value.tv_usec += pconfig->pc_numusec;
      BK_TV_RECTIFY(&itv.it_value);

      setitimer(ITIMER_REAL, &itv, NULL);
      for (exiter=0,cntr=0;!exiter;cntr++) ;
      if (BK_FLAG_ISSET(pconfig->pc_flags, PC_BINARY))
	printf("%c%c%c%c",((char *)&cntr)[0],((char *)&cntr)[1],((char *)&cntr)[2],((char *)&cntr)[3]);
      else
	printf("cntr %u\n",cntr);
    }

  case 2:
    signal(SIGVTALRM, case1);
    for (;pconfig->pc_loops;pconfig->pc_loops--)
    {
      volatile u_int cntr;
      struct itimerval itv;

      itv.it_value.tv_sec = itv.it_value.tv_usec = 0;
      itv.it_interval.tv_sec = itv.it_interval.tv_usec = 0;
      itv.it_value.tv_usec += pconfig->pc_numusec;
      BK_TV_RECTIFY(&itv.it_value);

      setitimer(ITIMER_VIRTUAL, &itv, NULL);
      for (exiter=0,cntr=0;!exiter;cntr++) ;
      if (BK_FLAG_ISSET(pconfig->pc_flags, PC_BINARY))
	printf("%c%c%c%c",((char *)&cntr)[0],((char *)&cntr)[1],((char *)&cntr)[2],((char *)&cntr)[3]);
      else
	printf("cntr %u\n",cntr);
    }

  case 3:					// gettimeofday usec counter
    for (;pconfig->pc_loops;pconfig->pc_loops--)
    {
      struct timeval end, last, cur, diff;
      volatile u_int cntr;

      gettimeofday(&end,NULL);
      last = end;
      end.tv_usec += pconfig->pc_numusec;
      BK_TV_RECTIFY(&end);

      for (cntr=0;;cntr++)
      {
	gettimeofday(&cur, NULL);
	if (BK_TV_CMP(&end,&cur) < 0)
	  break;
	BK_TV_SUB(&diff, &cur, &last);
	if (BK_FLAG_ISSET(pconfig->pc_flags, PC_BINARY))
	  printf("%c%c%c%c",((char *)&diff.tv_usec)[0],((char *)&diff.tv_usec)[1],((char *)&diff.tv_usec)[2],((char *)&diff.tv_usec)[3]);
	else
	  printf("diff %ld\n",diff.tv_usec);
	last=cur;
      }
    }
    break;

  }

  bk_exit(B,0);
  abort();
  return(255);
}
