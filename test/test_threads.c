#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: test_threads.c,v 1.1 2003/05/15 03:15:47 seth Exp $";
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
 * This file implements both ends of a bidirectional network pipe.
 *
 * Note: UDP support generally only works in transmit mode.  To
 * receive you must "transmit" on both sides with inverse IP and port
 * combinations.  Multicast and broadcast support is transmit only.
 * (Stupidities of connected UDP).
 */

#include <libbk.h>



#define ERRORQUEUE_DEPTH	32		///< Default depth



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
  struct bk_run	       *pc_run;			///< BAKA run environment
  bk_flags		pc_flags;		///< Everyone needs flags.
#define PC_THREADREADY			0x01	///< Want threads
#define PC_VERBOSE			0x02	///< Verbose output
  u_int			pc_cntr;		///< How many times
  u_int			pc_seconds;		///< Sleep time
};



static int proginit(bk_s B, struct program_config *pconfig);
void eventq(bk_s B, struct bk_run *run, void *opaque, const struct timeval *startide, bk_flags flags);



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
  BK_ENTRY_MAIN(B, __FUNCTION__, __FILE__, "bttcp");
  int c;
  int getopterr=0;
  extern char *optarg;
  extern int optind;
  struct program_config Pconfig, *pc=NULL;
  poptContext optCon=NULL;
  const struct poptOption optionsTable[] =
  {
    {"debug", 'd', POPT_ARG_NONE, NULL, 'd', "Turn on debugging", NULL },
    {"verbose", 'v', POPT_ARG_NONE, NULL, 'v', "Turn on verbose message", NULL },
    {"no-seatbelts", 0, POPT_ARG_NONE, NULL, 0x1000, "Sealtbelts off & speed up", NULL },
    {"sleep", 's', POPT_ARG_INT, NULL, 's', "Set the sleep timeout", "mseconds" },
    {"thread", 't', POPT_ARG_NONE, NULL, 't', "Thread ready", NULL },
    POPT_AUTOHELP
    POPT_TABLEEND
  };

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD(NULL, "BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, LOG_LOCAL0, BK_GENERAL_THREADREADY)))
  {
    fprintf(stderr,"Could not perform basic initialization\n");
    exit(254);
  }
  bk_fun_reentry(B);

  pc = &Pconfig;
  memset(pc,0,sizeof(*pc));
  pc->pc_seconds = 2000;

  if (!(optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not initialize options processing\n");
    bk_exit(B,254);
  }

  while ((c = poptGetNextOpt(optCon)) >= 0)
  {
    switch (c)
    {
    case 'd':					// debug
      bk_error_config(B, BK_GENERAL_ERROR(B), 0, stderr, 0, 0, BK_ERROR_CONFIG_FH);	// Enable output of all error logs
      bk_general_debug_config(B, stderr, BK_ERR_NONE, 0);				// Set up debugging, from config file
      bk_debug_printf(B, "Debugging on\n");
      break;
    case 'v':					// verbose
      BK_FLAG_SET(pc->pc_flags, PC_VERBOSE);
      bk_error_config(B, BK_GENERAL_ERROR(B), ERRORQUEUE_DEPTH, stderr, BK_ERR_NONE, BK_ERR_ERR, 0);
      break;
    case 0x1000:				// no-seatbelts
      BK_FLAG_CLEAR(BK_GENERAL_FLAGS(B), BK_BGFLAGS_FUNON);
      break;
    default:
      getopterr++;
      break;

    case 't':					// local-name
      BK_FLAG_SET(pc->pc_flags, PC_THREADREADY);
      break;

    case 's':					// local-name
      pc->pc_seconds=atoi(poptGetOptArg(optCon));
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
    bk_exit(B, 254);
  }

  if (proginit(B, pc) < 0)
  {
    bk_die(B, 254, stderr, "Could not perform program initialization\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  }

  if (bk_run_run(B,pc->pc_run, 0)<0)
  {
    bk_die(B, 1, stderr, "Failure during run_run\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  }

  bk_exit(B, 0);
  return(255);
}



/**
 * General program initialization
 *
 *	@param B BAKA Thread/Global configuration
 *	@param pc Program configuration
 *	@return <i>0</i> Success
 *	@return <br><i>-1</i> Total terminal failure
 */
static int
proginit(bk_s B, struct program_config *pc)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"bttcp");

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_RETURN(B, -1);
  }

  if (!(pc->pc_run = bk_run_init(B, 0)))
  {
    fprintf(stderr,"Could not create run structure\n");
    goto error;
  }

  bk_run_enqueue_cron(B, pc->pc_run, 1000, eventq, pc, NULL, BK_FLAG_ISSET(pc->pc_flags, PC_THREADREADY)?BK_RUN_THREADREADY:0);
  bk_run_enqueue_cron(B, pc->pc_run, 1000, eventq, pc, NULL, BK_FLAG_ISSET(pc->pc_flags, PC_THREADREADY)?BK_RUN_THREADREADY:0);

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}


void eventq(bk_s B, struct bk_run *run, void *opaque, const struct timeval *startide, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"bttcp");
  struct program_config *pc = opaque;
  struct tm tm;
  time_t foo;
  char buf[90];

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_VRETURN(B);
  }

  foo = startide->tv_sec;
  localtime_r(&foo, &tm);
  printf("Running event queue job at: %s\n",asctime_r(&tm, buf));
  usleep(pc->pc_seconds*1000);

  if (++pc->pc_cntr == 10)
  {
    printf("Calling exit\n");
    bk_exit(B, 0);
  }

  printf("Exiting event queue job started at: %s\n",asctime_r(&tm, buf));
  BK_VRETURN(B);
}
