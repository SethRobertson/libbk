#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_addrgroup.c,v 1.3 2001/11/15 23:02:35 jtt Exp $";
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

#include <libbk.h>
#include "libbk_internal.h"

/**
 * @file 
 * Addrgroup.c
 */

/**
 * State associated with network access tries. 
 */
struct addrgroup_state
{
  bk_flags		as_flags;		///< Everyone needs flags
#define ADDRGROUP_STATE_FLAG_CONNECTING	0x1	///< We are connecting.
#define ADDRGROUP_STATE_FLAG_ACCEPTING	0x2	///< We are accepting.
  int			as_sock;		///< Socket
  struct bk_addrgroup 	as_bag;			///< Addrgroup info
  u_long		as_timeout;		///< Timeout in usecs
  bk_bag_callback_t	as_callback;		///< Called when sock ready
  void *		as_args;		///< User args for callback
  void *		as_eventh;		///< Timeout event handle
  struct bk_run *	as_run;			///< run handle.
};


static struct bk_addrgroup *bag_create(bk_s B);
static void bag_destroy(bk_s B,struct bk_addrgroup *bag);
static struct addrgroup_state *as_create(bk_s B);
static void as_destroy(bk_s B, struct addrgroup_state *as);
static int net_init_check_sanity(bk_s B, struct bk_netinfo *local, struct bk_netinfo *remote, struct bk_addrgroup *bag);
static void net_init_finish(bk_s B, struct bk_run *run, int fd, u_int gottype, void *args, struct timeval startime);
static int addrgroup_apply(bk_s B, struct addrgroup_state *as, bk_addrgroup_result_t result);
static void connect_timeout(bk_s B, struct bk_run *run, void *args, struct timeval starttime, bk_flags flags);
static int do_net_init_af_inet(bk_s B, struct addrgroup_state *as);
static int do_net_init_af_local(bk_s B, struct addrgroup_state *as);
static int do_net_init_af_inet_tcp(bk_s B, struct addrgroup_state *as);
static int do_net_init_af_inet_udp(bk_s B, struct addrgroup_state *as);
static int do_net_init_af_inet_tcp_listen(bk_s B, struct addrgroup_state *as);
static int do_net_init_af_inet_tcp_connect(bk_s B, struct addrgroup_state *as);




/**
 * Create an @a bk_addrgroup structure
 *	@param B BAKA thread/global state.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a bk_addrgroup on success.
 */
static struct bk_addrgroup *
bag_create(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_addrgroup *bag=NULL;

  if (!BK_CALLOC(bag))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocat bag: %s\n", strerror(errno));
    goto error;
  }
  
  BK_RETURN(B,bag);

 error:
  if (bag) bag_destroy(B,bag);
  BK_RETURN(B,NULL);
}




/**
 * Destroy a @a bk_addrgroup
 *	@param B BAKA thread/global state.
 *	@param bag The @a bk_addrgroup to destroy.
 */
static void
bag_destroy(bk_s B,struct bk_addrgroup *bag)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  free(bag);
  BK_VRETURN(B);
}



/**
 * Create an @a addrgroup_state
 *	@param B BAKA thread/global state.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a addrgroup_state on success.
 */
