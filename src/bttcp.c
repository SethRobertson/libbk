#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: bttcp.c,v 1.30 2002/08/15 04:16:26 jtt Exp $";
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



#define ERRORQUEUE_DEPTH 	32		///< Default depth
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
#define BTTCP_FLAG_SHUTTING_DOWN_SERVER	0x01	///< We're shutting down a server
#define PC_VERBOSE			0x02	///< Verbose output
#define PC_MULTICAST			0x04	///< Multicast allowed
#define PC_BROADCAST			0x08	///< Broadcast allowed
#define PC_MULTICAST_LOOP		0x10	///< Multicast loopback
#define PC_MULTICAST_NOMEMBERSHIP	0x20	///< Multicast w/o membership
#define PC_CLOSE_AFTER_ONE		0x40	///< Close relay after one side closed
  u_int			pc_multicast_ttl;	///< Multicast ttl
  char *		pc_proto;		///< What protocol to use
  char *		pc_remoteurl;		///< Remote "url".
  char *		pc_localurl;		///< Local "url".
  struct bk_netinfo *	pc_local;		///< Local side info.
  struct bk_netinfo *	pc_remote;		///< Remote side info.
  struct bk_run	*	pc_run;			///< Run structure.
  int			pc_af;			///< Address family.
  long			pc_timeout;		///< Connection timeout
  void *		pc_server;		///< Server handle
  int			pc_buffer;		///< Buffer sizes
  int			pc_len;			///< Input size hints
};



