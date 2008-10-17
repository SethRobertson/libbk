#if !defined(lint) && !defined(__INSIGHT__)
#include "libbk_compiler.h"
UNUSED static const char libbk__copyright[] = "Copyright (c) 2003";
UNUSED static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright LIBBK++
 *
 * Copyright (c) 2003 The Authors. All rights reserved.
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
#define DEFAULT_PROTO_STR	"tcp"		///< Default protocol
#define DEFAULT_PORT_STR	"5001"		///< Default port
#define ANY_PORT		"0"		///< Any port is OK



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
  bk_flags		pc_flags;		///< Everyone needs flags.
#define PC_VERBOSE			0x01	///< Verbose output
  int			pc_buffer;		///< Buffer sizes
  struct bk_run	*	pc_run;			///< Run structure.
  int			pc_len;			///< Input size hints
};



static int proginit(bk_s B, struct program_config *pconfig);
static void relay_finish(bk_s B, void *args, struct bk_ioh *read_ioh, struct bk_ioh *write_ioh, bk_vptr *data,  bk_flags flags);



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
  struct timeval tmstart, tmend;
  struct poptOption optionsTable[] =
  {
    {"debug", 'd', POPT_ARG_NONE, NULL, 'd', "Turn on debugging", NULL },
    {"verbose", 'v', POPT_ARG_NONE, NULL, 'v', "Turn on verbose message", NULL },
    {"no-seatbelts", 0, POPT_ARG_NONE, NULL, 0x1000, "Sealtbelts off & speed up", NULL },
    {"buffersize", 0, POPT_ARG_INT, NULL, 8, "Size of I/O queues", "buffer size" },
    {"length", 'l', POPT_ARG_INT, NULL, 9, "Default I/O chunk size", "default length" },
    POPT_AUTOHELP
    POPT_TABLEEND
  };

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD(B, "BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, LOG_LOCAL0, 0)))
  {
    fprintf(stderr,"Could not perform basic initialization\n");
    exit(254);
  }
  bk_fun_reentry(B);

  pc = &Pconfig;
  memset(pc,0,sizeof(*pc));

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

    case 8:					// Buffer size
      pc->pc_buffer = atoi(poptGetOptArg(optCon));
      break;

    case 9:					// Default I/O chunks
      pc->pc_len = atoi(poptGetOptArg(optCon));
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

  gettimeofday(&tmstart, NULL);

  if (bk_run_run(B,pc->pc_run, 0)<0)
  {
    bk_die(B, 1, stderr, "Failure during run_run\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  }

  gettimeofday(&tmend, NULL);

  BK_TV_SUB(&tmend, &tmend, &tmstart);
  fprintf(stderr,"%d.%06d\n",(int)tmend.tv_sec, (int)tmend.tv_usec);

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
  struct bk_ioh *stdin_ioh = NULL;
  struct bk_ioh *stdout_ioh = NULL;
  int devnull1, devnull2;

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

  if ((devnull1 = open("/dev/null", O_RDONLY)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not open /dev/null\n");
    goto error;
  }

  if ((devnull2 = open("/dev/null", O_WRONLY)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not open /dev/null\n");
    goto error;
  }

  if (!(stdin_ioh = bk_ioh_init(B, NULL, fileno(stdin), devnull2, NULL, NULL, pc->pc_len, pc->pc_buffer, pc->pc_buffer, pc->pc_run, BK_IOH_RAW|BK_IOH_STREAM)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create ioh on stdin/stdout\n");
    goto error;
  }

  if (!(stdout_ioh = bk_ioh_init(B, NULL, devnull1, fileno(stdout), NULL, NULL, pc->pc_len, pc->pc_buffer, pc->pc_buffer, pc->pc_run, BK_IOH_RAW|BK_IOH_STREAM)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create ioh on stdin/stdout\n");
    goto error;
  }

  if (bk_relay_ioh(B, stdin_ioh, stdout_ioh, relay_finish, pc, NULL, NULL, BK_RELAY_IOH_DONTCLOSEFDS|BK_RELAY_IOH_NOSHUTDOWN) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not relay my iohs\n");
    goto error;
  }

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}



/**
 * Callback prototype for ioh relay. This is called once per read and once
 * one everything is shutdown. While the relay is active, the callback is
 * called when the data has been read but before it has been
 * written. During shutdown, indicated by a NULL data argument, the
 * read_ioh and write_ioh no longer have these meanings, but are supplied
 * for convenience.
 *
 *	@param B BAKA thread/global state.
 *	@param opaque Data supplied by the relay initiator (optional of course).
 *	@param read_ioh Where the data came from.
 *	@param flags Where the data is going.
 *	@param data The data to be relayed.
 *	@param flags Flags for future use.
 */
static void
relay_finish(bk_s B, void *args, struct bk_ioh *read_ioh, struct bk_ioh *write_ioh, bk_vptr *data,  bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"bttcp");
  struct program_config *pc;

  // We only want to process on shutdown.
  if (data)
    BK_VRETURN(B);

  if (!(pc = args))
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  /* <TODO> Report statistics here </TODO> */
  bk_run_set_run_over(B,pc->pc_run);
  BK_VRETURN(B);
}
