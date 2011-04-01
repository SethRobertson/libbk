#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2001-2011";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2001-2011 The Authors. All rights reserved.
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
 * Addrgroup.c: This file takes care of initializing the network for data
 * transmission. We try to support as many address families and protocol
 * types as we can.
 */
#include <libbk.h>
#include "libbk_internal.h"

// Preamble sent in dgram service. If this changes *all* libbk dgram clients must change.
#define DGRAM_PREAMBLE	"PjIRi6yAvs3r4BG1"

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
  u_long		as_timeout;		///< Timeout in msecs
  bk_bag_callback_f	as_callback;		///< Called when sock ready
  void *		as_args;		///< User args for callback
  void *		as_eventh;		///< Timeout event handle
  struct bk_run *	as_run;			///< run handle.
  int			as_backlog;		///< Listen backlog.
  bk_flags		as_user_flags;		///< Flags passed in from user.
  struct addrgroup_state *as_server;		///< Server as (generally pointing at myself).
};



static struct bk_addrgroup *bag_create(bk_s B);
static void bag_destroy(bk_s B, struct bk_addrgroup *bag);
static struct addrgroup_state *as_create(bk_s B);
static void as_destroy(bk_s B, struct addrgroup_state *as);
static int net_init_check_sanity(bk_s B, struct bk_netinfo *local, struct bk_netinfo *remote, struct bk_addrgroup *bag);
static int do_net_init_af_inet(bk_s B, struct addrgroup_state *as);
static int do_net_init_af_local(bk_s B, struct addrgroup_state *as);
static int do_net_init_stream(bk_s B, struct addrgroup_state *as);
static int do_net_init_dgram(bk_s B, struct addrgroup_state *as);
static int do_net_init_dgram_connect(bk_s B, struct addrgroup_state *as);
static int do_net_init_stream_connect(bk_s B, struct addrgroup_state *as);
static int do_net_init_listen(bk_s B, struct addrgroup_state *as);
static int open_local(bk_s B, struct addrgroup_state *as);
static void stream_end(bk_s B, struct addrgroup_state *as);
static void dgram_end(bk_s B, struct addrgroup_state *as);
static void stream_connect_timeout(bk_s B, struct bk_run *run, void *args, const struct timeval starttime, bk_flags flags);
static void stream_connect_activity(bk_s B, struct bk_run *run, int fd, u_int gottype, void *args, const struct timeval *startime);
static void net_close(bk_s B, struct addrgroup_state *as);
static void listen_activity(bk_s B, struct bk_run *run, int fd, u_int gottype, void *args, const struct timeval *startime);
static void net_init_end(bk_s B, struct addrgroup_state *as);
static void net_init_abort(bk_s B, struct addrgroup_state *as);
static struct addrgroup_state *as_server_copy(bk_s B, struct addrgroup_state *oas , int s);
static int bag2socktype(bk_s B, struct bk_addrgroup *bag, bk_flags flags);



/**
 * Create an @a bk_addrgroup structure.  Caller does not
 * need to call bk_addrgroup_ref().
 *
 * THREADS: MT-SAFE
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

  bag->bag_refcount = 1;

  BK_RETURN(B, bag);

 error:
  if (bag) bag_destroy(B, bag);
  BK_RETURN(B, NULL);
}



/**
 * Decrement reference count and destroy
 * a @a bk_addrgroup if the count is zero.
 *
 * THREADS: MT-SAFE (assuming different bag)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param bag The @a bk_addrgroup to destroy.
 */
static void
bag_destroy(bk_s B, struct bk_addrgroup *bag)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bag)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  bag->bag_refcount--;
  if (!bag->bag_refcount)
  {
    if (bag->bag_local) bk_netinfo_destroy(B, bag->bag_local);
    if (bag->bag_remote) bk_netinfo_destroy(B, bag->bag_remote);

    if (bag->bag_ssl && bag->bag_ssl_destroy)
      (*bag->bag_ssl_destroy)(B, bag->bag_ssl, 0);

    free(bag);
  }

  BK_VRETURN(B);
}



/**
 * Increment the reference count on a bag.
 *
 * @param B BAKA Thread/global state
 * @param bag bag to reference
 */
void
bk_addrgroup_ref(bk_s B, struct bk_addrgroup *bag)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bag)
  {
    bk_error_printf(B, BK_ERR_ERR, "Internal error: invalid arguments.\n");
    BK_VRETURN(B);
  }

  bag->bag_refcount++;

  BK_VRETURN(B);
}



/**
 * Decrement the reference count on a bag.  You probably want
 * to call destroy() instead of unref() unless you know what
 * you're doing.
 *
 * @param B BAKA Thread/global state
 * @param bag bag to reference
 */
