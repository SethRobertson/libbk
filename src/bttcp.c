#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: bttcp.c,v 1.16 2001/11/29 17:39:56 jtt Exp $";
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
  BTTCP_ROLE_TRANSMIT=1,			///< We are transmitter (connect)
  BTTCP_ROLE_RECEIVE				///< We are receiver (accept)
} bttcp_role_t;



/**
 * Information about basic program runtime configuration
 * which must be passed around.
 */
struct program_config
{
  bttcp_role_t		pc_role;		///< What role do I play?
  bk_flags		pc_flags;		///< Everyone needs flags.
#define BTTCP_FLAG_SHUTTING_DOWN_SERVER	0x1	///< We're shutting down a server
  char *		pc_remoteurl;		///< Remote "url".
  char *		pc_localurl;		///< Local "url".
  struct bk_netinfo *	pc_local;		///< Local side info.
  struct bk_netinfo *	pc_remote;		///< Remote side info.
  struct bk_run	*	pc_run;			///< Run structure.
  int			pc_af;			///< Address family.
  long			pc_timeout;		///< Connection timeout
  void *		pc_server;		///< Server handle
};



static int proginit(bk_s B, struct program_config *pconfig);
static void progrun(bk_s B, struct program_config *pconfig);
static void connect_complete(bk_s B, void *args, int sock, struct bk_addrgroup *bag, void *server_handle, bk_flags flags);
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
  BK_ENTRY(B, __FUNCTION__, __FILE__, "bttcp");
  char c;
  int getopterr=0;
  extern char *optarg;
  extern int optind;
  struct program_config Pconfig, *pc=NULL;
  poptContext optCon=NULL;
  const struct poptOption optionsTable[] = 
  {
    {"debug", 'd', POPT_ARG_NONE, NULL, 'd', "Turn on debugging", NULL },
    {"transmit", 't', POPT_ARG_STRING, NULL, 't', "Transmit to host", "hostspec" },
    {"receive", 'r', POPT_ARG_NONE, NULL, 'r', "Receive", NULL },
    {"local-name", 'l', POPT_ARG_STRING, NULL, 'l', "Local address to bind", "localaddr" },
    {"address-family", 0, POPT_ARG_INT, NULL, 1, "Set the address family", "address_family" },
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

  pc->pc_timeout=BK_TV_SECTOUSEC(30);

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
      bk_general_debug_config(B, stderr, BK_ERR_NONE, 0);
      bk_debug_printf(B, "Debugging on\n");
      break;
    case 'r':
      if (pc->pc_role)
      {
	fprintf(stderr,"Cannot be both transmitter and receiver\n");
	exit(1);
      }
      pc->pc_role=BTTCP_ROLE_RECEIVE;
      if (!pc->pc_localurl)
      {
	pc->pc_localurl=BK_ADDR_ANY;
      }
      break;
    case 't':
      if (pc->pc_role)
      {
	fprintf(stderr,"Cannot be both transmitter and receiver\n");
	exit(1);
      }
      pc->pc_role=BTTCP_ROLE_TRANSMIT;
      pc->pc_remoteurl=(char *)poptGetOptArg(optCon);
      break;
    case 'l':
      pc->pc_localurl=(char *)poptGetOptArg(optCon);
      break;
    case 'T':
      pc->pc_timeout=BK_TV_SECTOUSEC(atoi(poptGetOptArg(optCon)));
      break;
    case 1:
      pc->pc_af=atoi(poptGetOptArg(optCon));
      break;
    default:
      getopterr++;
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
    bk_exit(B,254);
  }
    
  if (proginit(B, pc) < 0)
  {
    bk_die(B,254,stderr,"Could not perform program initialization\n",0);
  }

  progrun(B, pc);
  cleanup(B,pc);
  bk_exit(B,0);
  abort();
  return(255);
  return (0);
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
  
  if (!pc->pc_af) pc->pc_af=AF_INET;
    
  if (!(pc->pc_run=bk_run_init(B, 0)))
  {
    fprintf(stderr,"Could not create run structure\n");
    goto error;
  }

  switch (pc->pc_role)
  {
  case BTTCP_ROLE_RECEIVE:
    if (bk_netutils_start_service(B, pc->pc_run, pc->pc_localurl, BK_ADDR_ANY, DEFAULT_PORT_STR, DEFAULT_PROTO_STR, NULL, connect_complete, pc, 0, BK_ADDRGROUP_FLAG_WANT_ADDRGROUP))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not start service\n");
      goto error;
    }
    break;

  case BTTCP_ROLE_TRANSMIT:
    if (bk_netutils_make_conn(B, pc->pc_run, pc->pc_remoteurl, NULL, DEFAULT_PORT_STR, pc->pc_localurl, NULL, NULL, DEFAULT_PROTO_STR, pc->pc_timeout, connect_complete, pc, BK_ADDRGROUP_FLAG_WANT_ADDRGROUP)<0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not start connection\n");
      goto error;
    }
    break;

  default: 
    fprintf(stderr,"Uknown role: %d\n", pc->pc_role);
    goto error;
    break;
  }
  
  BK_RETURN(B, 0);

 error:
  BK_RETURN(B,-1);
  
}



