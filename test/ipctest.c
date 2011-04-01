#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2010-2011";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2010-2011 The Authors. All rights reserved.
 *
 * This source code is licensed to you under the terms of the file
 * LICENSE.TXT in this release for further details.
 *
 * Send e-mail to <projectbaka@baka.org> for further information.
 *
 * - -Copyright BAKA- -
 */

/**
 * @file
 *
 * This file implements IPC performance test, using three different modes
 */

#include <libbk.h>
#include <mqueue.h>



#define ERRORQUEUE_DEPTH	32		///< Default depth
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
#define PC_MQ				0x02	///< Message queue
#define PC_MB				0x04	///< Mailbox/mutex
#define PC_BK				0x08	///< Mailbox/mutex
  int			pc_buffer;		///< Buffer sizes
  int			pc_chunks;		///< Number of chunks to send
  volatile int		pc_ready;		///< Mailbox communication
  pthread_mutex_t       pc_mutex;		///< Protection of mutex
  pthread_cond_t	pc_cond;		///< Wakeup other party
  void * volatile       pc_buf;			///< Buffer to read
  mqd_t			pc_mqin;		///< Message queue
  mqd_t			pc_mqout;		///< Message queue
  pthread_t	       *pc_recvthread;		///< Receiver thread
  struct bk_shmipc     *pc_shmipc;		///< Shared memory ring
};