static struct addrgroup_state *
as_create(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct addrgroup_state *as=NULL;

  if (!(BK_CALLOC(as)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate as: %s\n", strerror(errno));
    goto error;
  }
    
  BK_RETURN(B,as);
 error:
  if (as) as_destroy(B, as);

  BK_RETURN(B,NULL);
}



/**
 * Destroy a @a addrgroup_state.
 *	@param B BAKA thread/global state.
 *	@param as The addrgroup_state to destroy.
 */
static void
as_destroy(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  /* If the callback has not been called already, do so with flag */
  if (as->as_callback)
  {
    (*(as->as_callback))(B, as->as_args, -1, NULL, BK_ADDRGROUP_RESULT_FLAG_DESTROYED);
  }

  /* XXX Is this correct??? */
  if (as->as_sock != -1) 
  {
    if (bk_run_close(B,as->as_run, as->as_sock, 0)<0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not withdrawl socket from run\n");
    }
    close(as->as_sock);
  }

  if (as->as_eventh) bk_run_dequeue(B, as->as_run, as->as_eventh, 0);

  free(as);
  BK_VRETURN(B);
}



/*
 * Intialize a transport layer "tap". This takes two @a bk_netinfo structs:
 * one @a local and one @a remote. Either of these may be NULL (but not
 * both :-)). If @a remote is NULL, then this will be a server (though
 * listen(2)) is deferred until later). If @a local is NULL, then the
 * kernel will chose the local side addres information and this structure
 * will be filled out when @a callback is called (assuming a successfull
 * conclusion). If neither side is NULL, then we will attempt to bind to @a
 * local and connect to @a remote. NB UDP connections are completely
 * legal. If the caller is setting up a service (ie @a local is non-NULL
 * and @a remote is NULL), he ma choose not to fill in either (or both) of
 * the @a bk_netinfo @a bk_netaddr or @a bk_servinfo structures (the @a
 * bk_protoinfo *must* be filled in). If left unset, the kernel will select
 * appropriate values and (again) the caller may recover these values by
 * calling @a BK_ADDRGROUP_GET_LOCAL_NETINFO() in the callback. This
 * routine allocates a @a bk_addrgroup which the caller gets in the
 * callback and should destroy with @a bk_addrgroup_destroy().
 *	@param B BAKA thread/global state.
 *	@param local @a bk_netinfo of the local side.
 *	@param remote @a bk_netinfo of the remote side.
 *	@param timeout Timeout information (usecs).
 *	@param flags Random flags
 *	@param callback Function to call when whatever job @a bk_net_init needs to do is done.
 *	@param args User args returned to @a callback.
 *	@return <i>-1</i> on failure.<br>
 *	@return new socket on success.
 */
int
bk_net_init(bk_s B, struct bk_run *run, struct bk_netinfo *local, struct bk_netinfo *remote, u_long timeout, bk_flags flags, bk_bag_callback_t callback, void *args)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_addrgroup *bag=NULL;		/* address group state */
  struct addrgroup_state *as=NULL;		/* my local state */
  int ret;

  if (!(local || remote) || !run)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  if (!(as=as_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create as\n");
    goto error;
  }

  as->as_sock=-1;
  bag=&as->as_bag;
  as->as_callback=callback;
  as->as_args=args;
  as->as_timeout=timeout;
  as->as_run=run;
  
  /* This function also sets some of bag's fields */
  if (net_init_check_sanity(B, local, remote, bag) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "did not pass initial sanity checks\n");
    goto error;
  }

  switch(bag->bag_type)
  {
  case BK_NETINFO_TYPE_INET:
  case BK_NETINFO_TYPE_INET6:
    ret=do_net_init_af_inet(B, as);
    break;
  case BK_NETINFO_TYPE_LOCAL:
    ret=do_net_init_af_local(B, as);
    break;
  case BK_NETINFO_TYPE_ETHER:
    bk_error_printf(B, BK_ERR_ERR, "Ether type not supported\n");
    goto error;
  default:
    bk_error_printf(B, BK_ERR_ERR, "Unknown netinfo type: %d\n", bag->bag_type);
    goto error;
  }
#if 0
  if (remote) bk_netinfo_reset_primary_address(B,remote);
  if (addrgroup_apply(B,as, BK_ADDRGROUP_RESULT_FLAG_OK)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not perform required network operations\n");
    goto error;
  }
#endif
  BK_RETURN(B,ret);

 error:
  /* 
   * bag *always be destroyed/closed as a part of as_destroy until such a
   * time as they are handed off to the user at which time they should
   * properly "removed" from the structure.
   */
  if (as) as_destroy(B,as);
  BK_RETURN(B,-1);
}



/**
 * Initialize the network in the AF_INET/AF_INET6 way.
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state.
 *	@return <i>-1</i> on failure.<br>
 *	@return new socket on success.
 */
static int
do_net_init_af_inet(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_addrgroup *bag;
  int ret;

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  bag=&as->as_bag;

  switch (bag->bag_proto)
  {
  case IPPROTO_TCP:
    ret=do_net_init_af_inet_tcp(B, as);
    break;
  case IPPROTO_UDP:
    ret=do_net_init_af_inet_udp(B, as);
    break;
  default: 
    bk_error_printf(B, BK_ERR_ERR, "Unknown INET protocol: %d\n", bag->bag_proto);
    goto error;
  }

  BK_RETURN(B,ret);

 error:
  BK_RETURN(B,-1);
  
}



/**
 * Initialize TCP (connect or listen).
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state information.
 *	@return <i>-1</i> on failure.<br>
 *	@return new socket on success.
 */
static int
do_net_init_af_inet_tcp(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_addrgroup *bag;
  int ret;
  
  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  bag=&as->as_bag;

  if (bag->bag_remote)
  {
    ret=do_net_init_af_inet_tcp_connect(B,as);
  }
  else
  {
    ret=do_net_init_af_inet_tcp_listen(B,as);
  }

  BK_RETURN(B,ret);
}