/**
 * Normal processing of program
 *
 *	@param B BAKA Thread/Global configuration
 *	@param pc Program configuration
 *	@return <i>0</i> Success--program may terminate normally
 *	@return <br><i>-1</i> Total terminal failure
 */
static void 
progrun(bk_s B, struct program_config *pc)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"bttcp");

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_VRETURN(B);
  }

  if (bk_run_run(B,pc->pc_run, 0)<0)
  {
    fprintf(stderr,"bk_run_run failed\n");
    exit(1);
  }
  
  BK_VRETURN(B);
}



/**
 * What to do when connection completes.
 *	@param B BAKA thread/global state.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static void 
connect_complete(bk_s B, void *args, int sock, struct bk_addrgroup *bag, void *server_handle, bk_addrgroup_state_t state)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"bttcp");
  struct program_config *pc;
  struct bk_ioh *std_ioh=NULL, *net_ioh=NULL;

  if (!(pc=args))
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  switch (state)
  {
  case BK_ADDRGROUP_STATE_READY:
    pc->pc_server=server_handle;
    fprintf(stderr,"Ready and waiting\n");
    goto done;
    break;
  case BK_ADDRGROUP_STATE_NEWCONNECTION:
    if (pc->pc_server /* && ! serving_conintuously */)
    {
      BK_FLAG_SET(pc->pc_flags, BTTCP_FLAG_SHUTTING_DOWN_SERVER);
      bk_addrgroup_server_close(B, pc->pc_server);
    }
    break;
  case BK_ADDRGROUP_STATE_ABORT:
    fprintf(stderr,"Software abort\n");
    break;
  case BK_ADDRGROUP_STATE_CLOSING:
    if (BK_FLAG_ISSET(pc->pc_flags, BTTCP_FLAG_SHUTTING_DOWN_SERVER) &&
	pc->pc_server == server_handle)
    {
      pc->pc_server=NULL;			/* Very important */
      fprintf(stderr,"Clean server close\n");
      BK_FLAG_CLEAR(pc->pc_flags, BTTCP_FLAG_SHUTTING_DOWN_SERVER);
      goto done;
    }
    fprintf(stderr,"Software shutdown during connection setup\n");
    break;
  case BK_ADDRGROUP_STATE_TIMEOUT:
    fprintf(stderr,"Connection timedout\n");
    break;
  case BK_ADDRGROUP_STATE_WIRE_ERROR:
    fprintf(stderr,"I/O Error\n");
    break;
  case BK_ADDRGROUP_STATE_BAD_ADDRESS:
    fprintf(stderr,"Bad address\n");
    break;
  case BK_ADDRGROUP_STATE_NULL:
    break;
  case BK_ADDRGROUP_STATE_CONNECTING:
    fprintf(stderr,"How did we get an connecting state?!\n");
    exit(1);
    break;
  case BK_ADDRGROUP_STATE_ACCEPTING:
    fprintf(stderr,"How did we get an accepting state?!\n");
    exit(1);
    break;
  default:
    fprintf(stderr, "Unknown as state: %d\n", state);
    exit(1);
    break;
  }


  if (state != BK_ADDRGROUP_STATE_NEWCONNECTION)
  {
    goto error;
  }

  fprintf(stderr, "%s ==> %s\n", bk_netinfo_info(B,bag->bag_local), bk_netinfo_info(B,bag->bag_remote));

  /* XXX If we need to hold on to bag save it here but for now */

  fflush(stdin);
  fflush(stdout);
  if (!(std_ioh=bk_ioh_init(B, fileno(stdin), fileno(stdout), NULL, NULL, 0, 0, 0, pc->pc_run, BK_IOH_RAW|BK_IOH_STREAM)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create ioh on stdin/stdout\n");
    goto error;
  }
  if (!(net_ioh=bk_ioh_init(B, sock, sock, NULL, NULL, 0, 0, 0, pc->pc_run, BK_IOH_RAW|BK_IOH_STREAM)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create ioh network\n");
    goto error;
  }

  if (bk_relay_ioh(B, std_ioh, net_ioh, relay_finish, pc, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not relay my iohs\n");
    goto error;
  }

 done:
  if (bag) bk_addrgroup_destroy(B, bag);
  BK_VRETURN(B);

 error:
  if (bag) bk_addrgroup_destroy(B, bag);
  if (std_ioh) bk_ioh_destroy(B, std_ioh);
  if (net_ioh) bk_ioh_destroy(B, net_ioh);
  bk_run_set_run_over(B,pc->pc_run);
  BK_VRETURN(B);
}



/**
 * Things to do when the relay is over.
 *	@param B BAKA thread/global state.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static void
relay_finish(bk_s B, void *args, u_int state)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"bttcp");
  struct program_config *pc;

  if (!(pc=args))
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }
  
  
  /* XXX Report statistics here */
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