void
bk_addrgroup_unref(bk_s B, struct bk_addrgroup *bag)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bag)
  {
    bk_error_printf(B, BK_ERR_ERR, "Internal error: invalid arguments.\n");
    BK_VRETURN(B);
  }

  bag->bag_refcount--;

  BK_VRETURN(B);
}



/**
 * Public interface to destroy routine
 *
 * THREADS: MT-SAFE (assuming different bag)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param bag The @a bk_addrgroup to destroy.
 */
void
bk_addrgroup_destroy(bk_s B, struct bk_addrgroup *bag)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bag)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  bag_destroy(B, bag);
  BK_VRETURN(B);
}




/**
 * Create an @a addrgroup_state
 *
 * THREADS: MT-SAFE
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

  BK_RETURN(B, as);

 error:
  if (as) as_destroy(B, as);

  BK_RETURN(B, NULL);
}



/**
 * Destroy a @a addrgroup_state.
 *
 * THREADS: MT-SAFE (assuming different as)
 * THREADS: REENTRANT (otherwise)
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
    bag_destroy(B, as->as_bag);
    as->as_bag = NULL;
  }

  /* If the callback has not been called already, do so with flag */
  if (as->as_callback)
  {
    (*(as->as_callback))(B, as->as_args, -1, NULL, as->as_server, as->as_state);
  }

  if (as->as_eventh) bk_run_dequeue(B, as->as_run, as->as_eventh, 0);

  /* <WARNING> Is this correct??? </WARNING> */
  net_close(B, as);
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
 * THREADS: MT-SAFE (assuming local and remote are different)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param local @a bk_netinfo of the local side.
 *	@param remote @a bk_netinfo of the remote side.
 *	@param timeout Timeout information (msecs).
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
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
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

  if (local && !(bag->bag_local = bk_netinfo_clone(B, local)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not clone local netinfo\n");
    goto error;
  }

  if (remote && !(bag->bag_remote = bk_netinfo_clone(B, remote)))
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
 * THREADS: MT-SAFE (assuming different as)
 * THREADS: REENTRANT (otherwise)
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
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  bag = as->as_bag;

  switch (bag->bag_proto)
  {
  case IPPROTO_TCP:
    ret = do_net_init_stream(B, as);
    break;

  case IPPROTO_UDP:
    ret = do_net_init_dgram(B, as);
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
 * THREADS: MT-SAFE (assuming different as)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state information.
 *	@return <i>-1</i> on failure.<br>
 *	@return new socket on success.
 */
static int
do_net_init_stream(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_addrgroup *bag;
  int ret;

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  bag = as->as_bag;

  if (bag->bag_remote)
  {
    ret = do_net_init_stream_connect(B, as);
  }
  else
  {
    ret = do_net_init_listen(B, as);
  }

  BK_RETURN(B, ret);
}



/**
 * Open an AF_LOCAL connection
 *
 * THREADS: MT-SAFE (assuming different as)
 * THREADS: REENTRANT (otherwise)
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
  struct bk_addrgroup *bag;

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  bag = as->as_bag;

  switch (bag->bag_proto)
  {
  case BK_GENERIC_STREAM_PROTO:
    ret = do_net_init_stream(B, as);
    break;

  case BK_GENERIC_DGRAM_PROTO:
    ret = do_net_init_dgram(B, as);
    break;

  default:
    bk_error_printf(B, BK_ERR_ERR, "Unknown generic protocol: %d\n", bag->bag_proto);
    goto error;
  }

  BK_RETURN(B, ret);

 error:
  net_close(B, as);
  BK_RETURN(B, -1);
}




/**
 * Open a udp network.
 *
 * THREADS: MT-SAFE (assuming different as)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state info.
 *	@return <i>-1</i> on failure.<br>
 *	@return a new socket on success.
 */
static int
do_net_init_dgram(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_addrgroup *bag;
  int ret = 0;

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  bag = as->as_bag;

  if (bag->bag_remote)
  {
    ret = do_net_init_dgram_connect(B, as);
  }
  else
  {
    ret = do_net_init_listen(B, as);
  }

  BK_RETURN(B, ret);
}



/**
 * Start a udp connection
 *
 * THREADS: MT-SAFE (assuming different as)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state info.
 *	@return <i>-1</i> on failure.<br>
 *	@return a new socket on success.
 */
static int
do_net_init_dgram_connect(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int s = -1;
  struct bk_addrgroup *bag;
  struct bk_netinfo *local, *remote;
  struct bk_netaddr *bna;
  bk_sockaddr_t bs;
  int af;
  u_int cnt;
  socklen_t socklen;

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  bag = as->as_bag;
  local = bag->bag_local;
  remote = bag->bag_remote;

  if (!(bna = bk_netinfo_advance_primary_address(B, remote)))
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
     * connecting side of a tcp association here and thus when stream_end()
     * returns 'as' HAS BEEN DESTROYED. Now you *could* save as->as_sock
     * before calling stream_end(), but why bother?
     */
    BK_RETURN(B, 0);
  }

  /* af can be either AF_INET of AF_INET6 */
  af = bk_netaddr_nat2af(B, bag->bag_type);

  if ((s = socket(af, SOCK_DGRAM, (bag->bag_proto == BK_GENERIC_DGRAM_PROTO)?0:bag->bag_proto)) < 0)
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
    if (open_local(B, as) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not open the local side of the connection\n");
      goto error;
    }
  }

  if (bk_netinfo_to_sockaddr(B, remote, bna, as->as_bag->bag_type, &bs, 0)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not remote local sockaddr\n");
    goto error;
  }

  BK_GET_SOCKADDR_LEN(B, &(bs.bs_sa), socklen);

  // Active connection -- UDP always succeeds immediately
  if ((connect(as->as_sock, &(bs.bs_sa), socklen) < 0) && (errno != EINPROGRESS))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not connect: %s\n", strerror(errno));
    goto error;
  }

  /*
   * <WARNING>
   * THIS IS TOTALLY UNNECESSARY CODE WHICH IS MAITAINED ONLY FOR
   * BACKWARD COMPATIBILITY WITH THINGS COMPILED AGAINST AN OLDER
   * VERSION OF LIBBK. YOU DO NOT NEED TO READ ANY DATA IN ORDER TO
   * OBTAIN THE REMOTE ADDRESS AND WHY JTT DIDN'T REALIZE THIS IN 2001
   * PASSES ALL UNDERSTANDING.
   *
   * Anyway do not set BK_NET_FLAG_BAKA_UDP unless you absolutely have to.
   * </WARNING>
   */
  if (BK_FLAG_ISSET(as->as_user_flags, BK_NET_FLAG_BAKA_UDP))
  {
    cnt = 0;
    while(cnt < sizeof(DGRAM_PREAMBLE))
    {
      int nbytes;
      const char *dgram_preamble = DGRAM_PREAMBLE;

      if ((nbytes = write(as->as_sock, dgram_preamble + cnt, sizeof(DGRAM_PREAMBLE) - cnt)) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not write out dgram pramble: %s\n", strerror(errno));
	goto error;
      }
      cnt += nbytes;
    }
  }

  as->as_state = BkAddrGroupStateConnected;
  dgram_end(B, as);

  BK_RETURN(B, s);

 error:
  net_close(B, as);
  BK_RETURN(B, -1);
}