/**
 * Open an AF_LOCAL connection
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state info.
 *	@return <i>-1</i> on failure.<br>
 *	@return a new socket on success.
 */
static int
do_net_init_af_local(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret=0;

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  BK_RETURN(B,ret);
  
}




/**
 * Open a udp network.
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state info.
 *	@return <i>-1</i> on failure.<br>
 *	@return a new socket on success.
 */
static int
do_net_init_af_inet_udp(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret=0;

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  BK_RETURN(B,ret);
  
}



/**
 * Start a tcp connection
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state info.
 *	@return <i>-1</i> on failure.<br>
 *	@return a new socket on success.
 */
static int
do_net_init_af_inet_tcp_connect(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret=0;

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  BK_RETURN(B,ret);
  
}



/**
 * Open a tcp listener
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state info.
 *	@return <i>-1</i> on failure.<br>
 *	@return a new socket on success.
 */
static int
do_net_init_af_inet_tcp_listen(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret=0;

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  BK_RETURN(B,ret);
  
}



/**
 * Do all the real dirty work in opening network access. This function has
 * to have a simple interface so it can be called from an event handler (so
 * timed out connections can try alternate addresses if they are
 * available). We open a socket, set nonblocking on it, examine the remote
 * and local addresses, and do the Right Thing for all.
 *
 *	@param B BAKA thread/global state.
 *	@param as All the state we need to know to do the job.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
addrgroup_apply(bk_s B, struct addrgroup_state *as, bk_addrgroup_result_t result)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct sockaddr sa;
  struct bk_netaddr *bna;
  struct bk_addrgroup *bag;
  struct bk_netinfo *local, *remote;
  int s=-1;
  int transport_type;
  int af;

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  bag=&as->as_bag;
  local=bag->bag_local;
  remote=bag->bag_remote;

  if (as->as_sock != -1)
  {
    if (bk_run_close(B,as->as_run, as->as_sock, 0)<0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not withdrawl socket from run\n");
    }
    close(as->as_sock);
  }
  as->as_sock=-1;

  af=bk_netaddr_nat2af(B,bag->bag_type); 
  
  switch (bag->bag_proto)
  {
  case IPPROTO_TCP:
    transport_type=SOCK_STREAM;
    break;
  case IPPROTO_UDP:
    transport_type=SOCK_DGRAM;
    break;
  default: 
    bk_error_printf(B, BK_ERR_ERR, "Protocol: %d not (yet?) supported:\n", bag->bag_proto);
    goto error;
    break;
  }

  if ((s=socket(af,transport_type, bag->bag_proto))<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create socket: %s\n", strerror(errno));
    goto error;
  }

  as->as_sock=s;
  


  /* Force non-blocking. We will restore this later */
  if (bk_fileutils_modify_fd_flags(B, s, O_NONBLOCK, BK_FILEUTILS_MODIFY_FD_FLAGS_ACTION_ADD)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set O_NONBLOCK on socket: %s\n", strerror(errno));
    /* 
     * XXX Should this really be fatal? Well, given what a totally bizzare
     * situation you must be in for this to fail, jtt actually thinks this
     * is reasonable.
     */
    goto error;
  }

  if (local)
  {
    if (bk_netinfo_to_sockaddr(B, local, NULL, bag->bag_type, &sa, BK_NETINFO2SOCKADDR_FLAG_FUZZY_ANY)<0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create local sockaddr\n");
      goto error;
    }

    /* XXX Use SA_LEN macro here */
    if (bind(s, &sa, sizeof(sa)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not bind to local address: %s\n", strerror(errno));
      goto error;
    }
  }

  if (remote)
  {
    /* Just turn this on every time. */
    BK_FLAG_SET(as->as_flags, ADDRGROUP_STATE_FLAG_CONNECTING);

    if (!remote->bni_addr)
    {
      if (!(bna=netinfo_addrs_minimum(remote->bni_addrs)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Must have at least one address to which to connect\n");
	goto error;
      }
    }
    else
    {
      if (!(bna=netinfo_addrs_successor(remote->bni_addrs,remote->bni_addr)))
      {
	close(s);
	as->as_sock=-1;
	if (as->as_callback)
	{
	  (*(as->as_callback))(B, as->as_args, as->as_sock, bag, result);
	  BK_RETURN(B,0);
	}
      }
    }
  
    if (bk_netinfo_set_primary_address(B, remote, bna) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not establish primary address\n");
      goto error;
    }

    /* Start up the remote side */
    if (bk_netinfo_to_sockaddr(B, remote, bna, as->as_bag.bag_type, &sa, 0)<0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not remote local sockaddr\n");
      goto error;
    }
    
    if (connect(as->as_sock, &sa, sizeof(sa))<0 && errno != EINPROGRESS)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not connect: %s\n", strerror(errno));
      goto error;
    }

    if (bk_run_enqueue_delta(B, as->as_run, as->as_timeout, connect_timeout, as, &as->as_eventh, 0)<0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not enque timeout event\n");
      goto error;
    }

    /* Put the socket in the select loop waiting for *write* to come ready */
    if (bk_run_handle(B, as->as_run, as->as_sock, net_init_finish, as, BK_RUN_WANTWRITE, 0)<0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not configure socket's I/O handler\n");
      goto error;
    }
  }
  else 
  {
    /*
     * We only get here if (local && !remote) (you might want to convince
     * yourself of this). So if we are only setting up the local side then
     * we finish off this function synchronously. jtt *had* thought we
     * could be really crafty and simply wait for a write-ready condition
     * (which is what you wait for when connect(2)'ing). This is true for
     * UDP, but absolutely not the case (he now assumes) for TCP listens
     * (goll-dang-it), so we have to finish off synchronously for TCP
     * listens. And if we have to do it for one local side thing, we might
     * as well do it for all.
     */
    struct timeval timenow;
    gettimeofday(&timenow, NULL);
    net_init_finish(B, as->as_run, s, 0, as, timenow);
  }


  BK_RETURN(B,0);

 error:
  if (as) as_destroy(B,as);
  BK_RETURN(B,-1);
}



