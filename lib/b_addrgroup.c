#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_addrgroup.c,v 1.31 2002/10/15 22:22:19 jtt Exp $";
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
 * Addrgroup.c: This file takes care of initializing the network for data
 * transmission. We try to support as many address families and protocol
 * types as we can.
 */

#include <libbk.h>
#include "libbk_internal.h"



/**
 * @Section README DEVELOPERS
 *
 * This file is structured in a particular way and it might be a good
 * idea if you strive to maintain that. The problem with network code
 * like this is that you can wind up with <em>huge</em> functions with
 * so many decisions that debugging is a bear and modifying darn near
 * impossible. So instead we choose to make only a small number of
 * decisions in each function and call down to sub functions (with
 * typically longer names :-)) based on that decision. This is very
 * irritating when you're first coding or first looking at the file,
 * but believe me, it's great when you just need to go in and make a
 * slight change (of course if sucks <em>big time</em> if you have to
 * make some form of global change). The structure of the file should
 * reveal itself as you read through, but this is one thing of such
 * significance that's it's worth mentioning here. The whole kit and
 * caboodle begins with net_init() where we allocate an
 * addrgroup_state structure. The whole things ends with
 * net_init_end(). <em>Make sure your code thread wind up in
 * net_init_end() no matter what happens.</em> That function takes a
 * @a state argument which allows you to offer some basic error
 * handling. Now just as important as it is to reach net_init_end()
 * once, it's equally important that you don't call it again (for this
 * file descriptor) unless this is a tcp accepting socket (or
 * something similar). This is to say that if the socket is a
 * connecting socket or in an error condition (regardless of type),
 * then this state structure is going to be cleaned up in
 * net_init_end() and thus you do <em>not</em> to call this again (for
 * that descriptor).
 */



/**
 * State associated with network access tries. 
 */
struct addrgroup_state
{
  bk_flags		as_flags;		///< Everyone needs flags
  bk_addrgroup_state_e	as_state;		///< Our state.
  int			as_sock;		///< Socket
  struct bk_addrgroup *	as_bag;			///< Addrgroup info
  u_long		as_timeout;		///< Timeout in usecs
  bk_bag_callback_f	as_callback;		///< Called when sock ready
  void *		as_args;		///< User args for callback
  void *		as_eventh;		///< Timeout event handle
  struct bk_run *	as_run;			///< run handle.
  int			as_backlog;		///< Listen backlog.
  bk_flags		as_user_flags;		///< Flags passed in from user.
  struct addrgroup_state *as_server;		///< Server as (generally pointing at myself).
};



static struct bk_addrgroup *bag_create(bk_s B);
static void bag_destroy(bk_s B,struct bk_addrgroup *bag);
static struct addrgroup_state *as_create(bk_s B);
static void as_destroy(bk_s B, struct addrgroup_state *as);
static int net_init_check_sanity(bk_s B, struct bk_netinfo *local, struct bk_netinfo *remote, struct bk_addrgroup *bag);
static int do_net_init_af_inet(bk_s B, struct addrgroup_state *as);
static int do_net_init_af_local(bk_s B, struct addrgroup_state *as);
static int do_net_init_af_inet_tcp(bk_s B, struct addrgroup_state *as);
static int do_net_init_af_inet_udp(bk_s B, struct addrgroup_state *as);
static int do_net_init_af_inet_udp_connect(bk_s B, struct addrgroup_state *as);
static int do_net_init_af_inet_udp_listen(bk_s B, struct addrgroup_state *as);
static int do_net_init_af_inet_tcp_listen(bk_s B, struct addrgroup_state *as);
static int do_net_init_af_inet_tcp_connect(bk_s B, struct addrgroup_state *as);
static int tcp_connect_start(bk_s B, struct addrgroup_state *as);
static int open_inet_local(bk_s B, struct addrgroup_state *as);
static void tcp_end(bk_s B, struct addrgroup_state *as);
static void tcp_connect_timeout(bk_s B, struct bk_run *run, void *args, const struct timeval *starttime, bk_flags flags);
static void tcp_connect_activity(bk_s B, struct bk_run *run, int fd, u_int gottype, void *args, const struct timeval *startime);
static void net_close(bk_s B, struct addrgroup_state *as);
static void tcp_listen_activity(bk_s B, struct bk_run *run, int fd, u_int gottype, void *args, const struct timeval *startime);
static void net_init_end(bk_s B, struct addrgroup_state *as);
static void net_init_abort(bk_s B, struct addrgroup_state *as);
static struct addrgroup_state *as_server_copy(bk_s B, struct addrgroup_state *oas , int s);



/**
 * Create an @a bk_addrgroup structure
 *
 *	@param B BAKA thread/global state.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a bk_addrgroup on success.
 */
static struct bk_addrgroup *
bag_create(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_addrgroup *bag = NULL;

  if (!BK_CALLOC(bag))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bag: %s\n", strerror(errno));
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

  if (!bag)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (bag->bag_local) bk_netinfo_destroy(B, bag->bag_local);
  if (bag->bag_remote) bk_netinfo_destroy(B, bag->bag_remote);

  free(bag);
  BK_VRETURN(B);
}



/**
 * Public interface to destroy routine
 *
 *	@param B BAKA thread/global state.
 *	@param bag The @a bk_addrgroup to destroy.
 */
void
bk_addrgroup_destroy(bk_s B,struct bk_addrgroup *bag)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bag)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  bag_destroy(B,bag);
  BK_VRETURN(B);
}