/**
 * Get a stream connection going. If there's no address to which to
 * attach, then just call the finish up function.
 *
 * THREADS: MT-SAFE (assuming different as)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state info.
 *	@return <i>-1</i> on failure.<br>
 *	@return a new socket on success.
 */
static int
do_net_init_stream_connect(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int s = -1;
  struct bk_addrgroup *bag;
  struct bk_netinfo *local, *remote;
  struct bk_netaddr *bna;
  bk_sockaddr_t bs;
  int af;
  socklen_t socklen;

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  bag = as->as_bag;
  local = bag->bag_local;
  remote = bag->bag_remote;

  if (!(bna = bk_netinfo_advance_primary_address(B, remote)))
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
     * connecting side of a tcp association here and thus when stream_end()
     * returns 'as' HAS BEEN DESTROYED. Now you *could* save as->as_sock
     * before calling stream_end(), but why bother?
     */
    BK_RETURN(B, 0);
  }

  /* af can be either AF_INET of AF_INET6 */
  af = bk_netaddr_nat2af(B, bag->bag_type);

  /* We *know* this is tcp so we can assume SOCK_STREAM */
  if ((s = socket(af, SOCK_STREAM, (bag->bag_proto == BK_GENERIC_STREAM_PROTO)?0:bag->bag_proto)) < 0)
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
    if (open_local(B, as) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not open the local side of the connection\n");
      goto error;
    }
  }

  if (bk_netinfo_to_sockaddr(B, remote, bna, as->as_bag->bag_type, &bs, 0)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not remote local sockaddr\n");
    goto error;
  }

  BK_GET_SOCKADDR_LEN(B, &(bs.bs_sa), socklen);

  // Active connection -- run through bk_run even if connect succeeds immediately
  if (connect(as->as_sock, &(bs.bs_sa), socklen) < 0 && errno != EINPROGRESS)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not connect: %s\n", strerror(errno));
    goto error;
  }

  if (as->as_timeout)
  {
    if (bk_run_enqueue_delta(B, as->as_run, as->as_timeout, stream_connect_timeout, as, &as->as_eventh, 0) < 0)
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
  if (bk_run_handle(B, as->as_run, as->as_sock, stream_connect_activity, as, BK_RUN_WANTWRITE, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not configure socket's I/O handler\n");
    goto error;
  }


  BK_RETURN(B, s);

 error:
  net_close(B, as);
  as->as_state = bk_net_init_sys_error(B, errno);
  do_net_init_stream_connect(B, as);
  BK_RETURN(B, -1);
}



/**
 * Open the local side of a socket.
 *
 * THREADS: MT-SAFE (assuming different as)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state info.
 *	@return <i>-1</i> on failure<br>
 *	@return <i>0</i> on success.
 */
static int
open_local(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netinfo *local;
  struct bk_addrgroup *bag;
  bk_sockaddr_t bs;
  int s;
  socklen_t socklen;

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  bag = as->as_bag;
  local = bag->bag_local;
  s = as->as_sock;

  if (bk_netinfo_to_sockaddr(B, local, NULL, bag->bag_type, &bs, BK_NETINFO2SOCKADDR_FLAG_FUZZY_ANY) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create local sockaddr\n");
    goto error;
  }

  BK_GET_SOCKADDR_LEN(B, &(bs.bs_sa), socklen);

  /* <TODO> Use SA_LEN macro here </TODO> */
  if (bind(s, &(bs.bs_sa), socklen) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not bind to local address: %s\n", strerror(errno));
    goto error;
  }

  BK_RETURN(B, 0);

 error:
  net_close(B, as);
  BK_RETURN(B, -1);
}



/**
 * Completely finish up an successful inet "connection". Prepare the addrgroup for the
 * callback. Make the callback.
 *
 * THREADS: MT-SAFE (assuming different as)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state info.
 *	@param state The state of the connection to pass on to the user.
 *	@return <i>-1</ip> on failure.<br>
 *	@return <i>0</i> on success.
 */
static void
stream_end(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  net_init_end(B, as);

  BK_VRETURN(B);
}




/**
 * Completely finish up an successful dgram servuce. Prepare the addrgroup for the
 * callback. Make the callback.
 *
 * THREADS: MT-SAFE (assuming different as)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state info.
 *	@param state The state of the connection to pass on to the user.
 *	@return <i>-1</ip> on failure.<br>
 *	@return <i>0</i> on success.
 */
static void
dgram_end(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
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
 * THREADS: MT-SAFE (assuming different as)
 * THREADS: REENTRANT (otherwise)
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
  struct bk_addrgroup *bag = NULL;

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

      if (!(bag->bag_local = bk_netinfo_from_socket(B, as->as_sock, bag->bag_proto, BkSocketSideLocal)))
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
	bk_netinfo_set_primary_address(B, bag->bag_local, NULL);
      }

      if (!(bag->bag_remote = bk_netinfo_from_socket(B, as->as_sock, bag->bag_proto, BkSocketSideRemote)))
      {
	// This is quite common in server case, so a WARN not an ERR
	bk_error_printf(B, BK_ERR_WARN, "Could not generate remote side netinfo (possibly normal)\n");
      }
      else
      {
	bk_netinfo_set_primary_address(B, bag->bag_remote, NULL);
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
    as_destroy(B, as);
    break;
  case BkAddrGroupStateSocket:
  case BkAddrGroupStateReady:
  case BkAddrGroupStateClosing:
    break;
  }

  if (bag) bk_addrgroup_destroy(B, bag);
  BK_VRETURN(B);
}