/**
 * Complete initializing the network. This is a standard @a bk_run callback function. 
 *	@param B BAKA thread/global state.
 *	@param run The run structure passed up.
 *	@param fd The descriptor on which we had activity. 
 *	@param gottype The type of activity detected.
 *	@param gottype The type of activity detected.
 *	@param args The @a addrgroup_state structure.
 *	@param starttime The start time of the latested @a bk_run loop.
 */
static void
net_init_finish(bk_s B, struct bk_run *run, int fd, u_int gottype, void *args, struct timeval startime)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct addrgroup_state *as;
  struct bk_addrgroup *bag;

  if (!run || !(as=args))
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }
  
  bag=&as->as_bag;

  /* 
   * Remove from bk_run's control
   */
  if (bk_run_close(B,as->as_run, as->as_sock, 0)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not withdrawl socket from run\n");
  }

  /*
   * First find out the state of any connection. 
   */
  if (BK_FLAG_ISSET(as->as_flags, ADDRGROUP_STATE_FLAG_CONNECTING)) 
  {
    struct sockaddr_in sin4;
    if (bk_netinfo_to_sockaddr(B, bag->bag_remote, NULL, BK_NETINFO_TYPE_UNKNOWN, (struct sockaddr *)(&sin4), 0)<0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not convert remote address to sockaddr any more\n");
      goto done;
    /* Forge on suppose, though our socket might be toally hosed */
    }
    else
    {
      /*
       * Run a second connect to find out what has happened to our
       * descriptor. The specific results of this seem to be platform
       * specific, which is a little disturbing. The following illustrates
       * the confusion. 1st call and 2nd call refer to calls to connect(2)
       * *following* the return from select(2) (IOW when we have idea of
       * what has hapened to the socket.
       * 
       * 		Succesful connection
       * 		--------------------
       *		1st Call		2nd call
       * linux: 	Success (returns 0)	EISCONN
       * solaris:	EISCONN			EISCONN
       * openbsd:	EISCONN			EISCONN
       *
       * 		RST connection
       * 		---------------
       *		1st Call		2nd call
       * linux: 	ECONNREFUSED		EINPROGRESS
       * solaris:	ECONNREFUSED		EINPROGRESS
       * openbsd:	EINVALID		EINVALID
       * 
       * For my money only *solaris* gets it right all the way the way
       * around. It, along with openbsd, properly returns EISCONN on the
       * first connect(2) following a successfull connect unlike linux
       * which returns 0 on this call (and EISCONN only after
       * that). Futhermore it, along with linux, returns the actual error
       * on a connect failure (as a result of that first call), while
       * openbsd simply return EINVALID which tell you *nothing* about what
       * actually went wrong!
       *
       * However, it is at lest the case that all platforms seem to return
       * either 0 or EISCONN on a successful connect(2). So we can at least
       * code for this case properly.
       *
       * I wonder what Windows does....
       */
      if (connect(fd, (struct sockaddr *)(&sin4), sizeof(sin4))<0 && errno != EISCONN)
      {
	bk_error_printf(B, BK_ERR_ERR, "Connect to %s failed: %s\n", bag->bag_remote->bni_pretty, strerror(errno)); 
	if (bk_run_close(B,as->as_run, as->as_sock, 0)<0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not withdrawl socket from run\n");
	}
	close(fd);
	addrgroup_apply(B,as, BK_ADDRGROUP_RESULT_FLAG_IO_ERROR);
	BK_VRETURN(B);
      }
    }
  }


  /* Only mess around with the fd if it is still valid */
  if (fd != -1)
  {
    /* 
     * XXX Still need to do this.
     * Get the actual local name. Add it to the local address and set the
     * "primary" address to it.
     */

    /* Reset the nonblock flag as promised */
    if (bk_fileutils_modify_fd_flags(B, fd, O_NONBLOCK, BK_FILEUTILS_MODIFY_FD_FLAGS_ACTION_DELETE)<0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not set O_NONBLOCK on socket: %s\n", strerror(errno));
    }
  }
  

  if (as->as_callback)
  {
    (*(as->as_callback))(B, as->as_args, fd, bag, 0);
  }

  /* Successfully made callback, so null out callback pointer beore destroy */
  as->as_callback=NULL;

  as_destroy(B, as);
  
 done:
  BK_VRETURN(B);
}



