#if !defined(lint) && !defined(__INSIGHT__)
#include "libbk_compiler.h"
UNUSED static const char libbk__rcsid[] = "$Id: bttcp.c,v 1.60 2006/11/29 22:55:31 seth Exp $";
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
 *
 * TODO:
 *	--socks w/bind support?
 *	--proxy (http proxy support?)
 *	--address-family for unix domain named sockets
 *	* Verify bidirectional SSL authentication, encryption w/keygen ex *
 */

#include <libbk.h>
#include <libbk_net.h>
#include <libbkssl.h>



#define ERRORQUEUE_DEPTH	32		///< Default depth
#define DEFAULT_PROTO_STR	"tcp"		///< Default protocol
#define DEFAULT_PORT_STR	"5001"		///< Default port
#define DEFAULT_BLOCK_SIZE	8192		///< Default ioh block size
#define DEFAULT_BUFFER_SIZE	1024*1024	///< Default ioh buffering
#define ANY_PORT		"0"		///< Any port is OK


static int cancel_relay = 0;


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
  bttcp_role_e		pc_role;		///< What role do I play?
  bk_flags		pc_flags;		///< Everyone needs flags.
#define BTTCP_FLAG_SHUTTING_DOWN_SERVER	0x00001	///< We're shutting down a server
#define PC_VERBOSE			0x00002	///< Verbose output
#define PC_MULTICAST			0x00004	///< Multicast allowed
#define PC_BROADCAST			0x00008	///< Broadcast allowed
#define PC_MULTICAST_LOOP		0x00010	///< Multicast loopback
#define PC_MULTICAST_NOMEMBERSHIP	0x00020	///< Multicast w/o membership
#define PC_CLOSE_AFTER_ONE		0x00040	///< Close relay after one side closed
#define PC_SSL				0x00080	///< Want SSL
#define PC_NODELAY			0x00100	///< Set nodelay socket buffer option
#define PC_KEEPALIVE			0x00200	///< Set keepalive preference
#define PC_SERVER			0x00400	///< Server mode
#define PC_EXECUTE			0x00800 ///< Execute command instead of stdio
#define PC_EXECUTE_STDERR		0x01000 ///< Dup cmd stderr to stdout
#define PC_EXECUTE_PTY			0x02000 ///< Run cmd in a pty
#define PC_RAW				0x04000 ///< Run in raw mode
#define PC_RESTORE_TERMIN		0x08000 ///< Reset input termio mode
#define PC_RESTORE_TERMOUT		0x10000 ///< Reset output termio mode
#define PC_RUN_OVER			0x20000 ///< bk_run is now over
  u_int			pc_multicast_ttl;	///< Multicast ttl
  char *		pc_proto;		///< What protocol to use
  char *		pc_remoteurl;		///< Remote "url".
  char *		pc_localurl;		///< Local "url".
  char *const*		pc_args;		///< Arguments for execution
  struct bk_netinfo *	pc_local;		///< Local side info.
  struct bk_netinfo *	pc_remote;		///< Remote side info.
  struct bk_run	*	pc_run;			///< Run structure.
  int			pc_af;			///< Address family.
  long			pc_timeout;		///< Connection timeout
  void *		pc_server;		///< Server handle
  int			pc_buffer;		///< Buffer sizes
  int			pc_len;			///< Input size hints
  int			pc_sockbuf;		///< Socket buffer size
  int			pc_fdin;		///< Input fd for artificial EOF (HACK)
  pid_t			pc_childid;		///< Child PID if exec
  struct termios	pc_termioin;		///< Original Termio stdin
  struct termios	pc_termioout;		///< Original Termio stdout
  int			pc_dupstdin;		///< Duplicate copy of stdin for restoring termios
  int			pc_dupstdout;		///< Duplicate copy of stdout for restoring termios
  u_quad_t		pc_translimit;		///< Transmit limit
  struct bk_relay_ioh_stats	pc_stats;	///< Statistics about relay
  struct timeval	pc_start;		///< Starting time
  const char	       *pc_filein;		///< Set input filename
  const char	       *pc_fileout;		///< Set output filename
  struct bk_ssl_ctx    *pc_ssl_ctx;		///< SSL session template
  const char *		pc_ssl_cert_file;	///< Location of SSL cert file
  const char *		pc_ssl_key_file;	///< Location of SSL key file
  const char *		pc_ssl_cafile;		///< Location of SSL CA file
  const char *		pc_ssl_dhparam_file;	///< Location of SSL DH params file
  struct bk_relay_cancel pc_brc;		///< User cancel struct
};



