#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2004-2008";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2004-2008 The Authors. All rights reserved.
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
 * This file implements both ends of a bidirectional relay.  You
 * specify two inbound and outbound sockets (or one of each) and the
 * system will create/wait for the necessary connections to be
 * established (active connections will be deferred until passive
 * connections are completed).
 *
 * TODO:
 *	Oh, probably a lot of stuff
 *	--log <filename> log data going back and forth
 *	Modify command line args to show who is connected
 *	Securenets for inbound connections
 *	SSL relay
 */

#include <libbk.h>
#include <libbk_net.h>
/* #include <libbkssl.h> */


#define ERRORQUEUE_DEPTH	32		///< Default depth
#define DEFAULT_PROTO_STR	"tcp"		///< Default protocol
#define DEFAULT_PORT_STR	"5001"		///< Default port
#define DEFAULT_BLOCK_SIZE	8192		///< Default ioh block size
#define ANY_PORT		"0"		///< Any port is OK



/**
 * Information of international importance to everyone
 * which cannot be passed around.
 */
struct global_structure
{
} Global;



/**
 * An enumeration defining the roles we can play (transmit, receive, etc)
 */
typedef enum
{
  BttcpRoleTransmit=1,				///< We are trans. (connect)
  BttcpRoleReceive,				///< We are receiver (accept)
} bttcp_role_e;



/**
 * Information about basic program runtime configuration
 * which must be passed around.
 */
struct program_config
{
  bttcp_role_e		pc_role_a;		///< What role do I play?
  bttcp_role_e		pc_role_b;		///< What role do I play?
  bk_flags		pc_flags;		///< Everyone needs flags.
#define BTTCP_FLAG_SHUTTING_DOWN_SERVER	0x00001	///< We're shutting down a server
#define PC_VERBOSE			0x00002	///< Verbose output
#define PC_CLOSE_AFTER_ONE		0x00004	///< Close relay after one side closed
#define PC_NODELAY			0x00008	///< Set nodelay socket buffer option
#define PC_KEEPALIVE			0x00010	///< Set keepalive preference
#define PC_SERVER			0x00020	///< Server mode
#define PC_ACTIVEACTIVE			0x00040	///< Active connections started
#define PC_WAIT_DATA_A			0x00080 ///< Wait for one junk byte from A before further
#define PC_MULTISERVER			0x00100	///< Multiple attach server mode
#define PC_ANNOUNCE_ON_RELAY		0x00200 ///< Defer announcements until relay is set up
  const char *		pc_password_a;		///< Password to expect from a
  const char *		pc_password_b;		///< Password to expect from b
  const char *		pc_announce_a;		///< Data to announce to side a
  const char *		pc_announce_b;		///< Data to announce to side b
  char *		pc_proto_a;		///< What protocol to use
  char *		pc_proto_b;		///< What protocol to use
  char *		pc_remoteurl_a;		///< Remote "url".
  char *		pc_remoteurl_b;		///< Remote "url".
  char *		pc_localurl_a;		///< Local "url".
  char *		pc_localurl_b;		///< Local "url".
  struct bk_netinfo *	pc_local_a;		///< Local side info.
  struct bk_netinfo *	pc_local_b;		///< Local side info.
  struct bk_netinfo *	pc_remote_a;		///< Remote side info.
  struct bk_netinfo *	pc_remote_b;		///< Remote side info.
  struct bk_run	*	pc_run;			///< Run structure.
  int			pc_fd_a;		///< FD of side A
  int			pc_fd_b;		///< FD of side B
  int			pc_af_a;		///< Address family.
  int			pc_af_b;		///< Address family.
  long			pc_timeout;		///< Connection timeout
  void *		pc_server_a;		///< Server handle
  void *		pc_server_b;		///< Server handle
  int			pc_buffer;		///< Buffer sizes
  int			pc_len;			///< Input size hints
  int			pc_sockbuf;		///< Socket buffer size
  struct bk_relay_ioh_stats	pc_stats;	///< Statistics about relay
  struct timeval	pc_start;		///< Starting time
  const char	       *pc_logfilename;		///< Protocol log file
  FILE		       *pc_logfile;		///< Output handle
  int			pc_desiredpassive;	///< Number of passive connections
  int			pc_desiredactive;	///< Number of passive connections
  int			pc_actualpassive;	///< Number of passive connections so far
  int			pc_actualactive;	///< Number of active connections so far
};



/**
 * Stupidity to identify which side we are referring to by
 * using the address of memory;
 */
struct sideidentify
{
  struct program_config	*si_pc;
};
struct sideidentify sidea, sideb;



