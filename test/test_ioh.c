#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: test_ioh.c,v 1.2 2001/11/13 06:46:52 seth Exp $";
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
 * Test basic ioh operation--*not* a good prototype ioh user
 */
#include <libbk.h>


#define ERRORQUEUE_DEPTH 32			///< Default depth
#define PORT 4999				///< Port to use for network I/O



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
  const char * 		pc_remhost;		///< The host to connect to
  int			pc_port;		///< Port number to use
  int			pc_input_hint;		///< IOH input hint
  int			pc_input_max;		///< IOH input max
  int			pc_output_max;		///< IOH output max
  u_int			pc_ioh_mode;		///< IOH message mode
#define PC_MODE_RAW	1			///< Raw messaging mode for IOH
#define PC_MODE_BLOCK	2			///< Block messaging mode for IOH
#define PC_MODE_VECTOR	3			///< Vectored messaging mode for IOH
#define PC_MODE_LINE	4			///< Line messaging mode for IOH
  bk_flags 		pc_flags;		///< Flags are fun!
#define PC_VERBOSE	1			///< Verbose output
};



int proginit(bk_s B, struct program_config *pconfig);
static void relayer(bk_s B, bk_vptr data[], void *opaque, struct bk_ioh *ioh_in, u_int state_flags);
static int create_relay(bk_s B, struct program_config *pconfig, int fd1in, int fd1out, int fd2in, int fd2out, bk_flags flags);
static void rmt_acceptor(bk_s B, struct bk_run *run, u_int fd, u_int gottypes, void *opaque, struct timeval starttime);
static void address_resolved(bk_s B, struct program_config *pconfig, struct in_addr *himaddr);



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
  BK_ENTRY(B, __FUNCTION__, __FILE__, "IOHTEST");
  char c;
  int getopterr=0;
  extern char *optarg;
  extern int optind;
  struct program_config Pconfig, *pconfig=NULL;
  poptContext optCon=NULL;
  const struct poptOption optionsTable[] = 
  {
    {"debug", 'd', POPT_ARG_NONE, NULL, 'd', "Turn on debugging", NULL },
    {"port", 'p', POPT_ARG_INT, NULL, 'p', "Port to communicate over", NULL },
    {"transmit", 't', POPT_ARG_STRING, NULL, 't', "Connect to a remote test_ioh server", NULL },
    {"receive", 'r', POPT_ARG_NONE, NULL, 'r', "Receive connection from a remote test_ioh server (default)", NULL },
    {"verbose", 'v', POPT_ARG_NONE, NULL, 'v', "Turn on verbose message", NULL },
    {"seatbelts", 'S', POPT_ARG_NONE, NULL, 'S', "Seatbelts off & speed up", NULL },
    {"ioh-inbuf-hint", 0, POPT_ARG_INT, NULL, 1, "Input buffer sizing hint for IOH", NULL },
    {"ioh-inbuf-max", 0, POPT_ARG_INT, NULL, 2, "Input buffer max hint for IOH", NULL },
    {"ioh-outbuf-max", 0, POPT_ARG_INT, NULL, 3, "Output buffer max hint for IOH", NULL },
    {"ioh-mode-raw", 0, POPT_ARG_NONE, NULL, 4, "Put ioh in raw-mode (default)", NULL },
    {"ioh-mode-block", 0, POPT_ARG_NONE, NULL, 5, "Put ioh in block-mode", NULL },
    {"ioh-mode-vectored", 0, POPT_ARG_NONE, NULL, 6, "Put ioh in vectored-mode", NULL },
    {"ioh-mode-line", 0, POPT_ARG_NONE, NULL, 7, "Put ioh in line-mode", NULL },
    POPT_AUTOHELP
    POPT_TABLEEND
  };

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD("BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, BK_ERR_ERR, 0)))
  {
    fprintf(stderr,"Could not perform basic initialization\n");
    exit(254);
  }
  bk_fun_reentry(B);

  pconfig = &Pconfig;
  memset(pconfig,0,sizeof(*pconfig));
  pconfig->pc_ioh_mode = PC_MODE_RAW;		// Default mode is RAW
  pconfig->pc_port = PORT;			// Default port is PORT

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
      bk_debug_setconfig(B, BK_GENERAL_DEBUG(B), BK_GENERAL_CONFIG(B), BK_GENERAL_PROGRAM(B));
      bk_general_debug_config(B, stderr, BK_ERR_NONE, 0);
      bk_debug_printf(B, "Debugging on\n");
      break;
    case 't':
      pconfig->pc_remhost = poptGetOptArg(optCon);
      break;
    case 'p':
      pconfig->pc_port = atoi(poptGetOptArg(optCon));
      break;
    case 'r':
      pconfig->pc_remhost = NULL;
      break;
    case 'v':
      BK_FLAG_SET(pconfig->pc_flags, PC_VERBOSE);
      bk_error_config(B, BK_GENERAL_ERROR(B), ERRORQUEUE_DEPTH, stderr, BK_ERR_NONE, BK_ERR_ERR, 0);
      break;
    case 'S':
      BK_FLAG_CLEAR(BK_GENERAL_FLAGS(B), BK_BGFLAGS_FUNON);
      break;
    case 1:
      pconfig->pc_input_hint = atoi(poptGetOptArg(optCon));
      break;
    case 2:
      pconfig->pc_input_max = atoi(poptGetOptArg(optCon));
      break;
    case 3:
      pconfig->pc_output_max = atoi(poptGetOptArg(optCon));
      break;
    case 4:
      pconfig->pc_ioh_mode = PC_MODE_RAW;
      break;
    case 5:
      pconfig->pc_ioh_mode = PC_MODE_BLOCK;
      break;
    case 6:
      pconfig->pc_ioh_mode = PC_MODE_VECTOR;
      break;
    case 7:
      pconfig->pc_ioh_mode = PC_MODE_LINE;
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
    
  if (proginit(B, pconfig) < 0)
  {
    bk_die(B,254,stderr,"Could not perform program initialization\n",BK_FLAG_ISSET(pconfig->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  }

  bk_run_run(B, pconfig->pc_run, 0);

  bk_exit(B,0);
  abort();
}



/**
 * General program initialization
 *
 *	@param B BAKA Thread/Global configuration
 *	@param pconfig Program configuration
 *	@return <i>0</i> Success
 *	@return <br><i>-1</i> Total terminal failure
 */
int proginit(bk_s B, struct program_config *pconfig)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"IOHTEST");

  if (!pconfig)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_RETURN(B, -1);
  }

  // Create run environment
  if (!(pconfig->pc_run = bk_run_init(B, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create baka run environment\n");
    BK_RETURN(B, -1);
  }

  // Transmit or receive mode?
  if (pconfig->pc_remhost)
  {						// Transmit
    struct in_addr himaddr;

    if (inet_aton(pconfig->pc_remhost, &himaddr) < 0)
    {
      bk_die(B,254,stderr,"Could not perform convert remote address\n",BK_FLAG_ISSET(pconfig->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
    }

    address_resolved(B, pconfig, &himaddr);
  }
  else
  {						// Receive
    int sd = -1;
    struct sockaddr_in sinme;

    if ((sd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not receive socket: %s\n",strerror(errno));
      BK_RETURN(B, -1);
    }

    // Bind to PORT on any address
    memset(&sinme, 0, sizeof(sinme));
#ifdef HAS_SIN_LEN
    sinme.sin_len = sizeof(sineme);
#endif /* HAS_SIN_LEN */
    sinme.sin_family = AF_INET;
    sinme.sin_port = htons(pconfig->pc_port);

    if (bind(sd, (struct sockaddr *)&sinme, sizeof(sinme)) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not bind socket: %s\n", strerror(errno));
      close(sd);
      BK_RETURN(B, -1);
    }

    if (listen(sd, 1) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not listen to socket: %s\n", strerror(errno));
      close(sd);
      BK_RETURN(B, -1);
    }

    if (bk_run_handle(B, pconfig->pc_run, sd, rmt_acceptor, pconfig, BK_RUN_WANTREAD, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not place fd %d into run environment\n",sd);
      close(sd);
      BK_RETURN(B, -1);
    }
  }

  BK_RETURN(B, 0);
}



/**
 * We have activity on server socket--accept a new connection
 *
 *	@param B BAKA Thread/Global configuration
 *	@param run Run environment
 *	@param fd File descriptor the activity was received on	
 *	@param gottypes Activity that we received
 *	@param opaque Private data (really pconfig)
 *	@param starttime Time this event occured, sorta
 */
void rmt_acceptor(bk_s B, struct bk_run *run, u_int fd, u_int gottypes, void *opaque, struct timeval starttime)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"IOHTEST");
  struct program_config *pconfig = opaque;
  int sockaddrsize, newfd = -1;
  struct sockaddr throwaway;
  struct bk_ioh *ioh;

  if (!pconfig || !run)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_VRETURN(B);
  }

  if (BK_FLAG_ISSET(gottypes, BK_RUN_CLOSE) ||
      BK_FLAG_ISSET(gottypes, BK_RUN_DESTROY))
  {
    // Yawn--program is probably going away.  We don't care.
    BK_VRETURN(B);
  }

  if (BK_FLAG_ISSET(gottypes, BK_RUN_XCPTREADY) ||
      BK_FLAG_ISSET(gottypes, BK_RUN_WRITEREADY))
  {
    bk_error_printf(B, BK_ERR_ERR, "How did we get %x set--we did not ask for any\n",gottypes);
    BK_VRETURN(B);
  }

  sockaddrsize = sizeof(throwaway);
  if ((newfd = accept(fd, &throwaway, &sockaddrsize)) < 0)
  {
    if (
#ifdef EWOULDBLOCK
	errno == EWOULDBLOCK
#endif /* EWOULDBLOCK */
#if defined(EWOULDBLOCK) && defined(EAGAIN)
	||
#endif /* EWOULDBLOCK && EAGAIN */
#if defined(EAGAIN)
	errno == EAGAIN
#endif /* EAGAIN */
	)
    {
      bk_error_printf(B, BK_ERR_NOTICE, "Received read-ready, but got EAGAIN when tried to accept\n");
      BK_VRETURN(B);
    }

    bk_error_printf(B, BK_ERR_ERR, "Accept failed: %s\n",strerror(errno));
    BK_VRETURN(B);
  }

  // Single-shot this puppy...
  bk_run_close(B, run, fd, 0);

  if (create_relay(B, pconfig, newfd, newfd, 0, 1, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create relay: %s\n",strerror(errno));
    goto error;
  }

  BK_VRETURN(B);

 error:
  if (newfd >= 0)
    close(newfd);

  BK_VRETURN(B);
}



static int create_relay(bk_s B, struct program_config *pconfig, int fd1in, int fd1out, int fd2in, int fd2out, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"IOHTEST");
  struct bk_ioh *ioh1 = NULL;
  struct bk_ioh *ioh2 = NULL;
  int mode;

  if (!pconfig)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_RETURN(B, -1);
  }

  mode = BK_IOH_STREAM;
  switch (pconfig->pc_ioh_mode)
  {
  default:
  case PC_MODE_RAW:
    mode |= BK_IOH_RAW;
    break;
  case PC_MODE_BLOCK:
    mode |= BK_IOH_BLOCKED;
    break;
  case PC_MODE_VECTOR:
    mode |= BK_IOH_VECTORED;
    break;
  case PC_MODE_LINE:
    mode |= BK_IOH_LINE;
    break;
  }

  // Create IOH for network -- note opaque data must be reset
  if (!(ioh1 = bk_ioh_init(B, fd1in, fd1out, bk_ioh_stdrdfun, bk_ioh_stdwrfun, relayer, ioh2, pconfig->pc_input_hint, pconfig->pc_input_max, pconfig->pc_output_max, pconfig->pc_run, mode)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid network ioh creation\n");
    bk_die(B,254,stderr,"Could not perform ioh initialization\n",BK_FLAG_ISSET(pconfig->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  }

  // Create IOH for stdio
  if (!(ioh2 = bk_ioh_init(B, fd2in, fd2out, bk_ioh_stdrdfun, bk_ioh_stdwrfun, relayer, ioh1, pconfig->pc_input_hint, pconfig->pc_input_max, pconfig->pc_output_max, pconfig->pc_run, mode)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid network ioh creation\n");
    bk_die(B,254,stderr,"Could not perform ioh initialization\n",BK_FLAG_ISSET(pconfig->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  }
  
  // Update IOH1 opaque data now that we know it
  if (bk_ioh_update(B, ioh1, bk_ioh_stdrdfun, bk_ioh_stdwrfun, relayer, ioh2, pconfig->pc_input_hint, pconfig->pc_input_max, pconfig->pc_output_max, mode) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid network ioh update\n");
    bk_die(B,254,stderr,"Could not perform mandatory ioh update\n",BK_FLAG_ISSET(pconfig->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  }

  BK_RETURN(B, 0);
}



static void relayer(bk_s B, bk_vptr data[], void *opaque, struct bk_ioh *ioh_in, u_int state_flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"IOHTEST");
  struct bk_ioh *ioh_out = opaque;
  int cnt = 0;
  int size = 0;
  bk_vptr *newcopy = NULL;

  if (!ioh_in || !ioh_out)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_VRETURN(B);
  }

  switch (state_flags)
  {
  case BK_IOH_STATUS_INCOMPLETEREAD:
  case BK_IOH_STATUS_READCOMPLETE:
    // Coalesce into one buffer for output
    for (cnt = 0; data[cnt].ptr; cnt++)
    {
      size += data[cnt].len;
    }
    if (!BK_MALLOC(newcopy))
      goto error;
    newcopy->len = size;
    if (!BK_MALLOC_LEN(newcopy->ptr, newcopy->len))
      goto error;
    for (cnt = 0; data[cnt].ptr; cnt++)
    {
      memcpy((char *)newcopy->ptr + size, data[cnt].ptr, data[cnt].len);
      size += data[cnt].len;
    }
    if (bk_ioh_write(B, ioh_out, newcopy, 0) != 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not write data to output ioh\n");
      goto error;
    }

  case BK_IOH_STATUS_IOHREADERROR:
  case BK_IOH_STATUS_IOHREADEOF:
    // Propagate shutdown to write side of peer
    bk_ioh_shutdown(B, ioh_out, SHUT_WR, 0);
    BK_VRETURN(B);

  case BK_IOH_STATUS_IOHWRITEERROR:
    // Propagate shutdown to read side of peer
    bk_ioh_shutdown(B, ioh_out, SHUT_RD, 0);
    BK_VRETURN(B);

  case BK_IOH_STATUS_WRITECOMPLETE:
  case BK_IOH_STATUS_WRITEABORTED:
    free(data[cnt].ptr);
    free(data);
    BK_VRETURN(B);

  case BK_IOH_STATUS_IOHCLOSING:
  case BK_IOH_STATUS_IOHABORT:
  case BK_IOH_STATUS_USERERROR:
    // Don't care about this
    BK_VRETURN(B);
  }

  bk_error_printf(B, BK_ERR_ERR, "Unknown state %d\n",state_flags);
  BK_VRETURN(B);

 error:
  if (newcopy)
  {
    if (newcopy->ptr)
      free(newcopy->ptr);
    free(newcopy);
  }
  BK_VRETURN(B);
}



static void address_resolved(bk_s B, struct program_config *pconfig, struct in_addr *himaddr)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"IOHTEST");
  int sd = -1;
  struct sockaddr_in sinhim;

  if (!pconfig || !himaddr)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  if ((sd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not receive socket: %s\n",strerror(errno));
    BK_VRETURN(B);
  }

  // Connect to PORT on his address
  memset(&sinhim, 0, sizeof(sinhim));
#ifdef HAS_SIN_LEN
  sinhim.sin_len = sizeof(sineme);
#endif /* HAS_SIN_LEN */
  sinhim.sin_family = AF_INET;
  sinhim.sin_addr = *himaddr;
  sinhim.sin_port = htons(pconfig->pc_port);

  if (connect(sd, &sinhim, sizeof(sinhim)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could connect to remote address\n");
    close(sd);
    bk_die(B,254,stderr,"Could not connect to remote address\n",BK_FLAG_ISSET(pconfig->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  }

  if (create_relay(B, pconfig, sd, sd, 0, 1, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set up relay\n");
    close(sd);
    bk_die(B,254,stderr,"Could not set up relay\n",BK_FLAG_ISSET(pconfig->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  }

  BK_VRETURN(B);
}