/**
 * Create an @a addrgroup_state
 *
 *	@param B BAKA thread/global state.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a addrgroup_state on success.
 */
static struct addrgroup_state *
as_create(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct addrgroup_state *as = NULL;

  if (!(BK_CALLOC(as)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate as: %s\n", strerror(errno));
    goto error;
  }

  /* Point this at myself. It'll be updated later if necessary */
  as->as_server = as;

  /* 
   * We start out with SysError as the *default* state for the user
   * callback.  By this we simply mean that this is the state for which we
   * most likely have to *code* since it catches the majority of things
   * which can go wrong (malloc/strdup failures, dict failures, etc). The
   * success cases tend to manifest themselves in exactly one place in the
   * code, so it's less typing to assume error and reset to non-error when
   * you know that's the right thing to do.
   */
  as->as_state = BkAddrGroupStateSysError;

  if (!(as->as_bag = bag_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create bk_addrgroup: %s\n", strerror(errno));
    goto error;
  }

  BK_RETURN(B,as);

 error:
  if (as) as_destroy(B, as);

  BK_RETURN(B,NULL);
}



/**
 * Destroy a @a addrgroup_state.
 *
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

  /* Recursion protection */
  if (as->as_state == BkAddrGroupStateClosing)
  {
    BK_VRETURN(B);
  }
  as->as_state = BkAddrGroupStateClosing;
 
 if (as->as_bag)
  {
    bag_destroy(B,as->as_bag);
  }

  /* If the callback has not been called already, do so with flag */
  if (as->as_callback)
  {
    (*(as->as_callback))(B, as->as_args, -1, NULL, as->as_server, as->as_state);
  }

  if (as->as_eventh) bk_run_dequeue(B, as->as_run, as->as_eventh, 0);

  /* <WARNING> Is this correct??? </WARNING> */
  net_close(B,as);
  free(as);
  BK_VRETURN(B);
}



/**
 * Initialize a transport layer "tap". This takes two @a bk_netinfo structs:
 * one @a local and one @a remote. Either of these may be NULL (but not
 * both :-)). If @a remote is NULL, then this will be a server (though
 * listen(2)) is deferred until later). If @a local is NULL, then the
 * kernel will chose the local side address information and this structure
 * will be filled out when @a callback is called (assuming a successful
 * conclusion). If neither side is NULL, then we will attempt to bind to @a
 * local and connect to @a remote. NB UDP connections are completely
 * legal. If the caller is setting up a service (i.e. @a local is non-NULL
 * and @a remote is NULL), he may choose not to fill in either (or both) of
 * the @a bk_netinfo @a bk_netaddr or @a bk_servinfo structures (the @a
 * bk_protoinfo *must* be filled in). If left unset, the kernel will select
 * appropriate values and (again) the caller may recover these values by
 * calling @a BK_ADDRGROUP_GET_LOCAL_NETINFO() in the callback. This
 * routine allocates a @a bk_addrgroup which the caller gets in the
 * callback and should destroy with @a bk_addrgroup_destroy().
 *
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
bk_net_init(bk_s B, struct bk_run *run, struct bk_netinfo *local, struct bk_netinfo *remote, u_long timeout, bk_flags flags, bk_bag_callback_f callback, void *args, int backlog)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_addrgroup *bag = NULL;		/* address group state */
  struct addrgroup_state *as = NULL;		/* my local state */
  int ret;

  if (!(local || remote) || !run)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  if (!(as = as_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create as\n");
    goto error;
  }

  as->as_sock = -1;
  bag = as->as_bag;
  as->as_callback = callback;
  as->as_args = args;
  as->as_timeout = timeout;
  as->as_run = run;
  as->as_backlog = backlog;
  as->as_user_flags = flags;

  if (local && !(bag->bag_local = bk_netinfo_clone(B,local)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not clone local netinfo\n");
    goto error;
  }
  
  if (remote && !(bag->bag_remote = bk_netinfo_clone(B,remote)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not clone remote netinfo\n");
    goto error;
  }

  /* This function also sets some of bag's fields */
  if (net_init_check_sanity(B, local, remote, bag) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "did not pass initial sanity checks\n");
    goto error;
  }

  switch(bag->bag_type)
  {
  case BkNetinfoTypeInet:
  case BkNetinfoTypeInet6:
    ret = do_net_init_af_inet(B, as);
    break;

  case BkNetinfoTypeLocal:
    ret = do_net_init_af_local(B, as);
    break;

  case BkNetinfoTypeEther:
    bk_error_printf(B, BK_ERR_ERR, "Ether type not supported\n");
    goto error;

  default:
    bk_error_printf(B, BK_ERR_ERR, "Unknown netinfo type: %d\n", bag->bag_type);
    goto error;
  }

  if (ret >= 0)
    BK_RETURN(B, ret);

 error:
  net_init_abort(B, as);
  BK_RETURN(B, -1);
}



/**
 * Initialize the network in the AF_INET/AF_INET6 way.
 *
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state.
 *	@return <i>-1</i> on failure<br>
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
  
  bag = as->as_bag;

  switch (bag->bag_proto)
  {
  case IPPROTO_TCP:
    ret = do_net_init_af_inet_tcp(B, as);
    break;

  case IPPROTO_UDP:
    ret = do_net_init_af_inet_udp(B, as);
    break;

  default: 
    bk_error_printf(B, BK_ERR_ERR, "Unknown INET protocol: %d\n", bag->bag_proto);
    goto error;
  }

  BK_RETURN(B, ret);

 error:
  net_close(B, as);
  BK_RETURN(B, -1);
}



/**
 * Initialize TCP (connect or listen).
 *
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
  
  bag = as->as_bag;

  if (bag->bag_remote)
  {
    ret = do_net_init_af_inet_tcp_connect(B,as);
  }
  else
  {
    ret = do_net_init_af_inet_tcp_listen(B,as);
  }

  BK_RETURN(B,ret);
}



/**
 * Open an AF_LOCAL connection
 *
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state info.
 *	@return <i>-1</i> on failure.<br>
 *	@return a new socket on success.
 */
static int
do_net_init_af_local(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret = 0;

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  bk_error_printf(B, BK_ERR_ERR, "Do not support local domain sockets quite yet...\n");
  ret = -1;					// Not quite ready yet

  BK_RETURN(B,ret);
}




/**
 * Open a udp network.
 *
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state info.
 *	@return <i>-1</i> on failure.<br>
 *	@return a new socket on success.
 */
static int
do_net_init_af_inet_udp(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_addrgroup *bag;
  int ret = 0;

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  bag = as->as_bag;

  if (bag->bag_remote)
  {
    ret = do_net_init_af_inet_udp_connect(B,as);
  }
  else
  {
    /*
     * <TODO> XXX - how do we handle UDP listeners?
     * well, the question is really how do we handle UDP listeners in the
     * automagic way which users of libbk might expect?  What might they expect?
     * One example might be emulation of TCP behavior.  When a message comes in
     * from a previously unknown host, somehow create a bound socket for that
     * peer and go through normal accept procedure and let user create an IOH
     * for that peer.  Of course, by that time the message has already appeared
     * on the "wrong" (nee server) pcb input queue.  Insisting that all UDP
     * applications send a throwaway startup packet seems unlikely at this
     * late date...
     *
     * One way might be to simply only allow one UDP peer at a time.  When a message
     * is received, connect to that peer, and game over.  Maybe send message to
     * user to let them recreate the server socket.  Potential race condition.
     * Too bad we cannot dup the server and connect one of the new sockets.  We
     * cannot do that, right?
     *
     * Another option might be to somehow create pseudo IOHs which contain the real
     * IOH and the address of the peer.  When messages are received, the list of
     * pseudo IOHs is searched (and a new one created if necessary) and the user's
     * handler is passed the pseudo IOH as if everything normal was happening.
     * Of course, we have this new list we must support, and there must be some
     * kind of type or union in the ioh structure now.  Sigh.</TODO>
     *
     * On the other hand, we *can* get the socket working without this garbage, so
     * lets just do it.
     */
    bk_error_printf(B, BK_ERR_WARN, "UDP listening support is not quite ready for prime time, at least as far as IOHs are concerned\n");
    ret = do_net_init_af_inet_udp_listen(B,as);
  }

  BK_RETURN(B, ret);
}



/**
 * Start a udp connection
 *
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state info.
 *	@return <i>-1</i> on failure.<br>
 *	@return a new socket on success.
 */
static int
do_net_init_af_inet_udp_connect(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int s = -1;
  struct bk_addrgroup *bag;
  struct bk_netinfo *local, *remote;
  struct bk_netaddr *bna;
  struct sockaddr sa;
  int af;

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  bag = as->as_bag;
  local = bag->bag_local;
  remote = bag->bag_remote;

  if (!(bna = bk_netinfo_advance_primary_address(B,remote)))
  {
    /* 
     * <WARNING>
     * If there are *no* addresses at all, do we return a useful error?
     * We probably return SysError which is about as good as we can do so
     * this is probably OK
     * </WARNING>
     */
    net_init_end(B, as);
    /* 
     * You might be tempted to return as->as_sock here since this
     * function may return or be called from things which may return
     * socket names. Do *not* be deceived! At this point in the code
     * (where we are looking for successor and therefore have already
     * made at least *one* attempt to connect) we are running "off the
     * select loop" as it were and return values are pretty meaningless
     * (certainly returning the socket number is meaningless). But much
     * more important than this is is the fact we *know* we're on the
     * connecting side of a tcp association here and thus when tcp_end()
     * returns 'as' HAS BEEN DESTROYED. Now you *could* save as->as_sock
     * before calling tcp_end(), but why bother?
     */
    BK_RETURN(B, 0);
  }

  if (bk_netinfo_to_sockaddr(B, remote, bna, as->as_bag->bag_type, &sa, 0)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not remote local sockaddr\n");
    goto error;
  }
    
  /* af can be either AF_INET of AF_INET6 */
  af = bk_netaddr_nat2af(B,bag->bag_type); 

  /* We *know* this is udp so we can assume SOCK_DGRAM */
  if ((s = socket(af, SOCK_DGRAM, bag->bag_proto)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create socket: %s\n", strerror(errno));
    goto error;
  }

  as->as_sock = s;

  if (as->as_callback)
  {
    if ((*(as->as_callback))(B, as->as_args, as->as_sock, bag, as->as_server, BkAddrGroupStateSocket) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "User complained about newly created socket\n");
      goto error;
    }
  }
  
  if (local)
  {
    // Bind to local address
    if (open_inet_local(B, as) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not open the local side of the connection\n");
      goto error;
    }
  }

  // Active connection -- UDP always succeeds immediately
  if (connect(as->as_sock, &sa, sizeof(sa)) < 0 && errno != EINPROGRESS)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not connect: %s\n", strerror(errno));
    goto error;
  }

  as->as_state = BkAddrGroupStateConnected;
  tcp_end(B, as);

  BK_RETURN(B, s);
  
 error:
  net_close(B, as);
  BK_RETURN(B, -1);
}



/**
 * Start a udp server socket (not really listen, but is parallel with TCP)
 *
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state info.
 *	@return <i>-1</i> on failure.<br>
 *	@return a new socket on success.
 */
static int
do_net_init_af_inet_udp_listen(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int s = -1;
  struct bk_addrgroup *bag;
  static int one = 1;
  int af;

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  bag = as->as_bag;

  /* af can be either AF_INET of AF_INET6 */
  af = bk_netaddr_nat2af(B,bag->bag_type); 

  /* We *know* this is udp so we can assume SOCK_DGRAM */
  if ((s = socket(af, SOCK_DGRAM, bag->bag_proto)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create socket: %s\n", strerror(errno));
    goto error;
  }

  as->as_sock = s;

  if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
  {
    bk_error_printf(B, BK_ERR_WARN, "Could not set reuseaddr on socket\n");
    // Fatal? Nah...
  }

  if (as->as_callback)
  {
    if ((*(as->as_callback))(B, as->as_args, as->as_sock, bag, as->as_server, BkAddrGroupStateSocket) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "User complained about newly created socket\n");
      goto error;
    }
  }

  // Bind to local address
  if (open_inet_local(B, as) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not open the local side of the connection\n");
    goto error;
  }

  as->as_state = BkAddrGroupStateReady;
  tcp_end(B, as);

  BK_RETURN(B, s);
  
 error:
  net_close(B, as);
  BK_RETURN(B, -1);
}



/**
 * Start a tcp connection
 *
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state info.
 *	@return <i>-1</i> on failure.<br>
 *	@return a new socket on success.
 */
static int
do_net_init_af_inet_tcp_connect(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  BK_RETURN(B,tcp_connect_start(B, as));
}



/**
 * Really get a tcp connection going. If there's no address to which to
 * attach, then just call the finish up function.
 *
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state info.
 *	@param state The state of the previous connect attempt.
 *	@return <i>-1</i> on failure.<br>
 *	@return a new socket on success.
 */
static int
tcp_connect_start(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int s = -1;
  struct bk_addrgroup *bag;
  struct bk_netinfo *local, *remote;
  struct bk_netaddr *bna;
  struct sockaddr sa;
  int af;

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  bag = as->as_bag;
  local = bag->bag_local;
  remote = bag->bag_remote;

  if (!(bna = bk_netinfo_advance_primary_address(B,remote)))
  {
    /* 
     * <WARNING>
     * If there are *no* addresses at all, do we return a useful error?
     * We probably return SysError which is about as good as we can do so
     * this is probably OK
     * </WARNING>
     */
    net_init_end(B, as);
    /* 
     * You might be tempted to return as->as_sock here since this
     * function may return or be called from things which may return
     * socket names. Do *not* be deceived! At this point in the code
     * (where we are looking for successor and therefore have already
     * made at least *one* attempt to connect) we are running "off the
     * select loop" as it were and return values are pretty meaningless
     * (certainly returning the socket number is meaningless). But much
     * more important than this is is the fact we *know* we're on the
     * connecting side of a tcp association here and thus when tcp_end()
     * returns 'as' HAS BEEN DESTROYED. Now you *could* save as->as_sock
     * before calling tcp_end(), but why bother?
     */
    BK_RETURN(B,0);
  }

  if (bk_netinfo_to_sockaddr(B, remote, bna, as->as_bag->bag_type, &sa, 0)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not remote local sockaddr\n");
    goto error;
  }
    
  /* af can be either AF_INET of AF_INET6 */
  af = bk_netaddr_nat2af(B,bag->bag_type); 

  /* We *know* this is tcp so we can assume SOCK_STREAM */
  if ((s = socket(af, SOCK_STREAM, bag->bag_proto)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create socket: %s\n", strerror(errno));
    goto error;
  }

  as->as_sock = s;

  /* Force non-blocking. We will restore this later */
  if (bk_fileutils_modify_fd_flags(B, s, O_NONBLOCK, BkFileutilsModifyFdFlagsActionAdd) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set O_NONBLOCK on socket: %s\n", strerror(errno));
    /* 
     * <WARNING>
     * Should this really be fatal? Well, given what a totally bizarre
     * situation you must be in for this to fail, jtt actually thinks this
     * is reasonable.
     * </WARNING>
     */
    goto error;
  }

  if (as->as_callback)
  {
    if ((*(as->as_callback))(B, as->as_args, as->as_sock, bag, as->as_server, BkAddrGroupStateSocket) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "User complained about newly created socket\n");
      goto error;
    }
  }

  if (local)
  {
    // Bind to local address
    if (open_inet_local(B, as) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not open the local side of the connection\n");
      goto error;
    }
  }

  // Active connection -- run through bk_run even if connect succeeds immediately
  if (connect(as->as_sock, &sa, sizeof(sa)) < 0 && errno != EINPROGRESS)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not connect: %s\n", strerror(errno));
    goto error;
  }

  if (as->as_timeout)
  {
    if (bk_run_enqueue_delta(B, as->as_run, as->as_timeout, tcp_connect_timeout, as, &as->as_eventh, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not enqueue timeout event\n");
      goto error;
    }
  }
  else
  {
    as->as_eventh = NULL;
  }

  /* Put the socket in the select loop waiting for *write* to come ready */
  if (bk_run_handle(B, as->as_run, as->as_sock, tcp_connect_activity, as, BK_RUN_WANTWRITE, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not configure socket's I/O handler\n");
    goto error;
  }


  BK_RETURN(B,s);
  
 error:
  net_close(B, as);
  BK_RETURN(B,-1);
}



/**
 * Open the local side of a socket. 
 *
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state info.
 *	@return <i>-1</i> on failure<br>
 *	@return <i>0</i> on success.
 */
static int
open_inet_local(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netinfo *local;
  struct bk_addrgroup *bag;
  struct sockaddr sa;
  int s;

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  bag = as->as_bag;
  local = bag->bag_local;
  s = as->as_sock;

  if (bk_netinfo_to_sockaddr(B, local, NULL, bag->bag_type, &sa, BK_NETINFO2SOCKADDR_FLAG_FUZZY_ANY) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create local sockaddr\n");
    goto error;
  }

  /* <TODO> Use SA_LEN macro here </TODO> */
  if (bind(s, &sa, sizeof(sa)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not bind to local address: %s\n", strerror(errno));
    goto error;
  }

  BK_RETURN(B,0);

 error:
  net_close(B, as);
  BK_RETURN(B,-1);
}



/**
 * Completely finish up an successful inet "connection". Prepare the addrgroup for the
 * callback. Make the callback.
 *
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state info.
 *	@param state The state of the connection to pass on to the user.
 *	@return <i>-1</ip> on failure.<br>
 *	@return <i>0</i> on success.
 */
static void
tcp_end(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }
  
  net_init_end(B, as);

  BK_VRETURN(B);
}




/**
 * <em>All</em net_init() threads should wind up here whether in an error
 * state or no. No matter what happens the the user *must* get that
 * callback and we want to do the right thing with as.
 *
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state info.
 *	@param state The state to pass to the user.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static void 
net_init_end(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_addrgroup *bag=NULL;

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  /*
   * Allocate the bag and fill it out as much as possible. Being unable to
   * fill out the local side is an error; being unable to fill out the
   * remote is not as this is quite normal in TCP server and unconnected
   * UDP situations.
   */
  if (as->as_state == BkAddrGroupStateConnected ||
      as->as_state == BkAddrGroupStateReady)
  {
    if (!(bag = bag_create(B)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate bk_addrgroup: %s\n", strerror(errno));
    }
    else
    {
      /* The proto and type have to be the same as the cached. */
      bag->bag_proto = as->as_bag->bag_proto;
      bag->bag_type = as->as_bag->bag_type;

      if (!(bag->bag_local = bk_netinfo_from_socket(B,as->as_sock, bag->bag_proto, BkSocketSideLocal)))
      {
	/* 
	 * This is an error since we should *always be able to do this, but
	 * we cannot goto error in this function since we need to call the
	 * callback.
	 */
	bk_error_printf(B, BK_ERR_ERR, "Could not generate local side netinfo\n");
      }
      else
      {
	bk_netinfo_set_primary_address(B,bag->bag_local,NULL);
      }

      if (!(bag->bag_remote = bk_netinfo_from_socket(B,as->as_sock, bag->bag_proto, BkSocketSideRemote)))
      {
	// This is quite common in server case, so a WARN not an ERR
	bk_error_printf(B, BK_ERR_WARN, "Could not generate remote side netinfo (possibly normal)\n");
      }
      else
      {
	bk_netinfo_set_primary_address(B,bag->bag_remote,NULL);
      }
    }
  }

  if (as->as_callback)
  {
    (*(as->as_callback))(B, as->as_args, as->as_sock, bag, as->as_server, as->as_state);
    as->as_callback = NULL;

    /*
     * If we're in a connected state, then the user has taken over this at
     * this point, so we "withdraw" it from our saved state (so we don't
     * try to close it when we clean up.
     *
     * <TODO> Unconnected UDP (we will need BkAddrGroupStateDgram or
     * something) </TODO>
     */
    if (as->as_state == BkAddrGroupStateConnected)
    {
      /* We are no longer responsible for this socket */
      as->as_sock = -1;
    }
  }

  // Bag destroyed in "error" case. 

  /* Figure out whether you should nuke this as or keep it */
  switch(as->as_state)
  {
  case BkAddrGroupStateSysError:
  case BkAddrGroupStateRemoteError:
  case BkAddrGroupStateLocalError:
  case BkAddrGroupStateConnected:
  case BkAddrGroupStateTimeout:
    as_destroy(B,as);
    break;
  case BkAddrGroupStateSocket:
  case BkAddrGroupStateReady:
  case BkAddrGroupStateClosing:
    break;
  }

  if (bag) bk_addrgroup_destroy(B,bag);
  BK_VRETURN(B);
}



/**
 * Convenience function for aborting a @a bk_net_init thread.
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state info.
 *	@param state The state to pass to the user.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static void 
net_init_abort(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  
  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  net_init_end(B,as);

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
tcp_connect_timeout(bk_s B, struct bk_run *run, void *args, const struct timeval *starttime, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct addrgroup_state *as;

  if (!run || !(as = args))
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  /* Make darn sure this is NULL in the case of emergency :-) */
  as->as_eventh = NULL;
  
  bk_error_printf(B, BK_ERR_WARN, "Connection to %s timed out\n", as->as_bag->bag_remote->bni_pretty);

  net_close(B, as);

  /* Check for more addresses */
  as->as_state = BkAddrGroupStateTimeout;
  if (tcp_connect_start(B, as) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Failed to attempt connection to new address\n");
  }
  BK_VRETURN(B);
}



/**
 * Activity has occurred on a connecting TCP socket check. Check to see what
 * the disposition of this connection is. If it's an error, check for
 * another address. If it's OK, head for the end
 *
 *	@param B BAKA thread/global state.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static void
tcp_connect_activity(bk_s B, struct bk_run *run, int fd, u_int gottype, void *args, const struct timeval *startime)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct sockaddr sa;
  struct bk_addrgroup *bag;
  struct addrgroup_state *as;

  if (!run || !(as = args))
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (BK_FLAG_ISSET(gottype, BK_RUN_READREADY))
  {
    bk_error_printf(B, BK_ERR_ERR, "How did I get readready? \n");
    goto error;
  }

  if (BK_FLAG_ISSET(gottype, BK_RUN_XCPTREADY))
  {
    bk_error_printf(B, BK_ERR_ERR, "How did I get xctpready?\n");
    goto error;
  }
  
  if (BK_FLAG_ISSET(gottype, BK_RUN_DESTROY))
  {
    // user-initiated process shutdown; not an error at all
    bk_error_printf(B, BK_ERR_NOTICE, "Our run environment has been destroyed\n");
    goto error;
  }

  if (BK_FLAG_ISSET(gottype, BK_RUN_BAD_FD))
  {
    bk_error_printf(B, BK_ERR_ERR, "%d has become a bad fd\n", fd);
    goto error;
  }

  if (BK_FLAG_ISSET(gottype, BK_RUN_CLOSE))
  {
    // We're looping around in our own callbacks (most likely) 
    BK_VRETURN(B);
  }

  bag = as->as_bag;

  /*
   * Make sure that the timeout event cannot fire and null out event handle.
   */
  if (as->as_eventh)
  {
    if (bk_run_dequeue(B, as->as_run, as->as_eventh, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not dequeue timeout event\n");
    }
  }
  as->as_eventh = NULL;
  
  /* 
   * No matter *what*, we need to withdraw the socket from bk_run since
   * we're either about to get a new socket (new connection attempt) or we
   * want to let the caller take over the socket with a "clean slate" as it
   * were.
   */
  if (bk_run_close(B,as->as_run, as->as_sock, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not withdraw socket from run\n");
  }

  if (bk_netinfo_to_sockaddr(B, bag->bag_remote, NULL, BkNetinfoTypeUnknown, &sa, 0 < 0))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not convert remote address to sockaddr any more\n");
    goto error;
  }

  /*
   * Run a second connect to find out what has happened to our
   * descriptor. The specific states of this seem to be platform
   * specific, which is a little disturbing. The following illustrates
   * the confusion. 1st call and 2nd call refer to calls to connect(2)
   * *following* the return from select(2) (i.e. when we have idea of
   * what has happened to the socket.
   * 
   * 		Successful connection
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
   * first connect(2) following a successful connect unlike linux
   * which returns 0 on this call (and EISCONN only after
   * that). Furthermore it, along with linux, returns the actual error
   * on a connect failure (as a state of that first call), while
   * openbsd simply return EINVALID which tell you *nothing* about what
   * actually went wrong!
   *
   * However, it is at lest the case that all platforms seem to return
   * either 0 or EISCONN on a successful connect(2). So we can at least
   * code for this case properly.
   *
   * I wonder what Windows does....
   */
  if (connect(fd, &sa, sizeof(sa))<0 && errno != EISCONN)
  {
    // calls to bk_error_printf() can result in errno changing. Sigh...
    int connect_errno = errno;

    bk_error_printf(B, BK_ERR_ERR, "Connect to %s failed: %s\n", bag->bag_remote->bni_pretty, strerror(errno)); 
    net_close(B,as);

    if (connect_errno == BK_SECOND_REFUSED_CONNECT_ERRNO)
      as->as_state = BkAddrGroupStateRemoteError;
    else
      as->as_state = bk_net_init_sys_error(B,errno);

    tcp_connect_start(B,as);
    BK_VRETURN(B);
  }
  
  as->as_state = BkAddrGroupStateConnected;
  tcp_end(B, as);

  BK_VRETURN(B);

 error:
  /*
   * <WARNING>We can probably abort since this should be running off the bk_run
   * loop</WARNING>
   */
  net_init_abort(B, as);
  BK_VRETURN(B);
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
  int af;
  int s = -1;
  struct bk_addrgroup *bag = NULL;
  int one = 1;
  int ret;

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  bag = as->as_bag;

  af = bk_netaddr_nat2af(B, bag->bag_type); 
  
  if ((s = socket(af, SOCK_STREAM, bag->bag_proto)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create socket: %s\n", strerror(errno));
    goto error;
  }
  /* INSERT NO CODE HERE. ONLY *YOU* CAN PREVENT DESCRIPTOR LEAKS */
  as->as_sock = s;

  /* Force non-blocking. We will restore this later */
  if (bk_fileutils_modify_fd_flags(B, s, O_NONBLOCK, BkFileutilsModifyFdFlagsActionAdd) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set O_NONBLOCK on socket: %s\n", strerror(errno));
    /* 
     * <WARNING>
     * Should this really be fatal? Well, given what a totally bizarre
     * situation you must be in for this to fail, jtt actually thinks this
     * is reasonable.
     * </WARNING>
     */
    goto error;
  }

  if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
  {
    bk_error_printf(B, BK_ERR_WARN, "Could not set reuseaddr on socket\n");
    // Fatal? Nah...
  }

  if (as->as_callback)
  {
    if ((*(as->as_callback))(B, as->as_args, as->as_sock, bag, as->as_server, BkAddrGroupStateSocket) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "User complained about newly created socket\n");
      goto error;
    }
  }

  // Bind to local address
  if (open_inet_local(B, as) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not open the local side of the connection\n");
    goto error;
  }
  
  if (listen(s, as->as_backlog) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "listen failed: %s\n", strerror(errno));
    goto error;
  }
  
  as->as_state = BkAddrGroupStateReady;

  /* Put the socket in the select loop waiting for *write* to come ready */
  if (bk_run_handle(B, as->as_run, s, tcp_listen_activity, as, BK_RUN_WANTREAD, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not configure socket's I/O handler\n");
    goto error;
  }

  /* 
   * Inform the caller that we are now ready and waiting (or will be when
   * we return back to the to the select(2) loop. Fill out a bag if the
   * caller requested it.
   */
  if (as->as_callback)
  {
    struct bk_addrgroup *nbag = NULL;

    if (!(nbag = bag_create(B)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate bk_addrgroup\n");
      // This is kind of an error, but we must make callback so no goto
    }
    else
    {
      /* The proto and type have to be the same as the cached. */
      nbag->bag_proto = as->as_bag->bag_proto;
      nbag->bag_type = as->as_bag->bag_type;

      if (!(nbag->bag_local = bk_netinfo_from_socket(B, as->as_sock, nbag->bag_proto, BkSocketSideLocal)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not generate local side netinfo\n");
	// This is kind of an error, but we must make callback so no goto
      }
      else
      {
	bk_netinfo_set_primary_address(B,nbag->bag_local,NULL);
      }
    }

    ret = (*(as->as_callback))(B, as->as_args, s, nbag, as->as_server, as->as_state);

    if (nbag) bk_addrgroup_destroy(B, nbag);
    if (ret < 0)
      goto error;
  }
  
  BK_RETURN(B,s);

 error:
  net_close(B, as);
  BK_RETURN(B,-1);
}



/**
 * Activity detected on a TCP listening socket. Try to accept the
 * connection and do the right thing (now what *is* the Right Thing..?)
 *
 *	@param B BAKA thread/global state.
 *	@param B BAKA thread/global state.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static void 
tcp_listen_activity(bk_s B, struct bk_run *run, int fd, u_int gottype, void *args, const struct timeval *startime)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct addrgroup_state *as, *nas;
  struct sockaddr sa;
  int len = sizeof(sa);
  int newfd;

  if (!run || !(as = args))
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (BK_FLAG_ISSET(gottype, BK_RUN_WRITEREADY))
  {
    bk_error_printf(B, BK_ERR_ERR, "How did I get writeready? \n");
    goto error;
  }

  if (BK_FLAG_ISSET(gottype, BK_RUN_XCPTREADY))
  {
    bk_error_printf(B, BK_ERR_ERR, "How did I get xctpready?\n");
    goto error;
  }
  
  if (BK_FLAG_ISSET(gottype, BK_RUN_DESTROY))
  {
    // user-initiated process shutdown; not an error at all
    bk_error_printf(B, BK_ERR_NOTICE, "Our run environment has been destroyed\n");
    goto error;
  }

  if (BK_FLAG_ISSET(gottype, BK_RUN_BAD_FD))
  {
    bk_error_printf(B, BK_ERR_ERR, "%d has become a bad fd\n", fd);
    goto error;
  }

  if (BK_FLAG_ISSET(gottype, BK_RUN_CLOSE))
  {
    // We're looping around in our own callbacks (most likely) 
    BK_VRETURN(B);
  }

  if ((newfd = accept(as->as_sock, &sa, &len)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "accept failed: %s\n",strerror(errno));
    as->as_state = bk_net_init_sys_error(B, errno);
    goto error;
  }

  if (!(nas = as_server_copy(B,as, newfd)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Couldn't create new addrgroup_state\n");
    goto error;
  }
  
  nas->as_state = BkAddrGroupStateConnected;
  
  tcp_end(B, nas);
  BK_VRETURN(B);

 error:
  /*
   * <WARNING>We can probably abort since this should be running off the bk_run
   * loop</WARNING>
   */
  net_init_abort(B, as);
  BK_VRETURN(B);
}



/**
 * Close up tcp in the addrgroup way. Remove the socket from run and close
 * it up. Set the state to -1 so we wont try this again until we need to.
 *
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state info.
 */
static void
net_close(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (as->as_sock == -1)
  {
    /* We're already closed -- go away */
    BK_VRETURN(B);
  }

  if (bk_run_close(B,as->as_run, as->as_sock, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not withdraw socket from run\n");
  }
  close(as->as_sock);				// Actually close the socket
  as->as_sock = -1;

  BK_VRETURN(B);
}



/*
 * bk_net_init sanity checking. This code is exported here so that it
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
  struct bk_netaddr *bna = NULL;

  if (!bag || (!local && !remote))
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  /* Just a bit of sanity checking */
  if (local && local->bni_bpi)
  {
    bag->bag_proto = local->bni_bpi->bpi_proto;		/* Cache the proto */
  }
  else if (remote && remote->bni_bpi)
  {
    bag->bag_proto = remote->bni_bpi->bpi_proto;	/* Cache the proto */
  }
  else
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
  }
    
  /*
   * Check on address family sanity, but this is more complicated than
   * above. If both netinfos have addresses then make sure that the
   * address families match (the insertion routines should ensure that no
   * netinfo contains a heterogeneous set of addrs); if only one or the
   * other of netinfos have netaddrs, then use that value; if both
   * netinfos *lack* addresses, then flag a fatal error and return.
   */
  if (local && (bna = bk_netinfo_get_addr(B, local)))
  {
    bag->bag_type = bna->bna_type;
  }

  if (remote && (bna = bk_netinfo_get_addr(B, remote)))
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
      bag->bag_type = bna->bna_type;
    }
  }

  if (bag->bag_type == BkNetinfoTypeUnknown)
  {
    bk_error_printf(B, BK_ERR_ERR, "Address family not determinable\n");
    goto error;
  }

  BK_RETURN(B,0);

 error:
  BK_RETURN(B,-1);
  
}