static int proginit(bk_s B, struct program_config *pconfig);
static int connect_complete(bk_s B, void *args, int sock, struct bk_addrgroup *bag, void *server_handle, bk_addrgroup_state_e state);
static void relay_finish(bk_s B, void *args, struct bk_ioh *read_ioh, struct bk_ioh *write_ioh, bk_vptr *data,  bk_flags flags);
static void cleanup(bk_s B, struct program_config *pc);
static void finish(int signum);
static int do_cancel(bk_s B, struct bk_run *run, void *opaque, volatile int *demand, const struct timeval *starttime, bk_flags flags);
static int set_run_over(bk_s B, struct program_config *pc, bk_flags flags);



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
    {"transmit", 't', POPT_ARG_STRING, NULL, 't', "Transmit to host", "proto://ip:port" },
    {"receive", 'r', POPT_ARG_NONE, NULL, 'r', "Receive", NULL },
    {"local-name", 'l', POPT_ARG_STRING, NULL, 'l', "Local address to bind", "proto://ip:port" },
#ifdef NOTYET
    {"address-family", 0, POPT_ARG_INT, NULL, 1, "Set the address family", "address_family" },
#endif /* NOTYET */
    {"udp", 'u', POPT_ARG_NONE, NULL, 2, "Use UDP", NULL },
    {"multicast", 0, POPT_ARG_NONE, NULL, 3, "Multicast UDP packets", NULL },
    {"broadcast", 0, POPT_ARG_NONE, NULL, 4, "Broadcast UDP packets", NULL },
    {"multicast-loop", 0, POPT_ARG_NONE, NULL, 5, "Loop multicast packets to local machine", NULL },
    {"multicast-ttl", 0, POPT_ARG_INT, NULL, 6, "Set nondefault multicast ttl", "ttl" },
    {"multicast-nomembership", 0, POPT_ARG_NONE, NULL, 7, "Don't want multicast membership management", NULL },
    {"buffersize", 0, POPT_ARG_STRING, NULL, 8, "Size of I/O queues", "buffer size" },
    {"length", 'L', POPT_ARG_STRING, NULL, 9, "Default I/O chunk size", "default length" },
    {"close-after-one", 'c', POPT_ARG_NONE, NULL, 10, "Shut down relay after only one side closes", NULL },
#ifndef NO_SSL
    {"ssl", 's', POPT_ARG_NONE, NULL, 's', "Use SSL", NULL },
    {"ssl-cert", 0, POPT_ARG_STRING, NULL, 11, "PEM SSL certificate location", "filename" },
    {"ssl-key", 0, POPT_ARG_STRING, NULL, 12, "PEM SSL key location", "filename" },
    {"ssl-cafile", 0, POPT_ARG_STRING, NULL, 13, "File containing acceptable CA certificates", "filename" },
    {"ssl-dhparams", 0, POPT_ARG_STRING, NULL, 14, "Use DH param file (PEM format) instead of certificate & private key", "filename"},