/**
 * Convenience function for aborting a @a bk_net_init thread.
 *
 * THREADS: MT-SAFE (assuming different as)
 * THREADS: REENTRANT (otherwise)
 *
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

  net_init_end(B, as);

  BK_VRETURN(B);
}



/**
 * Connect timeout function. The interface conforms to a standard BAKA
 * event API
 *
 * THREADS: MT-SAFE (assuming different as)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param run The run structure passed up.
 *	@param args The @a addrgroup_state structure.
 *	@param starttime The start time of the last @a bk_run loop.
 *	@param flags Random flags.
 */
static void
stream_connect_timeout(bk_s B, struct bk_run *run, void *args, const struct timeval starttime, bk_flags flags)
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
  if (do_net_init_stream_connect(B, as) < 0)
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
 * THREADS: MT-SAFE (assuming different as/args)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param run Baka run environment
 *	@param fd File descriptor of activity
 *	@param gottype Type of activity
 *	@param args Private data
 *	@param starttide Official Time of activity
 */
static void
stream_connect_activity(bk_s B, struct bk_run *run, int fd, u_int gottype, void *args, const struct timeval *startime)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  bk_sockaddr_t bs;
  struct bk_addrgroup *bag;
  struct addrgroup_state *as = (struct addrgroup_state *)args;
  socklen_t socklen;

  if (!run || !as)
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
    goto error;  // Goto error is quite possibly wrong, in which case BK_RETURN(B) is probably right.
  }

  if (BK_FLAG_ISSET(gottype, BK_RUN_CLOSE))
  {
    bk_error_printf(B, BK_ERR_NOTICE, "Connection closing while waiting for connect activity\n");
    //goto error;
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
  if (bk_run_close(B, as->as_run, as->as_sock, BK_RUN_CLOSE_FLAG_NO_HANDLER) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not withdraw socket from run\n");
  }

  if (bk_netinfo_to_sockaddr(B, bag->bag_remote, NULL, BkNetinfoTypeUnknown, &bs, 0 < 0))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not convert remote address to sockaddr any more\n");
    goto error;
  }

  BK_GET_SOCKADDR_LEN(B, &(bs.bs_sa), socklen);

  /*
   * For network addrgroups, run a second connect to find out what has
   * happened to our descriptor. The specific states of this seem to be
   * platform specific, which is a little disturbing. The following
   * illustrates the confusion. 1st call and 2nd call refer to calls to
   * connect(2) *following* the return from select(2) (i.e. when we have
   * idea of what has happened to the socket.
   *
   *		Successful connection
   *		--------------------
   *		1st Call		2nd call
   * linux:	Success (returns 0)	EISCONN
   * solaris:	EISCONN			EISCONN
   * openbsd:	EISCONN			EISCONN
   *
   *		RST connection
   *		---------------
   *		1st Call		2nd call
   * linux:	ECONNREFUSED		EINPROGRESS
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
  if (bk_addrgroup_network(B, bag, 0) && (connect(fd, &(bs.bs_sa), socklen) < 0) && (errno != EISCONN))
  {
    // calls to bk_error_printf() can result in errno changing. Sigh...
    int connect_errno = errno;

    bk_error_printf(B, BK_ERR_WARN, "Connect to %s failed: %s\n", bag->bag_remote->bni_pretty, strerror(errno));
    net_close(B, as);

    if (connect_errno == BK_SECOND_REFUSED_CONNECT_ERRNO)
      as->as_state = BkAddrGroupStateRemoteError;
    else
      as->as_state = bk_net_init_sys_error(B, errno);

    do_net_init_stream_connect(B, as);
    BK_VRETURN(B);
  }
  as->as_state = BkAddrGroupStateConnected;
  stream_end(B, as);

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
 *
 * THREADS: MT-SAFE (assuming different as)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state info.
 *	@return <i>-1</i> on failure.<br>
 *	@return a new socket on success.
 */
