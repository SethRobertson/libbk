#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: testrandidea.c,v 1.1 2001/11/27 00:57:22 seth Exp $";
static char libbk__copyright[] = "Copyright (c) 2001";
static char libbk__contact[] = "<projectbaka@baka.org>";
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
 * Example libbk user with main() prototype.
 */
#include <libbk.h>


#define ERRORQUEUE_DEPTH 32			///< Default depth


volatile int exiter = 0;


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
  bk_flags 		pc_flags;		///< Flags are fun!
#define PC_VERBOSE	1			///< Verbose output
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
  BK_ENTRY(B, __FUNCTION__, __FILE__, "SIMPLE");
  char c;
  int getopterr=0;
  extern char *optarg;
  extern int optind;
  struct program_config Pconfig, *pconfig=NULL;
  poptContext optCon=NULL;
  const struct poptOption optionsTable[] = 
  {
    {"debug", 'd', POPT_ARG_NONE, NULL, 'd', "Turn on debugging", NULL },
    {"test", 'T', POPT_ARG_INT, NULL, 'T', "Set the generator test type", "test" },
    {"time", 't', POPT_ARG_INT, NULL, 't', "Set the time for the test to run", "test" },
    {"loops", 'l', POPT_ARG_INT, NULL, 'l', "Set the number of loops it runs", "test" },
    {"verbose", 'v', POPT_ARG_NONE, NULL, 'v', "Turn on verbose message", NULL },
    POPT_AUTOHELP
    POPT_TABLEEND
  };

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD("BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, LOG_LOCAL0, 0)))
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

      gettimeofday(&end,NULL);
      end.tv_usec += pconfig->pc_numusec;
      BK_TV_RECTIFY(&end);

      for (cntr=0;;cntr++)
      {
	gettimeofday(&cur, NULL);
	if (BK_TV_CMP(&end,&cur) < 0)
	  break;
      }
      printf("cntr %d\n",cntr);
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
      printf("cntr %d\n",cntr);
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
      printf("cntr %d\n",cntr);
    }
    
  }

  bk_exit(B,0);
  abort();
  return(255);
}