static int proginit(bk_s B, struct program_config *pconfig);
static int connect_complete(bk_s B, void *args, int sock, struct bk_addrgroup *bag, void *server_handle, bk_addrgroup_state_e state);
static void relay_finish(bk_s B, void *args, struct bk_ioh *read_ioh, struct bk_ioh *write_ioh, bk_vptr *data,  bk_flags flags);
static void cleanup(bk_s B, struct program_config *pc);



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
  struct poptOption optionsTable[] =
  {
    {"debug", 'd', POPT_ARG_NONE, NULL, 'd', "Turn on debugging", NULL },
    {"verbose", 'v', POPT_ARG_NONE, NULL, 'v', "Turn on verbose message", NULL },
    {"no-seatbelts", 0, POPT_ARG_NONE, NULL, 0x1000, "Sealtbelts off & speed up", NULL },
    {"receive_a", 0, POPT_ARG_NONE, NULL, 0x100, "Receive on side A", NULL },
    {"receive_b", 0, POPT_ARG_NONE, NULL, 0x101, "Receive on side B", NULL },
    {"transmit_a", 0, POPT_ARG_STRING, NULL, 0x102, "Transmit to host on side A", "proto://ip:port" },
    {"transmit_b", 0, POPT_ARG_STRING, NULL, 0x103, "Transmit to host on side B", "proto://ip:port" },
    {"local-name_a", 0, POPT_ARG_STRING, NULL, 0x104, "Local address to bind on side A", "proto://ip:port" },
    {"local-name_b", 0, POPT_ARG_STRING, NULL, 0x105, "Local address to bind on side B", "proto://ip:port" },
    {"wait-data_a", 0, POPT_ARG_NONE, NULL, 0x106, "Wait for data from A before attempting side B", NULL },
    {"announce_a", 0, POPT_ARG_STRING, NULL, 0x107, "Send the listed string to a on attach", "announcement" },
    {"announce_b", 0, POPT_ARG_STRING, NULL, 0x108, "Send the listed string to b on attach", "announcement" },
    {"password_a", 0, POPT_ARG_STRING, NULL, 0x109, "Use the listed string as password from a on attach", "password" },
    {"password_b", 0, POPT_ARG_STRING, NULL, 0x10a, "Use the listed string as password from b on attach", "password" },
    {"announce_on_complete", 0, POPT_ARG_NONE, NULL, 0x10b, "Send announcements when all connected", NULL },
    {"buffersize", 0, POPT_ARG_STRING, NULL, 8, "Size of I/O queues", "buffer size" },
    {"length", 'L', POPT_ARG_STRING, NULL, 9, "Default I/O chunk size", "default length" },
    {"close-after-one", 'c', POPT_ARG_NONE, NULL, 10, "Shut down relay after only one side closes", NULL },
    {"timeout", 'T', POPT_ARG_INT, NULL, 'T', "Set the connection timeout", "timeout" },
    {"sockbuf", 'B', POPT_ARG_STRING, NULL, 15, "Socket snd/rcv buffer size", "length in bytes" },
    {"nodelay", 0, POPT_ARG_NONE, NULL, 16, "Set TCP NODELAY", NULL },
    {"keepalive", 0, POPT_ARG_NONE, NULL, 17, "Set TCP KEEPALIVE", NULL },
#ifdef NOTYET
    {"logfile", 0, POPT_ARG_STRING, NULL, 18, "Log file", "filename" },
#endif /* NOTYET */
    {"server", 0, POPT_ARG_NONE, NULL, 21, "Set server (multiple connection) mode", NULL },
    {"multiserver", 0, POPT_ARG_NONE, NULL, 22, "Set multiple server mode", NULL },
    POPT_AUTOHELP
    POPT_TABLEEND
  };

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD(NULL, "BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, LOG_LOCAL0, 0)))
  {
    fprintf(stderr,"Could not perform basic initialization\n");
    exit(254);
  }
  bk_fun_reentry(B);

  pc = &Pconfig;
  memset(pc,0,sizeof(*pc));
  pc->pc_timeout=BK_SECS_TO_EVENT(30);
  pc->pc_proto_a=DEFAULT_PROTO_STR;
  pc->pc_proto_b=DEFAULT_PROTO_STR;
  pc->pc_len=DEFAULT_BLOCK_SIZE;
  pc->pc_fd_a = -1;
  pc->pc_fd_b = -1;

  if (!(optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not initialize options processing\n");
    bk_exit(B,254);
  }
  //  poptSetOtherOptionHelp(optCon, _("[NON-FLAG ARGUMENTS]"));

  while ((c = poptGetNextOpt(optCon)) >= 0)
  {
    switch (c)
    {
    case 'd':					// debug
      bk_error_config(B, BK_GENERAL_ERROR(B), 0, stderr, BK_ERR_NONE, BK_ERR_DEBUG, BK_ERROR_CONFIG_FH | BK_ERROR_CONFIG_HILO_PIVOT | BK_ERROR_CONFIG_SYSLOGTHRESHOLD);
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

    case 0x100:					// Receive
      if (pc->pc_role_a)
      {
	fprintf(stderr,"Cannot be both transmitter and receiver on same side\n");
	exit(1);
      }
      pc->pc_role_a=BttcpRoleReceive;
      if (!pc->pc_localurl_a)
      {
	pc->pc_localurl_a=BK_ADDR_ANY;
      }
      break;

    case 0x101:					// Receive
      if (pc->pc_role_b)
      {
	fprintf(stderr,"Cannot be both transmitter and receiver on same side\n");
	exit(1);
      }
      pc->pc_role_b=BttcpRoleReceive;
      if (!pc->pc_localurl_b)
      {
	pc->pc_localurl_b=BK_ADDR_ANY;
      }
      break;

    case 0x102:					// transmit
      if (pc->pc_role_a)
      {
	fprintf(stderr,"Cannot be both transmitter and receiver on same side\n");
	exit(1);
      }
      pc->pc_role_a=BttcpRoleTransmit;
      pc->pc_remoteurl_a=(char *)poptGetOptArg(optCon);
      break;

    case 0x103:					// transmit
      if (pc->pc_role_b)
      {
	fprintf(stderr,"Cannot be both transmitter and receiver on same side\n");
	exit(1);
      }
      pc->pc_role_b=BttcpRoleTransmit;
      pc->pc_remoteurl_b=(char *)poptGetOptArg(optCon);
      break;

    case 0x104:					// Local url
      pc->pc_localurl_a=(char *)poptGetOptArg(optCon);
      break;

    case 0x105:					// Local url
      pc->pc_localurl_b=(char *)poptGetOptArg(optCon);
      break;

    case 0x106:
      BK_FLAG_SET(pc->pc_flags, PC_WAIT_DATA_A);
      break;

    case 0x107:
      pc->pc_announce_a = poptGetOptArg(optCon);
      break;

    case 0x108:
      pc->pc_announce_b = poptGetOptArg(optCon);
      break;

    case 0x109:
      pc->pc_password_a = poptGetOptArg(optCon);
      break;

    case 0x10a:
      pc->pc_password_b = poptGetOptArg(optCon);
      break;

    case 0x10b:
      BK_FLAG_SET(pc->pc_flags, PC_ANNOUNCE_ON_RELAY);
      break;

    case 'T':					// Timeout
      pc->pc_timeout=BK_SECS_TO_EVENT(atoi(poptGetOptArg(optCon)));
      break;

    case 8:					// Buffer size
      {
	double tmplimit = bk_string_demagnify(B, poptGetOptArg(optCon), 0);
	if (isnan(tmplimit))
	  getopterr++;
	pc->pc_buffer = tmplimit;
      }
      break;

    case 9:					// Default I/O chunks
      {
	double tmplimit = bk_string_demagnify(B, poptGetOptArg(optCon), 0);
	if (isnan(tmplimit))
	  getopterr++;
	pc->pc_len = tmplimit;
      }
      break;

    case 10:					// Close after one
      BK_FLAG_SET(pc->pc_flags, PC_CLOSE_AFTER_ONE);
      break;

    case 15:
      {
	double tmplimit = bk_string_demagnify(B, poptGetOptArg(optCon), 0);
	if (isnan(tmplimit))
	  getopterr++;
	pc->pc_sockbuf = tmplimit;
      }
      break;

    case 16:
      BK_FLAG_SET(pc->pc_flags, PC_NODELAY);
      break;

    case 17:
      BK_FLAG_SET(pc->pc_flags, PC_KEEPALIVE);
      break;

    case 21:
      BK_FLAG_SET(pc->pc_flags, PC_SERVER);
      break;

    case 22:
      BK_FLAG_SET(pc->pc_flags, PC_MULTISERVER);
      break;
    }
  }

  /*
   * Reprocess so that argc and argv contain the remaining command
   * line arguments (note argv[0] is an argument, not the program
   * name).  argc remains the number of elements in the argv array.
   */
  argv = (char **)poptGetArgs(optCon);
  argc = 0;
  if (argv)
    for (; argv[argc]; argc++)
      ; // Void

  if (c < -1 || getopterr || !pc->pc_role_a || !pc->pc_role_b || argc > 0)
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

  if (pc->pc_desiredactive == 2)
  {
    do
    {
      int retcode = 0;

      if (BK_FLAG_ISSET(pc->pc_flags, PC_SERVER))
      {
	switch (retcode = fork())
	{
	case -1:
	  bk_error_printf(B, BK_ERR_ERR, "Could not create child process to handle accepted fd: %s\n", strerror(errno));
	  bk_die(B, 1, stderr, "Fork failure--assuming worst case and going away\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
	  break;
	default:					// Parent
	  waitpid(retcode, &retcode, 0);
	  sleep(1);
	  // <TODO>Sleep or something to prevent spinning</TODO>
	  continue;
	case 0:					// Child
	  BK_FLAG_CLEAR(pc->pc_flags, PC_SERVER);
	  break;				// Continue processing
	}
      }
      if (BK_FLAG_ISCLEAR(pc->pc_flags, PC_WAIT_DATA_A))
	BK_FLAG_SET(pc->pc_flags, PC_ACTIVEACTIVE);

      if (pc->pc_remoteurl_a && bk_netutils_make_conn_verbose(B, pc->pc_run, pc->pc_remoteurl_a, NULL, DEFAULT_PORT_STR, pc->pc_localurl_a, NULL, NULL, pc->pc_proto_a, pc->pc_timeout, connect_complete, &sidea, NULL, NULL, NULL, NULL, 0, 0) < 0)
	bk_die(B, 1, stderr, "Could not start side a transmitter (Remote not ready?)\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);

      if (BK_FLAG_ISCLEAR(pc->pc_flags, PC_WAIT_DATA_A) && pc->pc_remoteurl_b && bk_netutils_make_conn_verbose(B, pc->pc_run, pc->pc_remoteurl_b, NULL, DEFAULT_PORT_STR, pc->pc_localurl_b, NULL, NULL, pc->pc_proto_b, pc->pc_timeout, connect_complete, &sideb, NULL, NULL, NULL, NULL, 0, 0) < 0)
	bk_die(B, 1, stderr, "Could not start side a transmitter (Remote not ready?)\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
      break;
    } while (BK_FLAG_ISSET(pc->pc_flags, PC_SERVER));
  }

  if (bk_run_run(B,pc->pc_run, 0)<0)
  {
    bk_die(B, 1, stderr, "Failure during run_run\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  }

  cleanup(B, pc);
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

  // SIGPIPE is just annoying
  bk_signal(B, SIGPIPE, SIG_IGN, BK_RUN_SIGNAL_RESTART);

  // We use alarm for interrupt timing (running bk_reaper is kinda stupid)
  bk_signal(B, SIGALRM, bk_reaper, BK_RUN_SIGNAL_INTR);
  bk_signal(B, SIGCHLD, bk_reaper, BK_RUN_SIGNAL_RESTART);

  if (!pc->pc_af_a)
    pc->pc_af_a = AF_INET;

  if (!pc->pc_af_b)
    pc->pc_af_b = AF_INET;

  if (!(pc->pc_run = bk_run_init(B, 0)))
  {
    fprintf(stderr,"Could not create run structure\n");
    goto error;
  }

  sidea.si_pc = pc;
  sideb.si_pc = pc;

  if (pc->pc_role_a == BttcpRoleReceive)
  {
    if (bk_netutils_start_service_verbose(B, pc->pc_run, pc->pc_localurl_a, BK_ADDR_ANY, DEFAULT_PORT_STR, pc->pc_proto_a, NULL, connect_complete, &sidea, 0, NULL, NULL, NULL, NULL, 0, 0))
      bk_die(B, 1, stderr, "Could not start receiver (Port in use?)\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
    pc->pc_desiredpassive++;
  }
  else
    pc->pc_desiredactive++;

  if (pc->pc_role_b == BttcpRoleReceive)
  {
    pc->pc_desiredpassive++;

    if (BK_FLAG_ISCLEAR(pc->pc_flags, PC_MULTISERVER) && bk_netutils_start_service_verbose(B, pc->pc_run, pc->pc_localurl_b, BK_ADDR_ANY, DEFAULT_PORT_STR, pc->pc_proto_b, NULL, connect_complete, &sideb, 0, NULL, NULL, NULL, NULL, 0, 0))
      bk_die(B, 1, stderr, "Could not start receiver (Port in use?)\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  }
  else
    pc->pc_desiredactive++;

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}



/**
 * What to do when connection completes.
 *
 *	@param B BAKA thread/global state.
 *	@param args Opaque arguments for me
 *	@param sock New socket which we are talking about
 *	@param bag libbk connection state information
 *	@param server_handle For server sockets, handle to server for potential future descriptiuon
 *	@param state Why this function is being called
 *	@param <i>0</i> for normal conditions
 *	@param <br><i>-1</i> for terminal errors--note handler will be called again for shutdown/error notifications
 */
static int
connect_complete(bk_s B, void *args, int sock, struct bk_addrgroup *bag, void *server_handle, bk_addrgroup_state_e state)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"bttcp");
  struct sideidentify *si = args;
  struct program_config *pc;
  int one = 1;
  int side_a = -1;

  if (!si || !(pc = si->si_pc))
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (si == &sidea)
    side_a = 1;
  else if (si == &sideb)
    side_a = 0;
  else
  {
    bk_error_printf(B, BK_ERR_ERR, "Supplied args %p is neither A nor B\n", si);
    BK_RETURN(B, -1);
  }

  switch (state)
  {
  case BkAddrGroupStateSysError:
    fprintf(stderr,"%s%s: A system error occured\n", BK_GENERAL_PROGRAM(B), side_a?"(a)":"(b)");
    goto error;
    break;
  case BkAddrGroupStateRemoteError:
    fprintf(stderr,"%s%s: A remote network error occured (connection refused?)\n", BK_GENERAL_PROGRAM(B), side_a?"(a)":"(b)");
    goto error;
    break;
  case BkAddrGroupStateLocalError:
    fprintf(stderr,"%s%s: A local network error occured (address already in use?)\n", BK_GENERAL_PROGRAM(B), side_a?"(a)":"(b)");
    goto error;
    break;
  case BkAddrGroupStateTimeout:
    fprintf(stderr,"%s%s: The connection timed out with no more addresses to try\n", BK_GENERAL_PROGRAM(B), side_a?"(a)":"(b)");
    goto error;
    break;
  case BkAddrGroupStateReady:
    if (side_a)
      pc->pc_server_a = server_handle;
    else
      pc->pc_server_b = server_handle;

      if (BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE))
      {
	struct sockaddr_in mysin;
	int sinlen = sizeof(mysin);

	getsockname(sock, (struct sockaddr *)&mysin, &sinlen);

	fprintf(stderr,"%s%s: Ready and listening on port %d\n", BK_GENERAL_PROGRAM(B), side_a?"(a)":"(b)", htons(mysin.sin_port));
      }
    goto done;
    break;
  case BkAddrGroupStateConnected:
    if (side_a && pc->pc_server_a)
    {
      if (BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE))
      {
	struct sockaddr_in mysin;
	int sinlen = sizeof(mysin);

	getpeername(sock, (struct sockaddr *)&mysin, &sinlen);

	fprintf(stderr,"%s%s: Accepted connection from %s\n", BK_GENERAL_PROGRAM(B), side_a?"(a)":"(b)", inet_ntoa(mysin.sin_addr));
      }
      pc->pc_actualpassive++;
    }
    if (side_a && !pc->pc_server_a)
    {
      if (BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE))
	fprintf(stderr,"%s%s: Connection complete\n", BK_GENERAL_PROGRAM(B), side_a?"(a)":"(b)");
      pc->pc_actualactive++;
    }
    if (!side_a && pc->pc_server_b)
    {
      if (BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE))
      {
	struct sockaddr_in mysin;
	int sinlen = sizeof(mysin);

	getpeername(sock, (struct sockaddr *)&mysin, &sinlen);

	fprintf(stderr,"%s%s: Accepted connection from %s\n", BK_GENERAL_PROGRAM(B), side_a?"(a)":"(b)", inet_ntoa(mysin.sin_addr));
      }
      pc->pc_actualpassive++;
    }
    if (!side_a && !pc->pc_server_b)
    {
      if (BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE))
	fprintf(stderr,"%s%s: Connection complete\n", BK_GENERAL_PROGRAM(B), side_a?"(a)":"(b)");
      pc->pc_actualactive++;
    }
    if (side_a)
    {
      if (pc->pc_announce_a && BK_FLAG_ISCLEAR(pc->pc_flags, PC_ANNOUNCE_ON_RELAY))
      {
	if (write(sock, pc->pc_announce_a, strlen(pc->pc_announce_a)+1) < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Write failed: %s.\n", strerror(errno));
	  goto error;
	}
      }

      if (pc->pc_password_a)
      {
	char *buf;
	int len = strlen(pc->pc_password_a)+1;

	if (!BK_MALLOC_LEN(buf, len))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Malloc(%d): %s\n", len, strerror(errno));
	  bk_die(B, 1, stderr, "Password a allocation failure.  Assuming worst and going away.\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
	}

	bk_fileutils_modify_fd_flags(B, sock, O_NONBLOCK, BkFileutilsModifyFdFlagsActionDelete);
	alarm(2);
	if ((len = read(sock, buf, len)) != len)
	{
	  alarm(0);
	  bk_error_printf(B, BK_ERR_ERR, "Password read a (aborting connection): %d: %s\n", len, strerror(errno));
	  if (BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE))
	    fprintf(stderr,"%s%s: Password read error.  Aborting connection\n", BK_GENERAL_PROGRAM(B), side_a?"(a)":"(b)");
	  close(sock);
	  if (pc->pc_server_a)
	    pc->pc_actualpassive--;
	  else
	    pc->pc_actualactive--;
	  goto done;
	}
	alarm(0);
	if (strcmp(buf,pc->pc_password_a))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Password a compare failure (aborting connection): %d: %s\n", len, strerror(errno));
	  if (BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE))
	    fprintf(stderr,"%s%s: Password mismatch.  Aborting connection\n", BK_GENERAL_PROGRAM(B), side_a?"(a)":"(b)");
	  close(sock);
	  if (pc->pc_server_a)
	    pc->pc_actualpassive--;
	  else
	    pc->pc_actualactive--;
	  goto done;
	}
	free(buf);
      }

      if (BK_FLAG_ISSET(pc->pc_flags, PC_WAIT_DATA_A))
      {
	char x;
	int len;

	bk_fileutils_modify_fd_flags(B, sock, O_NONBLOCK, BkFileutilsModifyFdFlagsActionDelete);
	if ((len = read(sock, &x, 1)) != 1)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Read: %d: %s\n", len, strerror(errno));
	  bk_die(B, 1, stderr, "Read byte failure.  Assuming worst and going away\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
	}
      }


      pc->pc_fd_a = sock;
      if (pc->pc_server_a)
      {
	if (bk_addressgroup_suspend(B, pc->pc_run, pc->pc_server_a, 0) < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Failed to suspend server socket\n");
	  goto error;
	}
      }
    }
    else
    {
      if (pc->pc_announce_b && BK_FLAG_ISCLEAR(pc->pc_flags, PC_ANNOUNCE_ON_RELAY))
	if (write(sock, pc->pc_announce_b, strlen(pc->pc_announce_b)+1) < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Write failed: %s\n", strerror(errno));
	  goto error;
	}

      if (pc->pc_password_b)
      {
	char *buf;
	int len = strlen(pc->pc_password_b)+1;

	if (!BK_MALLOC_LEN(buf, len))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Malloc(%d): %s\n", len, strerror(errno));
	  bk_die(B, 1, stderr, "Password allocation failure.  Assuming worst and going away.\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
	}

	bk_fileutils_modify_fd_flags(B, sock, O_NONBLOCK, BkFileutilsModifyFdFlagsActionDelete);
	alarm(2);
	if ((len = read(sock, buf, len)) != len)
	{
	  alarm(0);
	  bk_error_printf(B, BK_ERR_ERR, "Password read b (aborting connection): %d: %s\n", len, strerror(errno));
	  if (BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE))
	    fprintf(stderr,"%s%s: Password read error.  Aborting connection\n", BK_GENERAL_PROGRAM(B), side_a?"(a)":"(b)");
	  close(sock);
	  if (pc->pc_server_b)
	    pc->pc_actualpassive--;
	  else
	    pc->pc_actualactive--;
	  goto done;
	}
	alarm(0);
	if (strcmp(buf,pc->pc_password_b))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Password b compare failure (aborting connection): %d: %s\n", len, strerror(errno));
	  if (BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE))
	    fprintf(stderr,"%s%s: Password mismatch.  Aborting connection\n", BK_GENERAL_PROGRAM(B), side_a?"(a)":"(b)");
	  close(sock);
	  if (pc->pc_server_b)
	    pc->pc_actualpassive--;
	  else
	    pc->pc_actualactive--;
	  goto done;
	}
	free(buf);
      }

      pc->pc_fd_b = sock;
      if (pc->pc_server_b)
      {
	if (bk_addressgroup_suspend(B, pc->pc_run, pc->pc_server_b, 0) < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Failed to suspend server socket\n");
	  goto error;
	}
      }
    }

    if (pc->pc_fd_a >= 0 && pc->pc_fd_b >= 0 && BK_FLAG_ISSET(pc->pc_flags, PC_ANNOUNCE_ON_RELAY))
    {
      if (pc->pc_announce_a)
	if (write(pc->pc_fd_a, pc->pc_announce_a, strlen(pc->pc_announce_a)+1) < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Write failed: %s\n", strerror(errno));
	  goto error;
	}
      if (pc->pc_announce_b)
	if (write(pc->pc_fd_b, pc->pc_announce_b, strlen(pc->pc_announce_b)+1) < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Write failed: %s\n", strerror(errno));
	  goto error;
	}
    }

    break;
  case BkAddrGroupStateClosing:
    if (BK_FLAG_ISSET(pc->pc_flags, BTTCP_FLAG_SHUTTING_DOWN_SERVER))
    {
      if (pc->pc_server_a == server_handle)
	pc->pc_server_a = NULL;			/* Very important */
      if (pc->pc_server_b == server_handle)
	pc->pc_server_b = NULL;			/* Very important */
      goto done;
    }

    fprintf(stderr,"Software shutdown during connection setup\n");
    goto error;
    break;
  case BkAddrGroupStateSocket:
    if (pc->pc_sockbuf)
    {
      if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *)&pc->pc_sockbuf, sizeof pc->pc_sockbuf) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not set socket buffer size: %s\n", strerror(errno));
	BK_RETURN(B, -1);
      }
      if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *)&pc->pc_sockbuf, sizeof pc->pc_sockbuf) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not set socket buffer size to %d: %s\n", pc->pc_sockbuf, strerror(errno));
	BK_RETURN(B, -1);
      }
    }
    if (BK_FLAG_ISSET(pc->pc_flags, PC_NODELAY))
    {
      if(setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof(one)) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not set nodelay option: %s\n", strerror(errno));
	BK_RETURN(B, -1);
      }
    }
    if (BK_FLAG_ISSET(pc->pc_flags, PC_KEEPALIVE))
    {
      if(setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char *)&one, sizeof(one)) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not set keepalive option: %s\n", strerror(errno));
	BK_RETURN(B, -1);
      }
    }
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
    {
      bk_error_printf(B, BK_ERR_WARN, "Could not set reuseaddr on socket: %s\n", strerror(errno));
      // Fatal? Nah...
    }