/**
 * Connect timeout function. The interface conforms to a standard BAKA
 * event API
 *	@param B BAKA thread/global state.
 *	@param run The run structure passed up.
 *	@param args The @a addrgroup_state structure.
 *	@param starttime The start time of the last @a bk_run loop.
 *	@param flags Random flags.
 */
static void
connect_timeout(bk_s B, struct bk_run *run, void *args, struct timeval starttime, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct addrgroup_state *as;
  if (!run || !(as=args))
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  /* Make darn sure this is NULL in the case of emegency :-) */
  as->as_eventh=NULL;
  
  bk_error_printf(B, BK_ERR_WARN, "Connection to %s timed out\n", as->as_bag.bag_remote->bni_pretty);

  /* Check for more addresses */
  if (addrgroup_apply(B, as, BK_ADDRGROUP_RESULT_FLAG_TIMEOUT)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Failed to attempt connection to new address\n");
  }
  BK_VRETURN(B);
}



/*
 * bk_net_init sanity checking. This code is experted here so that it
 * doesn't take up space in the main function.
 *	@param B BAKA thread/global state.
 *	@param local @a bk_netinfo of the local side.
 *	@param remote @a bk_netinfo of the remote side.
 *	@param bag @a bk_addrgroup to use and update.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
net_init_check_sanity(bk_s B, struct bk_netinfo *local, struct bk_netinfo *remote, struct bk_addrgroup *bag)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netaddr *bna=NULL;

  if (!bag || (!local && !remote))
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  bag->bag_local=local;
  bag->bag_remote=remote;

  /* Just a bit of sanity checking */
  if ((local && !local->bni_bpi) || 
      (remote && !remote->bni_bpi))
  {
    /* We require that both local and remote have proto entries */
    bk_error_printf(B, BK_ERR_ERR, "netinfo structs *must* contain protocol information\n");
    goto error;
  }

  if (local && remote)
  { 
    if (local->bni_bpi->bpi_proto != remote->bni_bpi->bpi_proto)
    {
      bk_error_printf(B, BK_ERR_ERR, "Mismatched protocols (%d != %d)\n",
		      local->bni_bpi->bpi_proto,
		      remote->bni_bpi->bpi_proto);
      goto error;
    }

    bag->bag_proto = local->bni_bpi->bpi_proto;		/* Cache the proto */
    
    /*
     * Check on address family sanity, but this is more complicated than
     * above. If both netinfo's have adresses then make sure that the
     * address families match (the insertion routines should ensure that no
     * netinfo contains a heterogenous set of addrs); if only one or the
     * other of netinfo's have netaddr's, then use that value; if both
     * netinfo's *lack* addresses, then flag a fatal error and return.
     */
    if ((bna=bk_netinfo_get_addr(B, local)))
    {
      bag->bag_type=bna->bna_type;
    }

    if ((bna=bk_netinfo_get_addr(B, remote)))
    {
      if (bag->bag_type)
      {
	if (bag->bag_type != bna->bna_type)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Address family mismatch: (%d != %d)\n", bag->bag_type, bna->bna_type);
	  goto error;
	}
      }
      else
      {
	bag->bag_type=bna->bna_type;
      }
    }

    if (bag->bag_type == BK_NETINFO_TYPE_UNKNOWN)
    {
      bk_error_printf(B, BK_ERR_ERR, "Address family not determinable\n");
      goto error;
    }
  }

  BK_RETURN(B,0);

 error:
  BK_RETURN(B,-1);
  
}
