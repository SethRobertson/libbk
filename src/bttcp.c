#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: bttcp.c,v 1.10 2001/11/20 21:07:24 jtt Exp $";
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
 * The state of our progress to a fully connected state
 */
typedef enum
{
  BDTTCP_INIT_STATE_RESOLVE_START=0,		///< Initial
  BDTTCP_INIT_STATE_RESOLVE_REMOTE,		///< Attempting to resolve remote hostname
  BDTTCP_INIT_STATE_RESOLVE_LOCAL,		///< Attempting to resolve local hostname
  BDTTCP_INIT_STATE_CONNECT,			///< Attempting to perform async() connect
  BDTTCP_INIT_STATE_DONE,			///< Fully connected--done with state engine
} bttcp_init_state_t;



/**
 * Information about basic program runtime configuration
 * which must be passed around.
 */
struct program_config
{
  bttcp_role_t		pc_role;		///< What role do I play?
  bk_flags		pc_flags;		///< Everyone needs flags.
  char *		pc_remname;		///< Remote "url".
  char *		pc_remport;		///< Remote "url".
  char *		pc_remproto;		///< Remote "url".
  char *		pc_localurl;		///< Local "url".
  bttcp_init_state_t	pc_init_cur_state;	///< Initialization state.
  bttcp_init_state_t	pc_init_next_state;	///< Initialization state.
  struct bk_netinfo *	pc_local;		///< Local side info.
  struct bk_netinfo *	pc_remote;		///< Remote side info.
  struct bk_run	*	pc_run;			///< Run structure.
  int			pc_af;			///< Address family.
  long			pc_timeout;		///< Connection timeout
};