static int
do_net_init_listen(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int af;
  int s = -1;
  struct bk_addrgroup *bag = NULL;
  int one = 1;
  int ret;
  int socktype;
  int sockproto;

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  bag = as->as_bag;

  if ((socktype = bag2socktype(B, bag, 0)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not determine addrgroup socket type\n");
    goto error;
  }

  if ((bag->bag_proto == BK_GENERIC_STREAM_PROTO) || (bag->bag_proto == BK_GENERIC_DGRAM_PROTO))
    sockproto = 0;
  else
    sockproto = bag->bag_proto;

  af = bk_netaddr_nat2af(B, bag->bag_type);

  if ((s = socket(af, socktype, sockproto)) < 0)
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
    bk_error_printf(B, BK_ERR_ERR, "Could not set reuseaddr on socket\n");
    // Fatal? Nah... but may cause problems later on, so warn at ERR level
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
  if (open_local(B, as) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not open the local side of the connection\n");
    goto error;
  }

  if ((socktype == SOCK_STREAM) && listen(s, as->as_backlog) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "listen failed: %s\n", strerror(errno));
    goto error;
  }

  as->as_state = BkAddrGroupStateReady;

  if (bk_run_handle(B, as->as_run, s, listen_activity, as, BK_RUN_WANTREAD, 0) < 0)
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
	bk_netinfo_set_primary_address(B, nbag->bag_local, NULL);
      }
    }

    ret = (*(as->as_callback))(B, as->as_args, s, nbag, as->as_server, as->as_state);

    if (nbag) bk_addrgroup_destroy(B, nbag);
    if (ret < 0)
      goto error;
  }

  BK_RETURN(B, s);

 error:
  net_close(B, as);
  BK_RETURN(B, -1);
}