#endif // NO_SSL
    {"timeout", 'T', POPT_ARG_INT, NULL, 'T', "Set the connection timeout", "timeout" },
    {"sockbuf", 'B', POPT_ARG_STRING, NULL, 15, "Socket snd/rcv buffer size", "length in bytes" },
    {"nodelay", 0, POPT_ARG_NONE, NULL, 16, "Set TCP NODELAY", NULL },
    {"keepalive", 0, POPT_ARG_NONE, NULL, 17, "Set TCP KEEPALIVE", NULL },
    {"file-io", 0, POPT_ARG_STRING, NULL, 18, "Use file instead of stdio", "filename"},
    {"file-in", 0, POPT_ARG_STRING, NULL, 19, "Use file instead of stdin", "filename"},
    {"file-out", 0, POPT_ARG_STRING, NULL, 20, "Use file instead of stdout", "filename"},
    {"server", 0, POPT_ARG_NONE, NULL, 21, "Set server (multiple connection) mode", NULL },
    {"transmit-limit", 0, POPT_ARG_STRING, NULL, 22, "Limit maximum transmit size", "bytes"},
    {"execute", 0, POPT_ARG_NONE, NULL, 23, "Execute program and argument after '--'", NULL },
    {"execute-with-stderr", 0, POPT_ARG_NONE, NULL, 24, "Execute program and argument after w/stderr on stdout '--'", NULL },
    {"execute-in-pty", 0, POPT_ARG_NONE, NULL, 25, "Execute program in a pty", NULL },
    {"raw", 0, POPT_ARG_NONE, NULL, 26, "No buffer, no tty", NULL },
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
  pc->pc_multicast_ttl = 1;			// Default local net multicast
  pc->pc_timeout=BK_SECS_TO_EVENT(30);
  pc->pc_proto=DEFAULT_PROTO_STR;
  pc->pc_len=DEFAULT_BLOCK_SIZE;
  pc->pc_buffer=DEFAULT_BUFFER_SIZE;

  if (!(optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, POPT_CONTEXT_POSIXMEHARDER)))
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

    case 'r':					// Receive
      if (pc->pc_role)
      {
	fprintf(stderr,"%s: Cannot be both transmitter and receiver\n", BK_GENERAL_PROGRAM(B));
	exit(1);
      }
      pc->pc_role=BttcpRoleReceive;
      if (!pc->pc_localurl)
      {
	pc->pc_localurl=BK_ADDR_ANY;
      }
      break;

    case 't':					// Transmit
      if (pc->pc_role)
      {
	fprintf(stderr,"%s: Cannot be both transmitter and receiver\n", BK_GENERAL_PROGRAM(B));
	exit(1);
      }
      pc->pc_role=BttcpRoleTransmit;
      pc->pc_remoteurl=(char *)poptGetOptArg(optCon);
      break;

    case 'l':					// local-name
      pc->pc_localurl=(char *)poptGetOptArg(optCon);
      break;

    case 'T':					// Timeout
      pc->pc_timeout=BK_SECS_TO_EVENT(atoi(poptGetOptArg(optCon)));
      break;

    case 's':
      BK_FLAG_SET(pc->pc_flags, PC_SSL);
      break;

    case 1:					// address-family
      pc->pc_af=atoi(poptGetOptArg(optCon));
      break;

    case 2:					// protocol
      pc->pc_proto="UDP";
      BK_FLAG_SET(pc->pc_flags, PC_CLOSE_AFTER_ONE);
      break;

    case 3:					// multicast
      BK_FLAG_SET(pc->pc_flags, PC_MULTICAST);
      break;

    case 4:					// broadcast
      BK_FLAG_SET(pc->pc_flags, PC_BROADCAST);
      break;

    case 5:					// Want multicast looping
      BK_FLAG_SET(pc->pc_flags, PC_MULTICAST_LOOP);
      break;

    case 6:					// Want non-1 multicast ttl
      pc->pc_multicast_ttl = atoi(poptGetOptArg(optCon));
      break;

    case 7:					// Don't want multicast membership
      BK_FLAG_SET(pc->pc_flags, PC_MULTICAST_NOMEMBERSHIP);
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

    case 11:					// SSL Cert
      pc->pc_ssl_cert_file = poptGetOptArg(optCon);
      break;

    case 12:					// SSL Key
      pc->pc_ssl_key_file = poptGetOptArg(optCon);
      break;

    case 13:					// SSL CA File
      pc->pc_ssl_cafile = poptGetOptArg(optCon);
      break;

    case 14:					// SSL DH Params
      pc->pc_ssl_dhparam_file = poptGetOptArg(optCon);
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

    case 18:
      /*
       * I am still wondering if one RDWR open would be better But if
       * we think about a named pipe or a device, to get proper close
       * semantics (e.g. be able to notify when network in shuts down)
       * we need two true file descriptors (and not just dups).
       */
      pc->pc_filein = pc->pc_fileout = poptGetOptArg(optCon);
      break;

    case 19:
      pc->pc_filein = poptGetOptArg(optCon);
      break;

    case 20:
      pc->pc_fileout = poptGetOptArg(optCon);
      break;

    case 21:
      BK_FLAG_SET(pc->pc_flags, PC_SERVER);
      break;

    case 22:
      {
	double tmplimit = bk_string_demagnify(B, poptGetOptArg(optCon), 0);
	if (isnan(tmplimit))
	  getopterr++;
	pc->pc_translimit = tmplimit;
      }
      break;

    case 23:
      BK_FLAG_SET(pc->pc_flags, PC_EXECUTE);
      break;

    case 24:
      BK_FLAG_SET(pc->pc_flags, PC_EXECUTE|PC_EXECUTE_STDERR);
      break;

    case 25:
      BK_FLAG_SET(pc->pc_flags, PC_EXECUTE|PC_EXECUTE_PTY);
      break;

    case 26:
      BK_FLAG_SET(pc->pc_flags, PC_RAW);
      break;
    }
  }

  /*
   * Reprocess so that argc and argv contain the remaining command
   * line arguments (note argv[0] is an argument, not the program
   * name).  argc remains the number of elements in the argv array.
   */
  pc->pc_args = (char **)poptGetArgs(optCon);
  argc = 0;
  if (pc->pc_args)
    for (; pc->pc_args[argc]; argc++)
      ; // Void

  if (c < -1 || getopterr || !pc->pc_role || (BK_FLAG_ISSET(pc->pc_flags, PC_EXECUTE) && argc < 1))
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

  if (!pc->pc_af)
    pc->pc_af = AF_INET;

  if (!(pc->pc_run = bk_run_init(B, 0)))
  {
    fprintf(stderr,"Could not create run structure\n");
    goto error;
  }

  if (bk_run_on_demand_add(B, pc->pc_run, do_cancel, pc, &cancel_relay, NULL, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not add relay cancel on demand function\n");
    goto error;
  }


  // SIGPIPE is just annoying
  bk_signal(B, SIGPIPE, SIG_IGN, BK_RUN_SIGNAL_RESTART);
  bk_signal(B, SIGCHLD, bk_reaper, BK_RUN_SIGNAL_RESTART);
  bk_signal(B, SIGINT, finish, BK_RUN_SIGNAL_RESTART);

  if (BK_FLAG_ISSET(pc->pc_flags, PC_SSL))
  {
#ifndef NO_SSL
    if (bk_ssl_env_init(B) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "SSL initialization failed.\n");
      BK_RETURN(B, -1);
    }

    if (!(pc->pc_ssl_ctx = bk_ssl_create_context(B, pc->pc_ssl_cert_file, pc->pc_ssl_key_file,
						 pc->pc_ssl_dhparam_file, pc->pc_ssl_cafile, 0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Failed to create SSL context.\n");
      BK_RETURN(B, -1);
    }
#endif // NO_SSL
  }

  switch (pc->pc_role)
  {
  case BttcpRoleReceive:
    if (BK_FLAG_ISSET(pc->pc_flags, PC_SSL))
    {
#ifndef NO_SSL
      if (bk_ssl_start_service_verbose(B, pc->pc_run, pc->pc_ssl_ctx, pc->pc_localurl, BK_ADDR_ANY, DEFAULT_PORT_STR, pc->pc_proto, NULL, connect_complete, pc, 0, 0))
	bk_die(B, 1, stderr, "Could not start receiver (Port in use?)\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
#endif // NO_SSL
    }
    else
    {
      if (bk_netutils_start_service_verbose(B, pc->pc_run, pc->pc_localurl, BK_ADDR_ANY, DEFAULT_PORT_STR, pc->pc_proto, NULL, connect_complete, pc, 0, 0))
	bk_die(B, 1, stderr, "Could not start receiver (Port in use?)\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
    }
    break;

  case BttcpRoleTransmit:
    if (BK_FLAG_ISSET(pc->pc_flags, PC_SSL))
    {
#ifndef NO_SSL
      if (bk_ssl_make_conn_verbose(B, pc->pc_run, pc->pc_ssl_ctx, pc->pc_remoteurl, NULL, DEFAULT_PORT_STR, pc->pc_localurl, NULL, NULL, pc->pc_proto, pc->pc_timeout, connect_complete, pc, 0) < 0)
	bk_die(B, 1, stderr, "Could not start transmitter (Remote not ready?)\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
#endif // NO_SSL
    }
    else
    {
      if (bk_netutils_make_conn_verbose(B, pc->pc_run, pc->pc_remoteurl, NULL, DEFAULT_PORT_STR, pc->pc_localurl, NULL, NULL, pc->pc_proto, pc->pc_timeout, connect_complete, pc, 0) < 0)
	bk_die(B, 1, stderr, "Could not start transmitter (Remote not ready?)\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
    }
    break;

  default:
    bk_error_printf(B, BK_ERR_ERR, "Unknown role: %d\n", pc->pc_role);
    bk_die(B, 1, stderr, "Unknown role\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
    goto error;
    break;
  }

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
  struct program_config *pc=args;
  struct bk_ioh *std_ioh = NULL, *net_ioh = NULL;
  int one = 1;
  int infd = fileno(stdin);
  int outfd = fileno(stdout);

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  switch (state)
  {
  case BkAddrGroupStateSysError:
    fprintf(stderr,"%s%s: A system error occurred (use -d for more details)\n", BK_GENERAL_PROGRAM(B), pc->pc_role==BttcpRoleReceive?"-r":"-t");
    goto error;
    break;
  case BkAddrGroupStateRemoteError:
    fprintf(stderr,"%s%s: A remote network error occurred (connection refused--use -d for more details?)\n", BK_GENERAL_PROGRAM(B), pc->pc_role==BttcpRoleReceive?"-r":"-t");
    goto error;
    break;
  case BkAddrGroupStateLocalError:
    fprintf(stderr,"%s%s: A local network error occurred (address already in use--use -d for more details?)\n", BK_GENERAL_PROGRAM(B), pc->pc_role==BttcpRoleReceive?"-r":"-t");
    goto error;
    break;
  case BkAddrGroupStateTimeout:
    fprintf(stderr,"%s%s: The connection timed out with no more addresses to try\n", BK_GENERAL_PROGRAM(B), pc->pc_role==BttcpRoleReceive?"-r":"-t");
    goto error;
    break;
  case BkAddrGroupStateReady:
    pc->pc_server = server_handle;
    if (BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE))
      fprintf(stderr,"%s%s: Ready and listening\n", BK_GENERAL_PROGRAM(B), pc->pc_role==BttcpRoleReceive?"-r":"-t");
    goto done;
    break;
  case BkAddrGroupStateConnected:
    if (BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE))
    {
      if (pc->pc_server)
	fprintf(stderr,"%s%s: Accepted connection\n", BK_GENERAL_PROGRAM(B), pc->pc_role==BttcpRoleReceive?"-r":"-t");
      else
	fprintf(stderr,"%s%s: Connection complete\n", BK_GENERAL_PROGRAM(B), pc->pc_role==BttcpRoleReceive?"-r":"-t");
    }

    if (BK_FLAG_ISSET(pc->pc_flags, PC_SERVER))
    { 
      switch (fork())
      {
      case -1:
	bk_error_printf(B, BK_ERR_ERR, "Could not create child process to handle accepted fd: %s\n", strerror(errno));
	bk_die(B, 1, stderr, "Fork failure--assuming worst case and going away\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
	break;
      case 0:					// Child
	break;					// Continue on with normal path
      default:					// Parent
	close(sock);
	goto done;
      }
    }
    if (pc->pc_server /* && ! serving_conintuously */)
    {
      BK_FLAG_SET(pc->pc_flags, BTTCP_FLAG_SHUTTING_DOWN_SERVER);
      bk_addrgroup_server_close(B, pc->pc_server);
      pc->pc_server = NULL;
    }
    break;
  case BkAddrGroupStateClosing:
    if (BK_FLAG_ISSET(pc->pc_flags, BTTCP_FLAG_SHUTTING_DOWN_SERVER) &&
	pc->pc_server == server_handle)
    {
      pc->pc_server = NULL;			/* Very important */
      BK_FLAG_CLEAR(pc->pc_flags, BTTCP_FLAG_SHUTTING_DOWN_SERVER);
      goto done;
    }
    if (BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE))
      fprintf(stderr,"%s%s: Software shutdown during connection setup\n", BK_GENERAL_PROGRAM(B), pc->pc_role==BttcpRoleReceive?"-r":"-t");
    goto error;
    break;
  case BkAddrGroupStateSocket:
    if (BK_FLAG_ISSET(pc->pc_flags, PC_MULTICAST))
    {
      if (bk_stdsock_multicast(B, sock, pc->pc_multicast_ttl, bag->bag_remote->bni_addr,
		       BK_FLAG_ISSET(pc->pc_flags, PC_MULTICAST_LOOP)?BK_MULTICAST_WANTLOOP:0 |
		       BK_FLAG_ISSET(pc->pc_flags, PC_MULTICAST_NOMEMBERSHIP)?BK_MULTICAST_NOJOIN:0) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not turn on multicast options after socket creation\n");
	BK_RETURN(B, -1);
      }
    }
    else if (pc->pc_multicast_ttl > 1)
    {
      // OK this is a stupid overloading hack, but I don't think it is a generally useful option
      if (setsockopt(sock, IPPROTO_IP, IP_TTL, &pc->pc_multicast_ttl, sizeof(pc->pc_multicast_ttl)) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not set IP ttl preference to %d: %s\n", pc->pc_multicast_ttl, strerror(errno));
	BK_RETURN(B, -1);
      }
    }
    if (BK_FLAG_ISSET(pc->pc_flags, PC_BROADCAST))
    {
      if (bk_stdsock_broadcast(B, sock, 0) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not turn on broadcast options after socket creation\n");
	BK_RETURN(B, -1);
      }
    }
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

  if (bag && bk_debug_and(B, 1))
  {
    bk_debug_printf(B, "%s ==> %s\n", bk_netinfo_info(B,bag->bag_local), bk_netinfo_info(B,bag->bag_remote));
  }

  /* If we need to hold on to bag save it here */

  if (pc->pc_filein)
  {
    if ((infd = open(pc->pc_filein,O_RDONLY,0666)) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not open input file %s: %s\n", pc->pc_filein, strerror(errno));
      goto error;
    }
  }

  if (pc->pc_fileout)
  {
    if ((outfd = open(pc->pc_fileout,O_WRONLY|O_CREAT|O_TRUNC,0666)) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not open output file %s: %s\n", pc->pc_fileout, strerror(errno));
      goto error;
    }
  }

  if (BK_FLAG_ISSET(pc->pc_flags, PC_EXECUTE))
  {
    if ((pc->pc_childid = bk_pipe_to_exec(B, &infd, &outfd, *pc->pc_args, pc->pc_args, NULL, (BK_EXEC_FLAG_CLOSE_CHILD_DESC|BK_EXEC_FLAG_SEARCH_PATH|
											      (BK_FLAG_ISSET(pc->pc_flags, PC_EXECUTE_STDERR)?BK_EXEC_FLAG_STDERR_ON_STDOUT:0)|
											      (BK_FLAG_ISSET(pc->pc_flags, PC_EXECUTE_PTY)?BK_EXEC_FLAG_USE_PTY:0)))) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not execute/pipe\n");
      goto error;
    }
  }

  if (BK_FLAG_ISSET(pc->pc_flags, PC_RAW))
  {
    if (isatty(infd))
    {
      struct termios curios;
      tcgetattr(infd, &curios);
      memcpy(&pc->pc_termioin, &curios, sizeof(curios));
      cfmakeraw(&curios);
      tcsetattr(infd, TCSANOW, &curios);
      tcsetattr(infd, TCSANOW, &curios);
      if (infd == fileno(stdin))
      {
	BK_FLAG_SET(pc->pc_flags, PC_RESTORE_TERMIN);
	pc->pc_dupstdin = dup(infd);
      }
    }
    if (isatty(outfd))
    {
      struct termios curios;
      tcgetattr(outfd, &curios);
      memcpy(&pc->pc_termioout, &curios, sizeof(curios));
      cfmakeraw(&curios);
      tcsetattr(outfd, TCSANOW, &curios);
      tcsetattr(outfd, TCSANOW, &curios);
      if (outfd == fileno(stdout))
      {
	BK_FLAG_SET(pc->pc_flags, PC_RESTORE_TERMOUT);
	pc->pc_dupstdout = dup(outfd);
      }
    }
  }


  pc->pc_fdin = infd;
  if (!(std_ioh = bk_ioh_init(B, infd, outfd, NULL, NULL, pc->pc_len, pc->pc_buffer, pc->pc_buffer, pc->pc_run, BK_IOH_RAW|BK_IOH_STREAM)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create ioh on stdin/stdout\n");
    goto error;
  }

  if (BK_FLAG_ISSET(pc->pc_flags, PC_SSL))
  {
#ifndef NO_SSL
    if (!(net_ioh = bk_ssl_ioh_init(B, bag->bag_ssl, sock, sock, NULL, NULL, pc->pc_len, pc->pc_buffer, pc->pc_buffer, pc->pc_run, BK_IOH_RAW|BK_IOH_STREAM)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create ioh network\n");
      if (bag->bag_ssl)
      {
	bk_ssl_destroy(B, bag->bag_ssl, 0);
	bag->bag_ssl = NULL;
      }
      goto error;
    }
    bag->bag_ssl = NULL;
#endif // NO_SSL
  }
  else
  {
    if (!(net_ioh = bk_ioh_init(B, sock, sock, NULL, NULL, pc->pc_len, pc->pc_buffer, pc->pc_buffer, pc->pc_run, BK_IOH_RAW|BK_IOH_STREAM)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create ioh network\n");
      goto error;
    }
  }

  gettimeofday(&pc->pc_start, NULL);

  if (bk_relay_ioh(B, std_ioh, net_ioh, relay_finish, pc, &pc->pc_stats, &pc->pc_brc, BK_FLAG_ISSET(pc->pc_flags,PC_CLOSE_AFTER_ONE)?BK_RELAY_IOH_DONE_AFTER_ONE_CLOSE:0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not relay my iohs\n");
    goto error;
  }

 done:
  BK_RETURN(B, 0);

 error:
  if (std_ioh) bk_ioh_close(B, std_ioh, 0);
  if (net_ioh) bk_ioh_close(B, net_ioh, 0);
  set_run_over(B, pc, 0);
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

  // We only care about shutdown
  if (data && !pc->pc_translimit)
    BK_VRETURN(B);

  if (data)
  {
    // <TRICKY>Assumption about data read and callback order</TRICKY>
    if (pc->pc_stats.side[0].birs_readbytes >= pc->pc_translimit)
    {						// Implement artificial EOF
      int newfd;
      // stupid hack to get an EOF...switch the FD from underneath the IOH
      close(pc->pc_fdin);
      newfd = open(_PATH_DEVNULL, O_RDONLY, 0666);
      if (newfd < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Good grief! Cannot open %s: %s\n",
			_PATH_DEVNULL, strerror(errno));
	BK_VRETURN(B);
      }
      if (newfd != pc->pc_fdin)
      {
	dup2(newfd, pc->pc_fdin);
	close(newfd);
      }

      // Pretend we read exactly the right amount
      data->len -= pc->pc_stats.side[0].birs_readbytes - pc->pc_translimit;
    }
    BK_VRETURN(B);
  }

  if (BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE))
  {
    gettimeofday(&end, NULL);
    BK_TV_SUB(&delta, &end, &pc->pc_start);

    bk_string_magnitude(B, (double)pc->pc_stats.side[0].birs_writebytes/((double)delta.tv_sec + (double)delta.tv_usec/1000000.0), 3, "B/s", speedin, sizeof(speedin), 0);
    bk_string_magnitude(B, (double)pc->pc_stats.side[1].birs_writebytes/((double)delta.tv_sec + (double)delta.tv_usec/1000000.0), 3, "B/s", speedout, sizeof(speedout), 0);

    fprintf(stderr, "%s%s: %llu bytes received in %ld.%06ld seconds: %s\n", BK_GENERAL_PROGRAM(B), pc->pc_role==BttcpRoleReceive?"-r":"-t", BUG_LLU_CAST(pc->pc_stats.side[0].birs_writebytes), (long int) delta.tv_sec, (long int) delta.tv_usec, speedin);
    fprintf(stderr, "%s%s: %llu bytes transmitted in %ld.%06ld seconds: %s\n", BK_GENERAL_PROGRAM(B), pc->pc_role==BttcpRoleReceive?"-r":"-t", BUG_LLU_CAST(pc->pc_stats.side[1].birs_writebytes), (long int) delta.tv_sec, (long int) delta.tv_usec, speedout);
  }

  if (pc->pc_childid > 0)
    kill(-pc->pc_childid, SIGTERM);

  if (BK_FLAG_ISSET(pc->pc_flags, PC_RESTORE_TERMOUT))
  {
    tcsetattr(pc->pc_dupstdout, TCSANOW, &pc->pc_termioout);
    tcsetattr(pc->pc_dupstdout, TCSANOW, &pc->pc_termioout);
    tcsetattr(pc->pc_dupstdout, TCSANOW, &pc->pc_termioout);
  }

  if (BK_FLAG_ISSET(pc->pc_flags, PC_RESTORE_TERMIN))
  {
    tcsetattr(pc->pc_dupstdin, TCSANOW, &pc->pc_termioin);
    tcsetattr(pc->pc_dupstdin, TCSANOW, &pc->pc_termioin);
    tcsetattr(pc->pc_dupstdin, TCSANOW, &pc->pc_termioin);
  }

  set_run_over(B, pc, 0);
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

  if (BK_FLAG_ISSET(pc->pc_flags, PC_SSL))
  {
#ifndef NO_SSL
    if (pc->pc_ssl_ctx)
    {
      bk_ssl_destroy_context(B, pc->pc_ssl_ctx);
    }
    bk_ssl_env_destroy(B);
#endif // NO_SSL
  }

  if (pc->pc_local) bk_netinfo_destroy(B,pc->pc_local);
  if (pc->pc_remote) bk_netinfo_destroy(B,pc->pc_remote);
  if (pc->pc_run) bk_run_destroy(B, pc->pc_run);
  return;
}



/**
 * Sigchild reaper signal handler
 *
 * THREADS: MT-SAFE
 *
 *	@param signal Signal number
 */
static void finish(int signum)
{
  cancel_relay = 1;
  return;
}



/**
 * Type for on demand functions
 *
 *	@param B BAKA thread/global state
 *	@param run The @a bk_run structure to use.
 *	@param opaque User args passed back.
 *	@param demand The flag which when raised causes this function to run.
 *	@param starttime The start time of the latest invocation of @a bk_run_once.
 *	@param flags Flags for your enjoyment.
 */
static int 
do_cancel(bk_s B, struct bk_run *run, void *opaque, volatile int *demand, const struct timeval *starttime, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"bttcp");
  struct program_config *pc = opaque;

  if (!run || !pc || !demand)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!*demand) // Protect against bizzare recursion (shouldn't happen but does)
    BK_RETURN(B, 0);    

  *demand = 0;

  if (pc->pc_server)
  {
    bk_addrgroup_server_close(B, pc->pc_server);
    pc->pc_server = NULL;
  }
    
  if (pc->pc_brc.brc_ioh1)
  {
    if (bk_relay_cancel(B, &pc->pc_brc, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not cancel relay\n");
      goto error;
    }
  }

  if (set_run_over(B, pc, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not stop bk_run\n");
    goto error;
  }

  BK_RETURN(B, 0);  

 error:
  BK_RETURN(B, -1);  
}



/**
 * Set the bk_run_set_run_over state.
 *
 *	@param B BAKA thread/global state.
 *	@param pc The program config
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
set_run_over(bk_s B, struct program_config *pc, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"bttcp");

  if (!pc || !pc->pc_run)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (BK_FLAG_ISSET(pc->pc_flags, PC_RUN_OVER))
    BK_RETURN(B, 0);    

  if (bk_run_set_run_over(B, pc->pc_run) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set run over\n");
    goto error;
  }
  
  BK_FLAG_SET(pc->pc_flags, PC_RUN_OVER);

  BK_RETURN(B, 0);  
  
 error:
  BK_RETURN(B, -1);  
}