static int proginit(bk_s B, struct program_config *pconfig);
static void *recvthread(bk_s B, void *opaque);



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
  BK_ENTRY_MAIN(B, __FUNCTION__, __FILE__, "ipctest");
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
    {"chunks", 'n', POPT_ARG_INT, NULL, 9, "Number of test chunks to send", "count" },
    {"mailbox", 0, POPT_ARG_NONE, NULL, 10, "Use mailbox/mutex testing", NULL },
    {"messagequeue", 0, POPT_ARG_NONE, NULL, 11, "Use messagequeue testing", NULL },
    {"shmipc", 0, POPT_ARG_NONE, NULL, 12, "Use bk shmipc testing", NULL },
    POPT_AUTOHELP
    POPT_TABLEEND
  };

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD(B, "BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, LOG_LOCAL0, BK_GENERAL_THREADREADY)))
  {
    fprintf(stderr,"Could not perform basic initialization\n");
    exit(254);
  }
  bk_fun_reentry(B);

  pc = &Pconfig;
  memset(pc,0,sizeof(*pc));
  pc->pc_buffer = 16;
  pc->pc_chunks = 1000000;

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
      pc->pc_chunks = atoi(poptGetOptArg(optCon));
      break;

    case 10:					// Mailbox mode
      BK_FLAG_SET(pc->pc_flags, PC_MB);
      pc->pc_ready++;
      break;

    case 11:					// Message queue mode
      BK_FLAG_SET(pc->pc_flags, PC_MQ);
      pc->pc_ready++;
      break;

    case 12:					// BK SHMIPC
      BK_FLAG_SET(pc->pc_flags, PC_BK);
      pc->pc_ready++;
      break;

    }
  }

  if (c < -1 || getopterr || (pc->pc_ready > 1))
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

  char *buf;
  int cnt;
  u_int prio = 1;

  if (!(buf = malloc(pc->pc_buffer)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocated %d byte buffer: %s\n", pc->pc_buffer, strerror(errno));
    bk_die(B, 1, stderr, "Could not allocate", BK_WARNDIE_WANTDETAILS);
  }

  for(cnt=pc->pc_chunks;cnt>0;cnt--)
  {
    if (BK_FLAG_ISSET(pc->pc_flags, PC_MQ))
    {
      if (mq_send(pc->pc_mqout, buf, pc->pc_buffer,prio) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not send: %s\n", strerror(errno));
	bk_die(B, 1, stderr, "Could not send", BK_WARNDIE_WANTDETAILS);
      }
    }
    else if (BK_FLAG_ISSET(pc->pc_flags, PC_MB))
    {
      pthread_mutex_lock(&pc->pc_mutex);
      while (pc->pc_ready)
	pthread_cond_wait(&pc->pc_cond, &pc->pc_mutex);
      pc->pc_buf = buf;			// Simulated usage
      pc->pc_ready = 1;
      pthread_mutex_unlock(&pc->pc_mutex);
      pthread_cond_signal(&pc->pc_cond);
    }
    else if (BK_FLAG_ISSET(pc->pc_flags, PC_BK))
    {
      if (bk_shmipc_write(B, pc->pc_shmipc, buf, sizeof(void *), 0, BK_SHMIPC_WRITEALL) != sizeof(void *))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not shmsend\n");
	bk_die(B, 1, stderr, "Could not receive", BK_WARNDIE_WANTDETAILS);
      }
    }
  }

  void *retval;
  if (pthread_join(*pc->pc_recvthread, &retval) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not join: %s\n", strerror(errno));
    bk_die(B, 1, stderr, "Could not join", BK_WARNDIE_WANTDETAILS);
  }
  if (!retval)
  {
    bk_error_printf(B, BK_ERR_ERR, "Failure in receiving thread: %s\n", strerror(errno));
    bk_die(B, 1, stderr, "Thread failed", BK_WARNDIE_WANTDETAILS);
  }

  fprintf(stderr,"S");
  gettimeofday(&tmend, NULL);

  BK_TV_SUB(&tmend, &tmend, &tmstart);
  fprintf(stderr,"\n%d.%06d for %d messages\n",(int)tmend.tv_sec, (int)tmend.tv_usec, pc->pc_chunks);

  if (BK_FLAG_ISSET(pc->pc_flags, PC_MQ))
  {
    fprintf(stderr,"In message queue mode\n");
  }
  else if (BK_FLAG_ISSET(pc->pc_flags, PC_MB))
  {
    fprintf(stderr,"In mailbox/mutex mode\n");
  }
  else if (BK_FLAG_ISSET(pc->pc_flags, PC_BK))
  {
    fprintf(stderr,"In bk_shmipc mode\n");
  }
  else
  {
    fprintf(stderr,"In noop mode\n");
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
  BK_ENTRY(B, __FUNCTION__,__FILE__,"ipctest");

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    goto error;
  }

  mq_unlink("/test");

  if (pthread_mutex_init(&pc->pc_mutex, NULL) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not initialize mutex: %s\n", strerror(errno));
    goto error;
  }

  if (pthread_cond_init(&pc->pc_cond, NULL) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not initialize mutex: %s\n", strerror(errno));
    goto error;
  }

  if ((pc->pc_mqin = mq_open("/test",O_RDONLY|O_CREAT, 0600, NULL)) == -1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not open mq for reading: %s\n", strerror(errno));
    goto error;
  }

  if ((pc->pc_mqout = mq_open("/test",O_WRONLY|O_CREAT, 0600, NULL)) == -1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not open mq for writing: %s\n", strerror(errno));
    goto error;
  }

  pc->pc_ready = 0;
  if (!(pc->pc_recvthread = bk_general_thread_create(B, "recvthread", recvthread, pc, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create receiver thread\n");
    goto error;
  }

  if (BK_FLAG_ISSET(pc->pc_flags, PC_BK))
  {
    if (!(pc->pc_shmipc = bk_shmipc_create(B, "test", 0, 0, 1, 16300, 0600, NULL, BK_SHMIPC_WRONLY)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create shared memory ipc\n");
      goto error;
    }
  }

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}



/**
 * Recevier thread
 *
 *	@param B BAKA thread/global state
 *	@param opaque Data passed into thread (pc)
 *	@return <i>NULL</i> on failure
 *	@return <i>pointer to pc</i> on success
 */
static void *recvthread(bk_s B, void *opaque)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"ipctest");
  struct program_config *pc;
  char *buf;
  int cnt;
  u_int prio;
  struct bk_shmipc *shmipc = NULL;

  if (!(pc = opaque))
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    goto error;
  }

  if (!(buf = malloc(8192)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocated %d byte buffer: %s\n", pc->pc_buffer, strerror(errno));
    goto error;
  }

  if (BK_FLAG_ISSET(pc->pc_flags, PC_BK))
  {
    if (!(shmipc = bk_shmipc_create(B, "test", 0, 0, 1, 0, 0600, NULL, BK_SHMIPC_RDONLY)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create shared memory ipc reader\n");
      goto error;
    }
  }


  for(cnt=pc->pc_chunks;cnt>0;cnt--)
  {
    if (BK_FLAG_ISSET(pc->pc_flags, PC_MQ))
    {
      if (mq_receive(pc->pc_mqin, buf, 8192,&prio) < 1)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not receive: %s\n", strerror(errno));
	goto error;
      }
    }
    else if (BK_FLAG_ISSET(pc->pc_flags, PC_MB))
    {
      volatile void *mbuf;
      pthread_mutex_lock(&pc->pc_mutex);
      while (!pc->pc_ready)
	pthread_cond_wait(&pc->pc_cond, &pc->pc_mutex);
      mbuf = pc->pc_buf;			// Simulated usage
      pc->pc_ready = 0;
      pthread_mutex_unlock(&pc->pc_mutex);
      pthread_cond_signal(&pc->pc_cond);
    }
    else if (BK_FLAG_ISSET(pc->pc_flags, PC_BK))
    {
      if (bk_shmipc_read(B, shmipc, buf, sizeof(void *), 0, BK_SHMIPC_READALL) != sizeof(void *))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not shmreceive\n");
	goto error;
      }
    }
  }

  fprintf(stderr,"R");

  if (BK_FLAG_ISSET(pc->pc_flags, PC_BK))
  {
    bk_shmipc_destroy(B, shmipc, 0);
  }

  BK_RETURN(B, pc);

 error:
  BK_RETURN(B, NULL);
}