static int proginit(bk_s B, struct program_config *pconfig);
static int connect_complete(bk_s B, void *args, int sock, struct bk_addrgroup *bag, void *server_handle, bk_addrgroup_state_e state);
static void relay_finish(bk_s B, void *args, u_int state);
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
  const struct poptOption optionsTable[] = 
  {
    {"debug", 'd', POPT_ARG_NONE, NULL, 'd', "Turn on debugging", NULL },
    {"verbose", 'v', POPT_ARG_NONE, NULL, 'v', "Turn on verbose message", NULL },
    {"no-seatbelts", 0, POPT_ARG_NONE, NULL, 0x1000, "Sealtbelts off & speed up", NULL },
    {"transmit", 't', POPT_ARG_STRING, NULL, 't', "Transmit to host", "hostspec" },
    {"receive", 'r', POPT_ARG_NONE, NULL, 'r', "Receive", NULL },
    {"local-name", 'l', POPT_ARG_STRING, NULL, 'l', "Local address to bind", "localaddr" },
    {"address-family", 0, POPT_ARG_INT, NULL, 1, "Set the address family", "address_family" },
    {"udp", 'u', POPT_ARG_NONE, NULL, 2, "Use UDP", NULL },
    {"multicast", 0, POPT_ARG_NONE, NULL, 3, "Multicast UDP packets", NULL },
    {"broadcast", 0, POPT_ARG_NONE, NULL, 4, "Broadcast UDP packets", NULL },
    {"multicast-loop", 0, POPT_ARG_NONE, NULL, 5, "Loop multicast packets to local machine", NULL },
    {"multicast-ttl", 0, POPT_ARG_INT, NULL, 6, "Set nondefault multicast ttl", "ttl" },
    {"multicast-nomembership", 0, POPT_ARG_NONE, NULL, 7, "Don't want multicast membership management", NULL },
    {"buffersize", 0, POPT_ARG_INT, NULL, 8, "Size of I/O queues", "buffer size" },
    {"length", 'l', POPT_ARG_INT, NULL, 9, "Default I/O chunk size", "default length" },
    {"close-after-one", 'c', POPT_ARG_NONE, NULL, 10, "Shut down relay after only one side closes", NULL },
    {"timeout", 'T', POPT_ARG_INT, NULL, 'T', "Set the connection timeout", "timeout" },
    POPT_AUTOHELP
    POPT_TABLEEND
  };

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD("BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, LOG_LOCAL0, 0)))
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
      bk_general_debug_config(B, stderr, BK_ERR_NONE, 0); 				// Set up debugging, from config file
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
	fprintf(stderr,"Cannot be both transmitter and receiver\n");
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
	fprintf(stderr,"Cannot be both transmitter and receiver\n");
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
      pc->pc_buffer = atoi(poptGetOptArg(optCon));
      break;

    case 9:					// Default I/O chunks
      pc->pc_len = atoi(poptGetOptArg(optCon));
      break;

    case 10:					// Close after one
      BK_FLAG_SET(pc->pc_flags, PC_CLOSE_AFTER_ONE);
      break;
    }
  }

  if (c < -1 || getopterr || !pc->pc_role)
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

  switch (pc->pc_role)
  {
  case BttcpRoleReceive:
    if (bk_netutils_start_service_verbose(B, pc->pc_run, pc->pc_localurl, BK_ADDR_ANY, DEFAULT_PORT_STR, pc->pc_proto, NULL, connect_complete, pc, 0, 0))
      bk_die(B, 1, stderr, "Could not start receiver (Port in use?)\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
    break;

  case BttcpRoleTransmit:
    if (bk_netutils_make_conn_verbose(B, pc->pc_run, pc->pc_remoteurl, NULL, DEFAULT_PORT_STR, pc->pc_localurl, NULL, NULL, pc->pc_proto, pc->pc_timeout, connect_complete, pc, 0) < 0)
      bk_die(B, 1, stderr, "Could not start transmitter (Remote not ready?)\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
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

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  switch (state)
  {
  case BkAddrGroupStateSysError:
    fprintf(stderr,"A system error occured\n");
    goto error;
    break;
  case BkAddrGroupStateRemoteError:
    fprintf(stderr,"A remote network error occured (connection refused?)\n");
    goto error;
    break;
  case BkAddrGroupStateLocalError:
    fprintf(stderr,"A local network error occured (address already in use?)\n");
    goto error;
    break;
  case BkAddrGroupStateTimeout:
    fprintf(stderr,"The connection timed out with no more addresses to try\n");
    goto error;
    break;
  case BkAddrGroupStateReady:
    pc->pc_server = server_handle;
    if (BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE))
      fprintf(stderr,"Ready and waiting\n");
    goto done;
    break;
  case BkAddrGroupStateConnected:
    if (pc->pc_server /* && ! serving_conintuously */)
    {
      BK_FLAG_SET(pc->pc_flags, BTTCP_FLAG_SHUTTING_DOWN_SERVER);
      bk_addrgroup_server_close(B, server_handle);
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
    fprintf(stderr,"Software shutdown during connection setup\n");
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
    if (BK_FLAG_ISSET(pc->pc_flags, PC_BROADCAST))
    {
      if (bk_stdsock_broadcast(B, sock, 0) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not turn on broadcast options after socket creation\n");
	BK_RETURN(B, -1);
      }
    }
    goto done;
    break;
  }

  if (bag && bk_debug_and(B, 1))
  {
    bk_debug_printf(B, "%s ==> %s\n", bk_netinfo_info(B,bag->bag_local), bk_netinfo_info(B,bag->bag_remote));
  }

  /* If we need to hold on to bag save it here */

  if (!(std_ioh = bk_ioh_init(B, fileno(stdin), fileno(stdout), NULL, NULL, pc->pc_len, pc->pc_buffer, pc->pc_buffer, pc->pc_run, BK_IOH_RAW|BK_IOH_STREAM)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create ioh on stdin/stdout\n");
    goto error;
  }
  if (!(net_ioh = bk_ioh_init(B, sock, sock, NULL, NULL, pc->pc_len, pc->pc_buffer, pc->pc_buffer, pc->pc_run, BK_IOH_RAW|BK_IOH_STREAM)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create ioh network\n");
    goto error;
  }

  if (bk_relay_ioh(B, std_ioh, net_ioh, relay_finish, pc, BK_FLAG_ISSET(pc->pc_flags,PC_CLOSE_AFTER_ONE)?BK_RELAY_IOH_DONE_AFTER_ONE_CLOSE:0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not relay my iohs\n");
    goto error;
  }

 done:
  BK_RETURN(B, 0);

 error:
  if (std_ioh) bk_ioh_close(B, std_ioh, 0);
  if (net_ioh) bk_ioh_close(B, net_ioh, 0);
  bk_run_set_run_over(B,pc->pc_run);
  BK_RETURN(B, -1);
}



/**
 * Things to do when the relay is over.
 *
 *	@param B BAKA thread/global state.
 *	@param args opaque program configuration
 *	@param state Why we are here
 */
static void
relay_finish(bk_s B, void *args, u_int state)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"bttcp");
  struct program_config *pc;

  if (!(pc = args))
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }
  
  /* <TODO> Report statistics here </TODO> */
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

  if (pc->pc_local) bk_netinfo_destroy(B,pc->pc_local);
  if (pc->pc_remote) bk_netinfo_destroy(B,pc->pc_remote);
  if (pc->pc_run) bk_run_destroy(B, pc->pc_run);
  return;
}