/**
 * Take over a listening socket and handle its service. Despite it's name
 * this function must appear here so it can reference a static callback
 * routine. Oh well.
 *
 *	@param B BAKA thread/global state.
 *	@param run @a bk_run structure.
 *	@param s The socket to assume control over.
 *	@param sercurenets IP address filtering.
 *	@param callback Function to call back when there's a connection
 *	@param args User arguments to supply to above.
 *	@param flags User flags.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_netutils_commandeer_service(bk_s B, struct bk_run *run, int s, char *securenets, bk_bag_callback_f callback, void *args, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct addrgroup_state *as = NULL; 

  if (!callback)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!(as = as_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate addrgroup state: %s\n", strerror(errno));
    goto error;
  }

  as->as_state = BkAddrGroupStateReady;
  as->as_sock = s;
  as->as_timeout = 0;
  as->as_callback = callback;
  as->as_args = args;
  as->as_user_flags = flags;
  as->as_run = run;

  if (!(as->as_bag->bag_local = bk_netinfo_from_socket(B, s, 0, BkSocketSideLocal)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not determine local address information\n");
    goto error;
  }
  
  if (bk_run_handle(B, as->as_run, s, tcp_listen_activity, as, BK_RUN_WANTREAD, 0)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not configure socket's I/O handler\n");
    goto error;
  }

  BK_RETURN(B,0);

 error:
  if (as) as_destroy(B,as);
  BK_RETURN(B,-1);
  
}



/**
 * Retrieve the socket from the server handle
 *
 *	@param B BAKA thread/global state.
 *	@param server_handle The handle supplied to the connect callbacks.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>socket</i> on success.
 */