/**
 * Activity detected on a stream listening socket. Try to accept the
 * connection and do the right thing (now what *is* the Right Thing..?)
 *
 * THREADS: MT-SAFE (assuming different as/args)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param B BAKA thread/global state.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static void
listen_activity(bk_s B, struct bk_run *run, int fd, u_int gottype, void *args, const struct timeval *startime)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct addrgroup_state *as = NULL;
  struct addrgroup_state *nas = NULL;
  struct sockaddr sa;
  socklen_t len = sizeof(sa);
  int newfd = -1;
  struct bk_addrgroup *bag = NULL;
  int socktype;
  int swapped = 0;

  if (BK_FLAG_ISSET(gottype, BK_RUN_CLOSE))
  {
    // We're looping around in our own callbacks (most likely)
    BK_VRETURN(B);
  }

  if (!run || !(as = args))
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
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

  if (!(bag = as->as_bag))
  {
    bk_error_printf(B, BK_ERR_ERR, "Address group missing from address group state\n");
    goto error;
  }

  if ((socktype = bag2socktype(B, bag, 0)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not determine scoket type of address group\n");
    goto error;
  }

  if (socktype == SOCK_STREAM)
  {
    /*
     * <WARNING>The linux man page says that accept():
     *
     *   creates a new connected socket with mostly the
     *   same properties as s
     *
     * Gee, thanks Linux for clearing that up.  The question is, what
     * properties are not copied?  Do we need to set keepalives et al?
     * The "official" Linux LKML word is:
     *
     *   if you want a well-written, portable program, you
     *   must set the file descriptor flags after an accept(2) call
     *
     * Note this says nothing about socket properties, though.  Should
     * we call the user back with a socket to allow him to set
     * properties again?  Or expect them to do it as part of connected?
     * </WARNING>
     */
    if ((newfd = accept(as->as_sock, &sa, &len)) < 0)
    {
      if (errno == EAGAIN)
      {
	bk_error_printf(B, BK_ERR_ERR, "accept got EAGAIN after presumably being told it was ready: %s\n", strerror(errno));
	BK_VRETURN(B);
      }
      bk_error_printf(B, BK_ERR_ERR, "accept failed: %s\n", strerror(errno));
      as->as_state = bk_net_init_sys_error(B, errno);
      goto error;
    }
  }
  else
  {
    bk_sockaddr_t bs;
    socklen_t bs_len;
    char buf[sizeof(DGRAM_PREAMBLE)];
    int one = 1;

    memset(&bs, 0, sizeof(bs));
    memset(buf, sizeof(buf), 0);
    bs_len = sizeof(bs);

    if (BK_FLAG_ISSET(as->as_user_flags, BK_NET_FLAG_BAKA_UDP))
    {
      /*
       * <WARNING>
       * THIS IS TOTALLY UNNECESSARY CODE WHICH IS MAITAINED ONLY FOR
       * BACKWARD COMPATIBILITY WITH THINGS COMPILED AGAINST AN OLDER
       * VERSION OF LIBBK. YOU DO NOT NEED TO READ ANY DATA IN ORDER TO
       * OBTAIN THE REMOTE ADDRESS AND WHY JTT DIDN'T REALIZE THIS IN 2001
       * PASSES ALL UNDERSTANDING.
       *
       * Anyway do not set BK_NET_FLAG_BAKA_UDP unless you absolutely have to.
       * </WARNING>
       */

      /*
       * Simulate accept(2) for DGRAM service. Obtain the preamble (which is
       * really mostly just a "dummy" client write so the server has something
       * to recvfrom(2) on before real data comes). Connect the server socket
       * to the client and the create a *new* server socket. It *must* be
       * done in this order or the second bind(2) will fail with
       * EADDRINUSE). Finally swap the descriptors so as->as_sock is the
       * server descriptor and newfd is the "accept(2)" descriptor, just as
       * would the case in the STREAM situation.
       */
      // Receive the preamble and verify it. Then connect to peer so read/write work
      if (recvfrom(as->as_sock, buf, sizeof(buf), 0, &(bs.bs_sa), &bs_len) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not receive dgram preamble: %s\n", strerror(errno));
	goto error;
      }

      if (!BK_STREQ(buf, DGRAM_PREAMBLE))
      {
	bk_error_printf(B, BK_ERR_ERR, "Did not receive proper dgram preamble\n");
	goto error;
      }
    }
    else
    {
      // Use a 0 length receive buffer to extract remote sockaddr without actually reading data.
      if (recvfrom(as->as_sock, NULL, 0, MSG_PEEK, &(bs.bs_sa), &bs_len) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not receive dgram preamble: %s\n", strerror(errno));
	goto error;
      }
    }

    // bk_run_close is a misleading name. We're just withdrawing the socket from bk_run.
    if (bk_run_close(B, run, as->as_sock, BK_RUN_CLOSE_FLAG_NO_HANDLER) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not withdraw newly connected DGRAM socket from serving\n");
      goto error;
    }

    // Connect with client.
    if (connect(as->as_sock, &(bs.bs_sa), bs_len) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create server dgram connection: %s\n", strerror(errno));
      goto error;
    }

    bs_len = sizeof(bs);
    if (getsockname(as->as_sock, &(bs.bs_sa), &bs_len))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not obtain local socket address: %s\n", strerror(errno));
      goto error;
    }

    // Create a new server
    if ((newfd = socket(bs.bs_sa.sa_family, SOCK_DGRAM, (bag->bag_proto == BK_GENERIC_DGRAM_PROTO)?0:bag->bag_proto)) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create socket: %s\n", strerror(errno));
      goto error;
    }

    if (setsockopt(newfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not set reuseaddr on socket: %s\n", strerror(errno));
      goto error;
    }

    BK_GET_SOCKADDR_LEN(B, &(bs.bs_sa), bs_len);

    // For AF_LOCAL we *must* remove the sync file before rebinding
    // <WARNNING> THIS CREATES A RACE CONDITION IF SOMEONE IS CONNECTING </WARNING?
    if (bs.bs_sa.sa_family == AF_LOCAL && (unlink(bs.bs_sun.sun_path) < 0) && (errno != ENOENT))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not unlink AFL_LOCAL file %s prior to rebinding: %s\n", bs.bs_sun.sun_path, strerror(errno));
      goto error;
    }

    // <WARNING> DO NOT INSERT CODE BETWEEN THE UNLINK(2) ABOVE AND THE BIND(2) BELOW </WARNING>

    if (bind(newfd, &(bs.bs_sa), bs_len) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not rebind local DGRAM server address: %s\n", strerror(errno));
      goto error;
    }

    if (bk_run_handle(B, run, newfd, listen_activity, as, BK_RUN_WANTREAD, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not configure socket's I/O handler\n");
      goto error;
    }

    BK_SWAP(as->as_sock, newfd);

    swapped = 1;
  }

  if (!(nas = as_server_copy(B, as, newfd)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Couldn't create new addrgroup_state\n");
    goto error;
  }

  newfd = -1;

  nas->as_state = BkAddrGroupStateConnected;

  stream_end(B, nas);
  BK_VRETURN(B);

 error:
  if (swapped)
    BK_SWAP(as->as_sock, newfd);

  if (newfd != -1)
    close(newfd);

  /*
   * <WARNING>We can probably abort since this should be running off the bk_run
   * loop</WARNING>
   */
  net_init_abort(B, as);

  if (nas)
    as_destroy(B, nas);

  BK_VRETURN(B);
}