#ifdef SO_REUSEPORT
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)) < 0)
    {
      bk_error_printf(B, BK_ERR_WARN, "Could not set reuseport on socket: %s\n", strerror(errno));
      // Fatal? Nah...
    }
#endif /* SO_REUSEPORT */
    goto done;
    break;
  }

  // XXX - this test is not well thought out and planned.
  if ((pc->pc_actualpassive && (pc->pc_actualpassive >= pc->pc_desiredpassive) && BK_FLAG_ISSET(pc->pc_flags, PC_SERVER) && BK_FLAG_ISCLEAR(pc->pc_flags, PC_MULTISERVER)) ||
      (pc->pc_actualpassive >= pc->pc_desiredpassive && pc->pc_actualactive >= pc->pc_desiredactive && BK_FLAG_ISSET(pc->pc_flags, PC_MULTISERVER)))
  {
    switch (fork())
    {
    case -1:
      bk_error_printf(B, BK_ERR_ERR, "Could not create child process to handle accepted fd: %s\n", strerror(errno));
      bk_die(B, 1, stderr, "Fork failure--assuming worst case and going away\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
      break;
    default:					// Parent
      if (pc->pc_desiredactive == 0)
	exit(0);

      if (pc->pc_fd_a >= 0)
	close(pc->pc_fd_a);
      pc->pc_fd_a = -1;
      if (pc->pc_fd_b >= 0)
	close(pc->pc_fd_b);
      pc->pc_fd_b = -1;
      pc->pc_actualactive = 0;
      pc->pc_actualpassive = 0;
      if (pc->pc_server_a && bk_addressgroup_suspend(B, pc->pc_run, pc->pc_server_a, BK_ADDRESSGROUP_RESUME) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Failed to resume server socket a\n");
	goto error;
      }
      if (pc->pc_server_b && bk_addressgroup_suspend(B, pc->pc_run, pc->pc_server_b, BK_ADDRESSGROUP_RESUME) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Failed to resume server socket b\n");
	goto error;
      }

      goto done;

    case 0:					// Child
      break;					// Continue on with normal path (next if)
    }
  }

  if (pc->pc_actualpassive >= pc->pc_desiredpassive && BK_FLAG_ISCLEAR(pc->pc_flags, BTTCP_FLAG_SHUTTING_DOWN_SERVER))
  {
    // Child (or only process--non-server)
    BK_FLAG_SET(pc->pc_flags, BTTCP_FLAG_SHUTTING_DOWN_SERVER);
    BK_FLAG_CLEAR(pc->pc_flags, PC_SERVER);	// To prevent this from happening again
    if (pc->pc_server_a)
      bk_addrgroup_server_close(B, pc->pc_server_a);
    pc->pc_server_a = NULL;
    if (pc->pc_server_b)
      bk_addrgroup_server_close(B, pc->pc_server_b);
    pc->pc_server_b = NULL;
  }

  if (pc->pc_actualpassive >= pc->pc_desiredpassive &&
      pc->pc_actualactive >= pc->pc_desiredactive)
  {
    struct bk_ioh *ioha, *iohb;

    if (!(ioha = bk_ioh_init(B, NULL, pc->pc_fd_a, pc->pc_fd_a, NULL, NULL, pc->pc_len, pc->pc_buffer, pc->pc_buffer, pc->pc_run, BK_IOH_RAW|BK_IOH_STREAM)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create ioh on side a\n");
      goto error;
    }

    if (!(iohb = bk_ioh_init(B, NULL, pc->pc_fd_b, pc->pc_fd_b, NULL, NULL, pc->pc_len, pc->pc_buffer, pc->pc_buffer, pc->pc_run, BK_IOH_RAW|BK_IOH_STREAM)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create ioh on side b\n");
      bk_ioh_close(B, ioha, 0);
      goto error;
    }

    pc->pc_fd_a = -1;
    pc->pc_fd_b = -1;

    gettimeofday(&pc->pc_start, NULL);

    if (bk_relay_ioh(B, ioha, iohb, relay_finish, pc, &pc->pc_stats, NULL, BK_FLAG_ISSET(pc->pc_flags,PC_CLOSE_AFTER_ONE)?BK_RELAY_IOH_DONE_AFTER_ONE_CLOSE:0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not relay my iohs\n");
      bk_ioh_close(B, iohb, 0);
      bk_ioh_close(B, ioha, 0);
      goto error;
    }
  }
  else if (pc->pc_actualpassive >= pc->pc_desiredpassive && BK_FLAG_ISCLEAR(pc->pc_flags, PC_ACTIVEACTIVE))
  {
    BK_FLAG_SET(pc->pc_flags, PC_ACTIVEACTIVE);

    if (BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE))
      fprintf(stderr,"%s: Connecting...\n", BK_GENERAL_PROGRAM(B));

    if (pc->pc_fd_a < 0 && pc->pc_remoteurl_a && bk_netutils_make_conn_verbose(B, pc->pc_run, pc->pc_remoteurl_a, NULL, DEFAULT_PORT_STR, pc->pc_localurl_a, NULL, NULL, pc->pc_proto_a, pc->pc_timeout, connect_complete, &sidea, NULL, NULL, NULL, NULL, 0, 0) < 0)
      bk_die(B, 1, stderr, "Could not start side a transmitter (Remote not ready?)\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);

    if (pc->pc_fd_b < 0 && pc->pc_remoteurl_b && bk_netutils_make_conn_verbose(B, pc->pc_run, pc->pc_remoteurl_b, NULL, DEFAULT_PORT_STR, pc->pc_localurl_b, NULL, NULL, pc->pc_proto_b, pc->pc_timeout, connect_complete, &sideb, NULL, NULL, NULL, NULL, 0, 0) < 0)
      bk_die(B, 1, stderr, "Could not start side a transmitter (Remote not ready?)\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  }
  else if (pc->pc_actualpassive < pc->pc_desiredpassive && BK_FLAG_ISSET(pc->pc_flags, PC_MULTISERVER))
  {
    switch (fork())
    {
    case -1:
      bk_error_printf(B, BK_ERR_ERR, "Could not create child process to handle accepted fd: %s\n", strerror(errno));
      bk_die(B, 1, stderr, "Fork failure--assuming worst case and going away\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
      break;
    case 0:					// Child
	break;
    default:					// Parent
      if (pc->pc_actualactive)
	exit(0);

      if (pc->pc_fd_a >= 0)
	close(pc->pc_fd_a);
      pc->pc_fd_a = -1;
      if (pc->pc_fd_b >= 0)
	close(pc->pc_fd_b);
      pc->pc_fd_b = -1;
      pc->pc_actualactive = 0;
      pc->pc_actualpassive = 0;
      if (pc->pc_server_a && bk_addressgroup_suspend(B, pc->pc_run, pc->pc_server_a, BK_ADDRESSGROUP_RESUME) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Failed to resume server socket a\n");
	goto error;
      }
      if (pc->pc_server_b && bk_addressgroup_suspend(B, pc->pc_run, pc->pc_server_b, BK_ADDRESSGROUP_RESUME) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Failed to resume server socket b\n");
	goto error;
      }
      goto done;
    }

    if (pc->pc_role_b == BttcpRoleReceive && bk_netutils_start_service_verbose(B, pc->pc_run, pc->pc_localurl_b, BK_ADDR_ANY, DEFAULT_PORT_STR, pc->pc_proto_b, NULL, connect_complete, &sideb, 0, NULL, NULL, NULL, NULL, 0, 0))
      bk_die(B, 1, stderr, "Could not start receiver (Port in use?)\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  }

 done:
  BK_RETURN(B, 0);

 error:
  bk_run_set_run_over(B,pc->pc_run);
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
  struct timeval end, delta;
  char speedin[128];
  char speedout[128];

  if (!(pc = args))
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  // We only care about shutdown (well, we might want to log in the future)
  if (data)
    BK_VRETURN(B);

  if (BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE))
  {
    gettimeofday(&end, NULL);
    BK_TV_SUB(&delta, &end, &pc->pc_start);

    bk_string_magnitude(B, (double)pc->pc_stats.side[0].birs_writebytes/((double)delta.tv_sec + (double)delta.tv_usec/1000000.0), 3, "B/s", speedin, sizeof(speedin), 0);
    bk_string_magnitude(B, (double)pc->pc_stats.side[1].birs_writebytes/((double)delta.tv_sec + (double)delta.tv_usec/1000000.0), 3, "B/s", speedout, sizeof(speedout), 0);

    fprintf(stderr, "%s: %llu bytes from side a in %ld.%06ld seconds: %s\n", BK_GENERAL_PROGRAM(B), BUG_LLU_CAST(pc->pc_stats.side[1].birs_writebytes), (long int) delta.tv_sec, (long int) delta.tv_usec, speedout);
    fprintf(stderr, "%s: %llu bytes from side b in %ld.%06ld seconds: %s\n", BK_GENERAL_PROGRAM(B), BUG_LLU_CAST(pc->pc_stats.side[0].birs_writebytes), (long int) delta.tv_sec, (long int) delta.tv_usec, speedin);
  }

  bk_run_set_run_over(B,pc->pc_run);
  BK_VRETURN(B);
}



/**
 * Cleanup bttcp. Actually this function does nothing but delay exiting and
 * so should probably only compile when INSURE is on (so that we clean up
 * memory we know about), however for the moment we just waste time
 *	@param B BAKA thread/global state.
 *	@param pc program configuraion.
 */
static void
cleanup(bk_s B, struct program_config *pc)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"bttcp");

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (pc->pc_local_a) bk_netinfo_destroy(B,pc->pc_local_a);
  if (pc->pc_remote_a) bk_netinfo_destroy(B,pc->pc_remote_a);
  if (pc->pc_server_a) bk_addrgroup_server_close(B, pc->pc_server_a);
  if (pc->pc_local_b) bk_netinfo_destroy(B,pc->pc_local_b);
  if (pc->pc_remote_b) bk_netinfo_destroy(B,pc->pc_remote_b);
  if (pc->pc_server_b) bk_addrgroup_server_close(B, pc->pc_server_b);
  if (pc->pc_run) bk_run_destroy(B, pc->pc_run);
  if (pc->pc_fd_a >= 0) close(pc->pc_fd_a);
  if (pc->pc_fd_b >= 0) close(pc->pc_fd_b);
  return;
}