int
bk_addrgroup_get_server_socket(bk_s B, void *server_handle)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct addrgroup_state *as;

  if (!(as = server_handle))
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  BK_RETURN(B,as->as_sock);
}




/**
 * Shutdown a server referenced by the server handle.
 *
 *	@param B BAKA thread/global state.
 *	@param server_handle The handle supplied to the connect callbacks.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_addrgroup_server_close(bk_s B, void *server_handle)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct addrgroup_state *as=server_handle;

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  as_destroy(B, as);
  BK_RETURN(B,0);
}




/**
 * "Clone" a new connected @a addrgroup_state from the server 
 *	@param B BAKA thread/global state.
 *	@param oas Old @a addrgroup_state to copy.
 *	@param newfd Use this fd instead of the one from @a oas.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a addrgroup_state on success.
 */
static struct addrgroup_state *
as_server_copy(bk_s B, struct addrgroup_state *oas , int s)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct addrgroup_state *as = NULL;

  if (!oas)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,NULL);
  }

  if (!(as = as_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate new as: %s\n", strerror(errno));
    goto error;
  }

  as->as_flags = oas->as_flags;
  as->as_sock = s;
  /* Ignore bag */
  as->as_timeout = oas->as_timeout;
  as->as_callback = oas->as_callback;
  as->as_args = oas->as_args;
  /* Ignore eventh */
  as->as_run = oas->as_run;
  /* Why are we copying this? I don't know. Leave me alone */
  as->as_backlog = oas->as_backlog; 
  as->as_user_flags = oas->as_user_flags;
  /* Actually point the server at the server */
  as->as_server = oas;
  
  BK_RETURN(B,as);

 error:
  if (as) as_destroy(B,as);
  BK_RETURN(B,NULL);
}



/**
 * Return the correct bk_addrgroup_state_e error type based on errno.
 *	@param B BAKA thread/global state.
 *	@param errno The errno value on which to base our computation.
 *	@return @a bk_addrgroup_state_e.
 */
bk_addrgroup_state_e 
bk_net_init_sys_error(bk_s B, int lerrno)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  switch (lerrno)
  {
  case EADDRINUSE:
    BK_RETURN(B, BkAddrGroupStateLocalError);
    break;
  case ECONNREFUSED:
  case ENETUNREACH:
    BK_RETURN(B, BkAddrGroupStateRemoteError);
    break;
  case ETIMEDOUT:
    BK_RETURN(B, BkAddrGroupStateTimeout);
    break;
  default:
    break;
  }
  BK_RETURN(B, BkAddrGroupStateSysError);
}