/**
 * Close up tcp in the addrgroup way. Remove the socket from run and close
 * it up. Set the state to -1 so we wont try this again until we need to.
 *
 * THREADS: MT-SAFE (assuming different as)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param as @a addrgroup_state info.
 */
static void
net_close(bk_s B, struct addrgroup_state *as)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  bk_flags bk_run_close_flags = 0;

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

  /*
   *  If we're here because we're allready in the process of closing we
   *  don't need the handler notification.
   */
  if (as->as_state == BkAddrGroupStateClosing)
  {
    BK_FLAG_SET(bk_run_close_flags, BK_RUN_CLOSE_FLAG_NO_HANDLER);
  }

  if (bk_run_close(B, as->as_run, as->as_sock, bk_run_close_flags) < 0)
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
 *
 * THREADS: MT-SAFE (assuming different bag)
 * THREADS: REENTRANT (otherwise)
 *
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
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
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

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);

}



/**
 * Modify read desirability on a addressgroup_state
 *
 * THREADS: REENTRANT
 *
 *	@param B BAKA thread/global state.
 *	@param run bk_run structure
 *	@param server_handle handle provided by complete routine
 *	@param flags BK_ADDRESSGROUP_RESUME resume instead of suspend
 *	@return <i>-1</i> on failure<br>
 *	@return <i>0</i>on success
 */
int
bk_addressgroup_suspend(bk_s B, struct bk_run *run, void *server_handle, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct addrgroup_state *as = server_handle;

  if (bk_run_setpref(B, run, as->as_sock, BK_FLAG_ISSET(flags, BK_ADDRESSGROUP_RESUME)?BK_RUN_WANTREAD:0, BK_RUN_WANTREAD, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set file descriptor preference\n");
    BK_RETURN(B, -1);
  }

  BK_RETURN(B, 0);
}



/**
 * Take over a listening socket and handle its service. Despite it's name
 * this function must appear here so it can reference a static callback
 * routine. Oh well.
 *
 * THREADS: MT-SAFE (assuming s is not closed)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param run @a bk_run structure.
 *	@param s The socket to assume control over.
 *	@param sercurenets IP address filtering.
 *	@param callback Function to call back when there's a connection
 *	@param args User arguments to supply to above.
 *	@param key_path (file) path to private key file in PEM format
 *	@param cert_path (file) path to certificate file in PEM format
 *	@param dhparam_path (file) path to dh param file in PEM format
 *	@param ca_file file to dh param file in PEM format
 *	@param ctx_flags SSL context flags (see bk_ssl_create_context())
 *	@param flags User flags.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_netutils_commandeer_service(bk_s B, struct bk_run *run, int s, const char *securenets, bk_bag_callback_f callback, void *args, const char *key_path, const char *cert_path, const char *ca_file, const char *dhparam_path, bk_flags ctx_flags, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  if (BK_FLAG_ISSET(flags, BK_NET_FLAG_WANT_SSL))
  {
    if (!bk_ssl_supported(B))
    {
      bk_error_printf(B, BK_ERR_ERR, "SSL support is not available\n");
      BK_RETURN(B, -1);
    }

#ifndef NO_SSL
    BK_RETURN(B, bk_ssl_netutils_commandeer_service(B, run, s, securenets, callback, args, key_path, cert_path, ca_file, dhparam_path, ctx_flags, 0));
#endif /* NO_SSL */
  }
  BK_RETURN(B, bk_netutils_commandeer_service_std(B, run, s, securenets, callback, args, flags));
}