static int proginit(bk_s B, struct program_config *pconfig);
static void progrun(bk_s B, struct program_config *pconfig);
static int parse_host_specifier(bk_s B, const char *url, char **name, char **port, char **proto);
static int init_state(bk_s B, struct program_config *pc);
static void remote_name(bk_s B, struct bk_run *run, struct hostent **h, struct bk_netinfo *bni, void *args);
static void local_name(bk_s B, struct bk_run *run, struct hostent **h, struct bk_netinfo *bni, void *args);
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
  BK_ENTRY(B, __FUNCTION__, __FILE__, "SIMPLE");
  char c;
  int getopterr=0;
  extern char *optarg;
  extern int optind;
  struct program_config Pconfig, *pc=NULL;
  poptContext optCon=NULL;
  const struct poptOption optionsTable[] = 
  {
    {"debug", 'd', POPT_ARG_NONE, NULL, 'd', "Turn on debugging", NULL },
    {"transmit", 't', POPT_ARG_STRING, NULL, 't', "Transmit to host", NULL },
    {"receive", 'r', POPT_ARG_NONE, NULL, 'r', "Receive", NULL },
    {"local-name", 'l', POPT_ARG_STRING, NULL, 'l', "Local address to bind", NULL },
    {"address-family", 'f', POPT_ARG_INT, NULL, 'f', "Set the address family", NULL },
    {"timeout", 'T', POPT_ARG_INT, NULL, 'T', "Set the connection timeout", NULL },
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
      parse_host_specifier(B, poptGetOptArg(optCon), (char **)&pc->pc_remname,(char **)&pc->pc_remport,(char **)&pc->pc_remproto);
      pc->pc_role=BTTCP_ROLE_TRANSMIT;
      break;
    case 'l':
      pc->pc_localurl=(char *)poptGetOptArg(optCon);
      break;
    case 'f':
      pc->pc_af=atoi(poptGetOptArg(optCon));
      break;
    case 'T':
      pc->pc_timeout=BK_TV_SECTOUSEC(atoi(poptGetOptArg(optCon)));
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
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");

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

  pc->pc_init_cur_state=BDTTCP_INIT_STATE_RESOLVE_START;
  pc->pc_init_next_state=BDTTCP_INIT_STATE_RESOLVE_REMOTE;

  if (!(pc->pc_local=bk_netinfo_create(B)))
  {
    fprintf(stderr,"Could not create local bk_netaddr structure\n");
    goto error;
  }

  if (pc->pc_remname && (!(pc->pc_remote=bk_netinfo_create(B))))
  {
    fprintf(stderr,"Could not create remote bk_netaddr structure\n");
    goto error;
  }

  /* Set protocol before starting */
  if (!pc->pc_remproto && !(pc->pc_remproto=strdup(DEFAULT_PROTO_STR)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup default proto str: %s\n", strerror(errno));
    goto error;
  }

  if (pc->pc_remport &&bk_getservbyfoo(B, (char *)pc->pc_remport, (char *)pc->pc_remproto, NULL , pc->pc_remote,0)<0)
  {
    fprintf(stderr,"Could not set remote port\n");
    goto error;
  }
 
 if (bk_getprotobyfoo(B, (char *)pc->pc_remproto, NULL , pc->pc_remote,0)<0)
  {
    fprintf(stderr,"Could not set remote proto\n");
    goto error;
  }

  init_state(B, pc);
  
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
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");

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





#define PROTO_SEPARATOR "://"
#define PORT_SEPARATOR ":"

/**
 * Parse out a 'url'. 
 *	@param B BAKA thread/global state.
 *	@param url "URL" to parse.
 *	@param name copyout host name/ip string.
 *	@param port copyout port string.
 *	@param proto copyout protcol string.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
parse_host_specifier(bk_s B, const char *url, char **name, char **port, char **proto)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
  char *tmp;
  char *p;
  char *tmp_name;
  
  if (!url || !name || !port || !proto)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  *name=NULL;
  *port=NULL;
  *proto=NULL;

  if (!(tmp=strdup(url)))
  {
    perror("failed to strdup url");
    exit(1);
  }

  if ((p=strstr(tmp,PROTO_SEPARATOR)))
  {
    *p='\0';
    *proto=strdup(tmp);
    tmp_name=p+strlen(PROTO_SEPARATOR);
  }
  else
  {
    *proto=strdup(DEFAULT_PROTO_STR);
    tmp_name=tmp;
  }
  
  if (!*proto)
  {
    perror("Could not strdup proto");
    exit(1);
  }
  
  if ((p=strstr(tmp_name,PORT_SEPARATOR)))
  {
    *p++='\0';
    *port=strdup(p);
  }
  else
  {
    *port=strdup(DEFAULT_PORT_STR);
  }

  if (!*port)
  {
    perror("Could not strdup port");
    exit(1);
  }

  if (!(*name=strdup(tmp_name)))
  {
    perror("Could not strdup host name/ip");
    exit(1);
  }
  free(tmp);
  BK_RETURN(B,0);
  
}



/**
 * Initialization state engine. This is required to accomplish various
 * initialzization routines which are asychronous (like connect)
 *
 *	@param B BAKA thread/global state.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int 
init_state(bk_s B, struct program_config *pc)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (pc->pc_init_cur_state > pc->pc_init_next_state)
  {
    fprintf(stderr,"Current state exceeds next state. Programmer error\n");
    exit(1);
  }

  switch(pc->pc_init_next_state)
  {
  case BDTTCP_INIT_STATE_RESOLVE_REMOTE:
    pc->pc_init_cur_state=BDTTCP_INIT_STATE_RESOLVE_REMOTE;
    if (pc->pc_remname)
    {
      if (bk_gethostbyfoo(B, (char *)pc->pc_remname, pc->pc_af, NULL, pc->pc_remote, pc->pc_run, remote_name, pc, 0)<0)
      {
	fprintf(stderr,"Could not resolv remote hostname\n");
	goto error;
      }
    }
    else
    {
      pc->pc_init_next_state=BDTTCP_INIT_STATE_RESOLVE_LOCAL;
      init_state(B,pc);
    }
    break;
  case BDTTCP_INIT_STATE_RESOLVE_LOCAL:
    pc->pc_init_next_state=BDTTCP_INIT_STATE_CONNECT;
    init_state(B,pc);
    break;
  case BDTTCP_INIT_STATE_CONNECT:
    pc->pc_init_cur_state=BDTTCP_INIT_STATE_CONNECT;
    if (pc->pc_role==BTTCP_ROLE_RECEIVE)
    {
      if (bk_netutils_start_service(B, pc->pc_run, pc->pc_localurl, BK_ADDR_ANY, "5001", "tcp", NULL, connect_complete, pc, 0, BK_ADDRGROUP_FLAG_WANT_ADDRGROUP))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not start service\n");
	goto error;
      }
    }
    else if (bk_net_init(B, pc->pc_run, pc->pc_local, pc->pc_remote, pc->pc_timeout, 0, connect_complete, pc, BK_ADDRGROUP_FLAG_WANT_ADDRGROUP)<0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not begin connect\n");
      goto error;
    }
    pc->pc_init_next_state=BDTTCP_INIT_STATE_DONE;
    break;
  case BDTTCP_INIT_STATE_DONE:
    pc->pc_init_cur_state=BDTTCP_INIT_STATE_DONE;
    break;
  default:
    fprintf(stderr,"Unknown init state: %d\n", pc->pc_init_next_state);
    exit(1);
    break;
  }
  BK_RETURN(B,0);

 error:
  bk_run_set_run_over(B, pc->pc_run);
  BK_RETURN(B,-1);
}




/**
 * We have found the remote hostname. Do whatever and call the state machine.
 *	@param B BAKA thread/global state.
 *	@param run The @a bk_run passed up.
 *	@param h @a hostent copyout pointer. 
 *	@param bni @a bk_netinfo which is being updated.
 *	@param args @a program_config pointer
 */
static void
remote_name(bk_s B, struct bk_run *run, struct hostent **h, struct bk_netinfo *bni, void *args)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
  struct program_config *pc;

  /* h should be null */
  if (!run || h || !bni || !(pc=args))
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  bk_netinfo_set_primary_address(B, bni, NULL);
  pc->pc_remote=bni;
  pc->pc_init_next_state=BDTTCP_INIT_STATE_RESOLVE_LOCAL;
  init_state(B, pc);
  BK_VRETURN(B);
}



/**
 * We have found the local hostname. Do whatever and set on demand flag.
 *	@param B BAKA thread/global state.
 *	@param run The @a bk_run passed up.
 *	@param h @a hostent copyout pointer. 
 *	@param bni @a bk_netinfo which is being updated.
 *	@param args @a program_config pointer
 */
static void
local_name(bk_s B, struct bk_run *run, struct hostent **h, struct bk_netinfo *bni, void *args)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
  struct program_config *pc;

  /* h should be null */
  if (!run || h || !bni || !(pc=args))
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  bk_netinfo_set_primary_address(B, bni, NULL);
  pc->pc_local=bni;
  pc->pc_init_next_state=BDTTCP_INIT_STATE_CONNECT;
  init_state(B, pc);
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
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
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
    fprintf(stderr,"Ready and waiting\n");
    BK_VRETURN(B);
    break;
  case BK_ADDRGROUP_STATE_NEWCONNECTION:
    break;
  case BK_ADDRGROUP_STATE_ABORT:
  case BK_ADDRGROUP_STATE_CLOSING:
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
  }


  if (state != BK_ADDRGROUP_STATE_NEWCONNECTION)
  {
    goto error;
  }

  fprintf(stderr, "%s ==> %s\n", bk_netinfo_info(B,bag->bag_local), bk_netinfo_info(B,bag->bag_remote));

  /* XXX If we need to hold on to bag save it here but for now */
  bk_addrgroup_destroy(B,bag);
  bag=NULL;

  fflush(stdin);
  fflush(stdout);
  if (!(std_ioh=bk_ioh_init(B, fileno(stdin), fileno(stdout), bk_ioh_stdrdfun, bk_ioh_stdwrfun, NULL, NULL, 0, 0, 0, pc->pc_run, BK_IOH_RAW|BK_IOH_STREAM)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create ioh on stdin/stdout\n");
    goto error;
  }
  if (!(net_ioh=bk_ioh_init(B, sock, sock, bk_ioh_stdrdfun, bk_ioh_stdwrfun, NULL, NULL, 0, 0, 0, pc->pc_run, BK_IOH_RAW|BK_IOH_STREAM)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create ioh network\n");
    goto error;
  }

  if (bk_relay_ioh(B, std_ioh, net_ioh, relay_finish, pc, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not relay my iohs\n");
    goto error;
  }

  BK_VRETURN(B);

 error:
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
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
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
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (pc->pc_remname) free(pc->pc_remname);
  if (pc->pc_remport) free(pc->pc_remport);
  if (pc->pc_remproto) free(pc->pc_remproto);
  if (pc->pc_local) bk_netinfo_destroy(B,pc->pc_local);
  if (pc->pc_remote) bk_netinfo_destroy(B,pc->pc_remote);
  if (pc->pc_run) bk_run_destroy(B, pc->pc_run);
  return;
}