/**
 * Take over a listening socket and handle its service. Despite it's name
 * this function must appear here so it can reference a static callback
 * routine. Oh well.
 *
 * THREADS: MT-SAFE (assuming s is not closed)
 * THREADS: REENTRANT (otherwise)
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
bk_netutils_commandeer_service_std(bk_s B, struct bk_run *run, int s, const char *securenets, bk_bag_callback_f callback, void *args, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct addrgroup_state *as = NULL;

  if (!callback)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
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

  if (bk_run_handle(B, as->as_run, s, listen_activity, as, BK_RUN_WANTREAD, 0)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not configure socket's I/O handler\n");
    goto error;
  }

  BK_RETURN(B, 0);

 error:
  if (as) as_destroy(B, as);
  BK_RETURN(B, -1);

}



/**
 * Retrieve the socket from the server handle
 *
 * THREADS: MT-SAFE (assuming different as/server_handle)
 * THREADS: REENTRANT (otherwise)
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
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  BK_RETURN(B, as->as_sock);
}




/**
 * Shutdown a server referenced by the server handle.
 *
 * THREADS: MT-SAFE (assuming different as/server_handle)
 * THREADS: REENTRANT (otherwise)
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
  struct addrgroup_state *as = server_handle;

  if (!as)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  as_destroy(B, as);
  BK_RETURN(B, 0);
}




/**
 * "Clone" a new connected @a addrgroup_state from the server
 *
 * THREADS: MT-SAFE (assuming different aas/s)
 * THREADS: REENTRANT (otherwise)
 *
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
    BK_RETURN(B, NULL);
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

  BK_RETURN(B, as);

 error:
  if (as) as_destroy(B, as);
  BK_RETURN(B, NULL);
}



/**
 * Return the correct bk_addrgroup_state_e error type based on errno.
 *
 * THREADS: MT-SAFE
 *
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



/**
 * Obtain the address type of an addrgroup. All addrs are assumed to be
 * homogeneous so the type of one is the type of all.
 *
 *	@param B BAKA thread/global state.
 *	@param bag The address group to check
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return address <i>type</i> on success.
 */
int
bk_addrgroup_addr_type(bk_s B, struct bk_addrgroup *bag, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bag)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (bag->bag_local)
    BK_RETURN(B, bk_netinfo_addr_type(B, bag->bag_local, 0));

  if (bag->bag_remote)
    BK_RETURN(B, bk_netinfo_addr_type(B, bag->bag_remote, 0));

  BK_RETURN(B, BkNetinfoTypeUnknown);
}



/**
 * Check if the a particular address group refers to a network connection or some other type
 *
 *	@param B BAKA thread/global state.
 *	@param bag The address group to check
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> if not a network address group.
 *	@return <i>0</i> if a network address group.
 */
int
bk_addrgroup_network(bk_s B, struct bk_addrgroup *bag, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int type;
  int ret;

  if (!bag)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if ((type = bk_addrgroup_addr_type(B, bag, flags)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not obtain address group type\n");
    BK_RETURN(B, -1);
  }

  switch(type)
  {
  case BkNetinfoTypeUnknown:
    ret = 0;
    break;
  case BkNetinfoTypeInet:
    ret = 1;
    break;
  case BkNetinfoTypeInet6:
    ret = 1;
    break;
  case BkNetinfoTypeLocal:
    ret = 0;
    break;
  case BkNetinfoTypeEther:
    ret = 0;
    break;
  default:
    bk_error_printf(B, BK_ERR_ERR,"Unknown type: %d\n", type);
    goto error;
    break;
  }

  BK_RETURN(B, ret);

 error:
  BK_RETURN(B, -1);
}



/**
 * Obtain the socket type of a bag based on the bag's proto
 *
 *	@param B BAKA thread/global state.
 *	@param bag The @a bk_addrgroup to check
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>>0</i> on success.
 */
static int
bag2socktype(bk_s B, struct bk_addrgroup *bag, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret;

  if (!bag)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  switch (bag->bag_proto)
  {
  case IPPROTO_UDP:
  case BK_GENERIC_DGRAM_PROTO:
    ret = SOCK_DGRAM;
    break;

  case IPPROTO_TCP:
  case BK_GENERIC_STREAM_PROTO:
    ret = SOCK_STREAM;
    break;

  default:
    bk_error_printf(B, BK_ERR_ERR, "Unknown addrgroup proto: %d\n", bag->bag_proto);
    goto error;
    break;
  }

  BK_RETURN(B, ret);

 error:
  BK_RETURN(B, -1);
}
