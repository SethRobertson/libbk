#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_ssl.c,v 1.8 2003/08/28 20:07:42 lindauer Exp $";
static const char libbk__copyright[] = "Copyright (c) 2003";
static const char libbk__contact[] = "<projectbaka@baka.org>";
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
 * OpenSSL wrapper functions to present a libbk-friendly interface.
 */

#include <libbk.h>
#include <libbkssl.h>
#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>



/**
 * SSL tasks
 */
typedef enum
{
  SslTaskAccept,				///< Accept connection
  SslTaskConnect,				///< Make connection
} ssl_task_e;



/**
 * Opaque args for start_service wrapper.
 */
struct start_service_args
{
  struct bk_run	       *ssa_run;		///< Run environment
  bk_bag_callback_f	ssa_user_callback;	///< Caller's callback.
  void		       *ssa_user_args;		///< Caller's opaque args.
  SSL_CTX	       *ssa_ssl_ctx;		///< Template for new SSL connections.
  ssl_task_e		ssa_task;		///< Connect or accept
  bk_flags		ssa_flags;		///< Reserved.
};



/**
 * Args for ssl session negotiation handler
 */
struct accept_handler_args
{
  bk_bag_callback_f	aha_user_callback;	///< Caller's callback.
  void		       *aha_user_args;		///< Caller's opaque args.
  struct bk_addrgroup  *aha_bag;		///< For callback
  void		       *aha_server_handle;	///< For callback
  SSL		       *aha_ssl;		///< SSL session state
  ssl_task_e		aha_task;		///< Connect or accept
  bk_flags		aha_flags;		///< Reserved.
};



/**
 * SSL session template.
 */
struct bk_ssl_ctx
{
  SSL_CTX      *bsc_ssl_ctx;			///< SSL context (session template)
  bk_flags	bsc_flags;			///< Reserved.
};



/**
 * A particular ssl session.
 */
struct bk_ssl
{
  SSL	       *bs_ssl;				///< SSL connection state
  struct bk_run *bs_run;			///< Baka run state
  bk_flags	bs_flags;			///< Reserved
};



static int ssl_newsock(bk_s B, void *opaque, int newsock, struct bk_addrgroup *bag, void *server_handle, bk_addrgroup_state_e state);
static void ssl_connect_accept_handler(bk_s B, struct bk_run *run, int fd, u_int gottype, void *args, const struct timeval *startime);
static int ssl_readfun(bk_s B, struct bk_ioh *ioh, void *opaque, int fd, caddr_t buf, __SIZE_TYPE__ size, bk_flags flags);
static int ssl_writefun(bk_s B, struct bk_ioh *ioh, void *opaque, int fd, struct iovec *buf, __SIZE_TYPE__ size, bk_flags flags);
static void ssl_closefun(bk_s B, struct bk_ioh *ioh, void *opaque, int fdin, int fdout, bk_flags flags);
static void ssl_shutdown_handler(bk_s B, struct bk_run *run, int fd, u_int gottype, void *opaque, const struct timeval *startime);
#ifdef BK_USING_PTHREADS
static int ssl_threads_init(bk_s B);
static void ssl_threads_destroy(bk_s B);
static unsigned long pthreads_thread_id(void);
static void pthreads_locking_callback(int mode, int type, const char *file, int line);
#endif // BK_USING_PTHREADS
static int verify_callback(int preverify_ok, X509_STORE_CTX *ctx);



#ifdef BK_USING_PTHREADS
static pthread_mutex_t *lock_cs = NULL;
static long *lock_count = NULL;
#endif // BK_USING_PTHREADS



/**
 * Initialize global OpenSSL state.  You *must* call this function before
 * any other ssl functions.
 *
 * @param B BAKA Thread/global state.
 * @return <i>0</i> on success
 * @return <i>-1</i> on failure
 */
int
bk_ssl_env_init(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkssl");
  struct bk_truerandinfo *randinfo = NULL;
  const int bufsize = 1024;
  char buf[bufsize];

  SSL_library_init();
  SSL_load_error_strings();

  if (!(randinfo = bk_truerand_init(B, 0, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create entropy gatherer.\n");
    goto error;
  }

  while (RAND_status() != 1)
  {
    if (bk_truerand_getbuf(B, randinfo, buf, bufsize, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not gather entropy.\n");
      goto error;
    }

    RAND_seed(buf, bufsize);
  }

  bk_truerand_destroy(B, randinfo);
  randinfo = NULL;

#ifdef BK_USING_PTHREADS
  if (ssl_threads_init(B) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Failed to initialize SSL threads.\n");
    goto error;
  }
#endif // BK_USING_PTHREADS

  BK_RETURN(B, 0);

 error:
  if (randinfo)
  {
    bk_truerand_destroy(B, randinfo);
    randinfo = NULL;
  }

#ifdef BK_USING_PTHREADS
  ssl_threads_destroy(B);
#endif // BK_USING_PTHREADS

  BK_RETURN(B, -1);
}



/**
 * Free global OpenSSL state.
 *
 * @param B BAKA Thread/global state.
 * @return <i>0</i> on success
 * @return <i>-1</i> on failure
 */
int
bk_ssl_env_destroy(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkssl");

  EVP_cleanup();

  BK_RETURN(B, 0);
}


/**
 * Create context (template) for a group of sessions that share the same configuration.
 *
 * For a server, the caller must specify either (cert_path && key_path) or dhparam_path.
 *
 * Flags:
 *   BK_SSL_REJECT_V2 Restrict access to ssl v3 clients.
 *
 * @param B BAKA Thread/global state
 * @param cert_path (file) path to certificate file in PEM format
 * @param key_path (file) path to private key file in PEM format
 * @param dhparam_path (file) path to dh param file in PEM format
 * @param flags See above.
 * @return <i>pointer to new bk_ssl_ctx</i> on success
 * @return <i>NULL</i> on error
 */
struct bk_ssl_ctx*
bk_ssl_create_context(bk_s B, const char *cert_path, const char *key_path, const char *dhparam_path, const char *cafile, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkssl");
  struct bk_ssl_ctx *ssl_ctx = NULL;
  DH *dh = NULL;
  BIO *dhparam_in = NULL;
  X509_STORE *store = NULL;
  long ssl_options = 0;
  int err;

  if (!BK_CALLOC(ssl_ctx))
  {
    bk_error_printf(B, BK_ERR_ERR, "Calloc failed: %s.\n", strerror(errno));
    BK_RETURN(B, NULL);
  }

  if (!(ssl_ctx->bsc_ssl_ctx = SSL_CTX_new(SSLv23_method())))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create new ssl session template: %s.\n", ERR_error_string(ERR_get_error(), NULL));
    goto error;
  }

  if (BK_FLAG_ISSET(flags, BK_SSL_REJECT_V2))
    BK_FLAG_SET(ssl_options, SSL_OP_NO_SSLv2);

  if (BK_FLAG_ISSET(flags, BK_SSL_NOCERT))
  {
    cert_path = NULL;
    key_path = NULL;
    cafile = NULL;
   
    // Read DH params
    if (!(dhparam_in = BIO_new_file(dhparam_path, "r")))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not initialize BIO: %s.\n", ERR_error_string(ERR_get_error(), NULL));
      goto error;
    }
   
    if (!(dh = PEM_read_bio_DHparams(dhparam_in, NULL, NULL, NULL)))
    {
      err = ERR_get_error();

      if (err == SSL_ERROR_SYSCALL)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not read DH params: %s.\n", strerror(errno));
      }
      else
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not read DH params: %s.\n", ERR_error_string(ERR_get_error(), NULL));
      }
      goto error;
    }

    // Free BIO (and close underlying descriptor)
    if (!BIO_free(dhparam_in))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not close BIO: %s.\n", ERR_error_string(ERR_get_error(), NULL));
      goto error;
    }
    dhparam_in = NULL;
 
    if (!SSL_CTX_set_tmp_dh(ssl_ctx->bsc_ssl_ctx, dh))
    {
      bk_error_printf(B, BK_ERR_ERR, "Failed to set temporary DH key: %s.\n", ERR_error_string(ERR_get_error(), NULL));
      goto error;
    }

    DH_free(dh);
    dh = NULL;
  }

  if (cert_path)
  {
    // <TODO>Use SSL_CTX_use_certificate_chain_file()?</TODO>
    if (SSL_CTX_use_certificate_file(ssl_ctx->bsc_ssl_ctx, cert_path, SSL_FILETYPE_PEM) < 1)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not set SSL certificate: %s.\n", ERR_error_string(ERR_get_error(), NULL));
      goto error;
    }
  }

  if (key_path)
  {
    if (SSL_CTX_use_PrivateKey_file(ssl_ctx->bsc_ssl_ctx, key_path, SSL_FILETYPE_PEM) < 1)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not set SSL private key: %s.\n", ERR_error_string(ERR_get_error(), NULL));
      goto error;
    }
  }

  if (cafile)
  {
    if (!SSL_CTX_load_verify_locations(ssl_ctx->bsc_ssl_ctx, cafile, NULL))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not load SSL CA file: %s.\n", ERR_error_string(ERR_get_error(), NULL));
      goto error;
    }
   
    if (!(store = SSL_CTX_get_cert_store(ssl_ctx->bsc_ssl_ctx)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not get SSL certificate store: %s.\n", ERR_error_string(ERR_get_error(), NULL));
      goto error;
    }

    X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK);

    SSL_CTX_set_verify(ssl_ctx->bsc_ssl_ctx,
		       SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT | SSL_VERIFY_CLIENT_ONCE, verify_callback);
  }

  SSL_CTX_set_options(ssl_ctx->bsc_ssl_ctx, ssl_options);

  BK_RETURN(B, ssl_ctx);

 error:
  if (dh)
    DH_free(dh);

  if (dhparam_in && !BIO_free(dhparam_in))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not close BIO: %s.\n", ERR_error_string(ERR_get_error(), NULL));
    goto error;
  }

  if (ssl_ctx)
    bk_ssl_destroy_context(B, ssl_ctx);

  BK_RETURN(B, NULL);
}



/**
 * Destroy an ssl context.
 *
 * @param B BAKA Thread/global state
 * @param ssl_ctx context to destroy
 */
void
bk_ssl_destroy_context(bk_s B, struct bk_ssl_ctx *ssl_ctx)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkssl");

  if (!ssl_ctx)
  {
    bk_error_printf(B, BK_ERR_ERR, "Internal error: invalid arguments.\n");
    BK_VRETURN(B);
  }

  if (ssl_ctx->bsc_ssl_ctx)
  {
    SSL_CTX_free(ssl_ctx->bsc_ssl_ctx);
  }

  free(ssl_ctx);

  BK_VRETURN(B);
}



/**
 * Destroy bk_ssl and close underlying fds.
 *
 * @param B BAKA Thread/global state
 * @param ssl to destroy
 */
void
bk_ssl_destroy(bk_s B, struct bk_ssl *ssl, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkssl");
  int fdin, fdout;

  if (!ssl)
  {
    bk_error_printf(B, BK_ERR_ERR, "Internal error: invalid arguments.\n");
    BK_VRETURN(B);
  }

  if (ssl->bs_ssl)
  {
    fdin = SSL_get_rfd(ssl->bs_ssl);
    fdout = SSL_get_wfd(ssl->bs_ssl);

    if (fdin >= 0)
    {
      bk_run_close(B, ssl->bs_run, fdin, 0);
      if (BK_FLAG_ISCLEAR(flags, BK_SSL_DESTROY_DONTCLOSEFDS))
	close(fdin);
    }

    if ((fdout >= 0) && (fdin != fdout))
    {
      bk_run_close(B, ssl->bs_run, fdout, 0);
      if (BK_FLAG_ISCLEAR(flags, BK_SSL_DESTROY_DONTCLOSEFDS))
	close(fdout);
    }

    SSL_free(ssl->bs_ssl);
  }

  free(ssl);

  BK_VRETURN(B);
}



/**
 * Make a service with a user friendly string based interface (verbose
 * format). The def* arguments will replace unfound elements of the url and
 * so should be defined when possible or you risk having an incompletely
 * specified local side.
 *
 *
 *	@param B BAKA thread/global state.
 *	@param run The @a bk_run structure.
 *	@param url The local endpoint specification (may be NULL).
 *	@param defhostsstr Host string to use if host part of url is not found. (may be NULL).
 *	@param defservstr Service string to use if service part of url is not found. (may be NULL).
 *	@param protostr Protocol string to use if protocol part of url is not found. (may be NULL).
 *	@param sercurenets Address based security specification.
 *	@param callback Function to call when start is complete.
 *	@param args User args for @a callback.
 *	@param backlog Server @a listen(2) backlog
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_ssl_start_service_verbose(bk_s B, struct bk_run *run, struct bk_ssl_ctx *ssl_ctx, char *url, char *defhoststr, char *defservstr, char *defprotostr, char *securenets, bk_bag_callback_f callback, void *args, int backlog, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkssl");
  struct start_service_args *ssa = NULL;

  if (!ssl_ctx || !ssl_ctx->bsc_ssl_ctx)
  {
    bk_error_printf(B, BK_ERR_ERR, "Internal error: invalid arguments\n");
    BK_RETURN(B, -1);
  }

  if (!BK_CALLOC(ssa))
  {
    bk_error_printf(B, BK_ERR_ERR, "Calloc failed: %s.\n", strerror(errno));
    goto error;
  }

  ssa->ssa_user_callback = callback;
  ssa->ssa_user_args = args;
  ssa->ssa_run = run;
  ssa->ssa_ssl_ctx = ssl_ctx->bsc_ssl_ctx;
  ssa->ssa_task = SslTaskAccept;

  if (bk_netutils_start_service_verbose(B, run, url, defhoststr, defservstr, defprotostr, securenets,
					ssl_newsock, ssa, backlog, flags) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Failed to start service.\n");
    goto error;
  }
 
  BK_RETURN(B, 0);

 error:
  if (ssa)
    free(ssa);

  BK_RETURN(B, -1);
}



/**
 * Start up a connection with a user friendly interface.
 *
 *
 *	@param B BAKA thread/global state.
 *	@param run The @a bk_run structure.
 *	@param rurl The remoe endpoint specification (may be NULL).
 *	@param rdefhostsstr Remote host string to use if host part of url is not found. (may be NULL).
 *	@param rdefservstr Remote service string to use if service part of url is not found. (may be NULL).
 *	@param lurl The local endpoint specification (may be NULL).
 *	@param ldefhostsstr Local host string to use if host part of url is not found. (may be NULL).
 *	@param ldefservstr Local service string to use if service part of url is not found. (may be NULL).
 *	@param protostr Protocol string to use if protocol part of url is not found. (may be NULL).
 *	@param timeout Abort connection after @a timeout seconds.
 *	@param callback Function to call when start is complete.
 *	@param args User args for @a callback.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_ssl_make_conn_verbose(bk_s B, struct bk_run *run, struct bk_ssl_ctx *ssl_ctx,
			 char *rurl, char *defrhost, char *defrserv,
			 char *lurl, char *deflhost, char *deflserv,
			 char *defproto, u_long timeout, bk_bag_callback_f callback, void *args, bk_flags flags )
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkssl");
  struct start_service_args *ssa = NULL;

  if (!ssl_ctx || !ssl_ctx->bsc_ssl_ctx)
  {
    bk_error_printf(B, BK_ERR_ERR, "Internal error: invalid arguments\n");
    BK_RETURN(B, -1);
  }

  if (!BK_CALLOC(ssa))
  {
    bk_error_printf(B, BK_ERR_ERR, "Calloc failed: %s.\n", strerror(errno));
    goto error;
  }

  ssa->ssa_user_callback = callback;
  ssa->ssa_user_args = args;
  ssa->ssa_run = run;
  ssa->ssa_ssl_ctx = ssl_ctx->bsc_ssl_ctx;
  ssa->ssa_task = SslTaskConnect;

  if (bk_netutils_make_conn_verbose(B, run, rurl, defrhost, defrserv, lurl, deflhost, deflserv,
				    defproto, timeout, ssl_newsock, ssa, flags) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Failed to make connection.\n");
    goto error;
  }

  BK_RETURN(B, 0);

 error:
  if (ssa)
    free(ssa);

  BK_RETURN(B, -1);
}



/**
 * Create IOH for an SSL connection.
 *
 * Although there's currently not a way to start an SSL session with fdin != fdout, the ssl_ioh code
 * is written with that possiblity in mind.
 *
 *	@param B BAKA thread/global state
 *	@param ssl SSL session state
 *	@param fdin The file descriptor to read from.  -1 if no input is desired.
 *	@param fdout The file descriptor to write to.  -1 if no output is desired.  (This will be different from fdin only for pipe(2) style fds where descriptors are only useful in one direction and occur in pairs.)
 *	@param handler The user callback to notify on complete I/O or other events
 *	@param opaque The opaque data for the user callback.
 *	@param inbufhint A hint for the input routines (0 for 128 bytes)
 *	@param inbufmax The maximum buffer size of incomplete data (0 for unlimited) -- note this is a hint not an absolute limit
 *	@param outbufmax The maximum amount of data queued for transmission (0 for unlimited) -- note this is a hint not an absolute limit
 *	@param run The bk run environment to use with the fd.
 *	@param flags The type of data on the file descriptors.
 *	@return <i>NULL</i> on call failure, allocation failure, or other fatal error.
 *	@return <br><i>ioh structure</i> if successful.
 */
struct bk_ioh *
bk_ssl_ioh_init(bk_s B, struct bk_ssl *ssl, int fdin, int fdout, bk_iohhandler_f handler, void *opaque,
		u_int32_t inbufhint, u_int32_t inbufmax, u_int32_t outbufmax, struct bk_run *run, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkssl");
  struct bk_ioh *ioh = NULL;
  int ret;

  if (!run || !ssl)
  {
    bk_error_printf(B, BK_ERR_ERR, "Internal error: illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  // Create ioh
  if (!(ioh = bk_ioh_init(B, fdin, fdout, handler, opaque, inbufhint, inbufmax, outbufmax, run, flags | BK_IOH_DONT_ACTIVATE)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Failed to create ioh.\n");
    goto error;
  }

  // Set read & write functions and activate
  ret = bk_ioh_update(B, ioh, ssl_readfun, ssl_writefun, ssl_closefun, ssl, 0,
		      0, 0, 0, 0, flags, BK_IOH_UPDATE_READFUN
		      | BK_IOH_UPDATE_WRITEFUN | BK_IOH_UPDATE_CLOSEFUN
		      | BK_IOH_UPDATE_IOFUNOPAQUE | BK_IOH_UPDATE_FLAGS);
  if (ret < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "IOH update failed.\n");
    goto error;
  }
  else if (ret == 2)				// ioh has been nuked
    ioh = NULL;

  BK_RETURN(B, ioh);

 error:
  if (ioh)
  {
    bk_ioh_close(B, ioh, 0);
  }
 
  BK_RETURN(B, NULL);
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
bk_ssl_netutils_commandeer_service(bk_s B, struct bk_run *run, struct bk_ssl_ctx *ssl_ctx, int s, char *securenets, 
				   bk_bag_callback_f callback, void *args, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct start_service_args *ssa = NULL;

  if (!ssl_ctx || !ssl_ctx->bsc_ssl_ctx)
  {
    bk_error_printf(B, BK_ERR_ERR, "Internal error: invalid arguments\n");
    BK_RETURN(B, -1);
  }

  if (!BK_CALLOC(ssa))
  {
    bk_error_printf(B, BK_ERR_ERR, "Calloc failed: %s.\n", strerror(errno));
    goto error;
  }

  ssa->ssa_user_callback = callback;
  ssa->ssa_user_args = args;
  ssa->ssa_run = run;
  ssa->ssa_ssl_ctx = ssl_ctx->bsc_ssl_ctx;
  ssa->ssa_task = SslTaskAccept;

  if (bk_netutils_commandeer_service(B, run, s, securenets, ssl_newsock, ssa, flags) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Failed to commandeer service for SSL.\n");
    goto error;
  }

  BK_RETURN(B, 0);

 error:
  if (ssa) 
    free(ssa);

  BK_RETURN(B, -1);

}



/**
 * Handle a new SSL socket connection
 *
 *	@param B BAKA Thread/Global state
 *	@param newsock Newly accepted socket
 *	@param opaque start service args
 *	@param state Why we are being called
 *	@param server Accepting server state
 */
int
ssl_newsock(bk_s B, void *opaque, int newsock, struct bk_addrgroup *bag, void *server_handle, bk_addrgroup_state_e state)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkssl");
  struct start_service_args *ssa = opaque;
  bk_bag_callback_f user_callback = NULL;
  void *user_args = NULL;
  struct accept_handler_args *aha = NULL;
  int err_ret = 0;				// value to return from this function on error
 
  if (!ssa || !server_handle)
  {
    bk_error_printf(B, BK_ERR_ERR, "Internal error: invalid arguments.\n");
    BK_RETURN(B, -1);
  }

  // Save these for the case where we destroy the ssa
  user_args = ssa->ssa_user_args;
  user_callback = ssa->ssa_user_callback;

  switch (state)
  {
  case BkAddrGroupStateSysError:
  case BkAddrGroupStateRemoteError:
  case BkAddrGroupStateLocalError:
  case BkAddrGroupStateTimeout:
    // Nothing SSL-specific to do.  Just pass to user callback.
    break;
  case BkAddrGroupStateConnected:
    // Negotiate an SSL session with the new client

    // set new socket to non-blcoking
    if (bk_fileutils_modify_fd_flags(B, newsock, O_NONBLOCK, BkFileutilsModifyFdFlagsActionSet) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not set socket to non-blocking mode.\n");
      goto error;
    }

    if (!BK_CALLOC(aha))
    {
      bk_error_printf(B, BK_ERR_ERR, "Calloc failed\n");
      goto fatal_error;
    }

    aha->aha_user_callback = ssa->ssa_user_callback;
    aha->aha_user_args = ssa->ssa_user_args;
    aha->aha_bag = bag;
    bk_addrgroup_ref(B, bag);
    aha->aha_server_handle = server_handle;
    aha->aha_task = ssa->ssa_task;

    if (!(aha->aha_ssl = SSL_new(ssa->ssa_ssl_ctx)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create SSL state for new connection: %s.\n", ERR_error_string(ERR_get_error(), NULL));
      goto error;
    }

    // Load anonymous DH ciphers
    if (!SSL_set_cipher_list(aha->aha_ssl, "ALL:+RC4:@STRENGTH"))
    {
      bk_error_printf(B, BK_ERR_ERR, "Failed to load anonymous ciphers for NOCERT option: %s.\n",
	      ERR_error_string(ERR_get_error(), NULL));
      goto error;
    }

    if (!SSL_set_fd(aha->aha_ssl, newsock))
    {
      bk_error_printf(B, BK_ERR_ERR, "Failed to intialize SSL for new connection: %s\n", ERR_error_string(ERR_get_error(), NULL));
      goto error;
    }

    if (bk_run_handle(B, ssa->ssa_run, newsock, ssl_connect_accept_handler, aha, BK_RUN_WANTREAD | BK_RUN_WANTWRITE, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not add run handler for new socket.\n");
      goto error;
    }

    // Don't call user callback.  Accept handler will do that after session negotiation.
    BK_RETURN(B, 0);
    break;

  case BkAddrGroupStateSocket:
    break;
  case BkAddrGroupStateReady:
    // Server listening for connections.
    break;

  case BkAddrGroupStateClosing:
    free(ssa);
    break;
  }

  BK_RETURN(B, (*user_callback)(B, user_args, newsock, bag, server_handle, state));

 fatal_error:
  err_ret = -1;

 error:
  if (aha)
  {
    if (aha->aha_ssl)
    {
      SSL_free(aha->aha_ssl);
    }

    free(aha);
  }

  close(newsock);

  BK_RETURN(B, err_ret);
}



/**
 * Ready to keep working on SSL handshake.
 *
 *	@param B BAKA thread/global state.
 *	@param run Baka run environment
 *	@param fd File descriptor of activity
 *	@param gottype Type of activity
 *	@param args Private data
 *	@param starttide Official Time of activity
 */
void
ssl_connect_accept_handler(bk_s B, struct bk_run *run, int fd, u_int gottype, void *args, const struct timeval *startime)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkssl");
  struct accept_handler_args *aha = NULL;
  bk_bag_callback_f user_callback;
  struct bk_addrgroup *bag = NULL;
  void *server_handle = NULL;
  void *user_args = NULL;
  struct bk_ssl *bs = NULL;
  int ret;
  int err;
  int err_level;
 
  if (!run || !(aha = args))
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
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

  if (BK_FLAG_ISSET(gottype, BK_RUN_READREADY) || BK_FLAG_ISSET(gottype, BK_RUN_WRITEREADY))
  {
    if (aha->aha_task == SslTaskAccept)
    {
      ret = SSL_accept(aha->aha_ssl);
    }
    else if (aha->aha_task == SslTaskConnect)
    {
      ret = SSL_connect(aha->aha_ssl);
    }
    else
    {
      bk_error_printf(B, BK_ERR_ERR, "Internal error: invalid SSL task.\n");
      goto error;
    }

    if (ret < 1)
    {
      err = SSL_get_error(aha->aha_ssl, ret);

      // We know fdin == fdout == fd since we can only get here from ssl_newsock().

      // Ratchet down most errors if we're the server.
      if (aha->aha_task == SslTaskConnect)
	err_level = BK_ERR_ERR;
      else
	err_level = BK_ERR_NOTICE;

      switch(err)
      {
      case SSL_ERROR_WANT_READ:
	if (bk_run_setpref(B, run, fd, BK_RUN_WANTREAD, BK_RUN_WANTREAD | BK_RUN_WANTWRITE, 0) < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not request read.\n");
	  goto error;
	}
	BK_VRETURN(B);
	break;

      case SSL_ERROR_WANT_WRITE:
	// Always want read in case of eof.
	if (bk_run_setpref(B, run, fd, BK_RUN_WANTREAD | BK_RUN_WANTWRITE, BK_RUN_WANTREAD | BK_RUN_WANTWRITE, 0) < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not request write.\n");
	  goto error;
	}
	BK_VRETURN(B);
	break;

      case SSL_ERROR_ZERO_RETURN:
	bk_error_printf(B, err_level, "Peer denied connection: %s.\n", ERR_error_string(err, NULL));
	goto error;
	break;

      case SSL_ERROR_WANT_X509_LOOKUP:
	bk_error_printf(B, BK_ERR_ERR, "SSL wants unavailable X509 lookup: %s.\n", ERR_error_string(err, NULL));
	goto error;
	break;

      case SSL_ERROR_SSL:
	bk_error_printf(B, err_level, "SSL protocol error: %s.\n", ERR_error_string(err, NULL));
	goto error;
	break;

      case SSL_ERROR_SYSCALL:
	if (errno)
	{
	  // If errno==0, the other end just dropped the connection.
	  bk_error_printf(B, err_level, "SSL session negotiation failed: (syserror) %s.\n", strerror(errno));
	}
	goto error;
	break;

      default:
	bk_error_printf(B, BK_ERR_ERR, "SSL session negotiation failed: %s.\n", ERR_error_string(err, NULL));
	goto error;
      };
    }

    // Session negotiated!

    // remove fd from select set
    if (bk_run_close(B, run, fd, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not remove run handler for new socket after failed SSL connection.\n");
      // non-fatal?
    }

    user_callback = aha->aha_user_callback;
    user_args = aha->aha_user_args;
    server_handle = aha->aha_server_handle;

    bag = aha->aha_bag;

    if (!BK_CALLOC(bs))
    {
      bk_error_printf(B, BK_ERR_ERR, "Calloc failed: %s.\n", strerror(errno));
      goto error;
    }
   
    bs->bs_ssl = aha->aha_ssl;
    bs->bs_run = run;
    /*
     * <TODO>There needs to be a better place b/c the bag can leak the ssl state &
     * we don't b_addrgroup to call back into b_ssl.</TODO>
     */
    bag->bag_ssl = bs;

    free(aha);
    aha = NULL;

    // call user callback
    (*user_callback)(B, user_args, fd, bag, server_handle, BkAddrGroupStateConnected);

    bk_addrgroup_destroy(B, bag);
  }

  BK_VRETURN(B);

 error:
  // remove fd from select set
  if (bk_run_close(B, run, fd, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not remove run handler for new socket after failed SSL connection.\n");
  }

  close(fd);
 
  if (aha)
  {
    if (aha->aha_ssl)
    {
      SSL_free(aha->aha_ssl);
    }
    free(aha);
  }

  // Don't call bk_ssl_destroy because some members are shared with aha.
  if (bs)
    free(bs);

  BK_VRETURN(B);
}



/**
 * SSL version of read() for IOH API.
 *
 *	@param B BAKA Thread/global stateid
 *	@param ioh The ioh to use (may be NULL for those not including libbk_internal.h)
 *	@param opaque Common opaque data for read and write funs
 *	@param fd File descriptor
 *	@param buf Data to read
 *	@param size Amount of data to read
 *	@param flags Fun for the future
 *	@return Standard @a read() return codes
 */
int ssl_readfun(bk_s B, struct bk_ioh *ioh, void *opaque, int fd, caddr_t buf, __SIZE_TYPE__ size, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkssl");
  int ret, erno;
  int ssl_err;
  struct bk_ssl *bs = (struct bk_ssl*) opaque;
  SSL *ssl = NULL;

  if (!bs || !bs->bs_ssl)
  {
    bk_error_printf(B, BK_ERR_ERR, "Internal error: invalid arguments.\n");
    BK_RETURN(B, -1);
  }

  ssl = bs->bs_ssl;

  ssl_err = 0;
  errno = 0;
  ret = SSL_read(ssl, buf, size);

  if (ret < 1)
  {
    ssl_err = SSL_get_error(ssl, ret);
    switch(ssl_err)
    {
    case SSL_ERROR_WANT_READ:
      errno = EAGAIN;
      break;
    case SSL_ERROR_WANT_WRITE:
      /*
       * This should never happen unless there's an error in OpenSSL
       * (read source for SSL_get_error() for details).
       */
      errno = EAGAIN;
      break;
    case SSL_ERROR_ZERO_RETURN:
      // Protocol EOF (socket may still be open, though)
      ret = 0;
      break;
    case SSL_ERROR_SYSCALL:
      // ret == 0: Rude EOF in violation of protocol
      // ret == -1: I/O Error (errno set)
      break;
    case SSL_ERROR_SSL:
      // SSL library failure.  Fall through to default.
    default:
      bk_error_printf(B, BK_ERR_ERR, "SSL read error: %s.", ERR_error_string(ssl_err, NULL));
      ret = -1;
    };
  }

  erno = errno;

  bk_debug_printf_and(B, 1, "SSL read returned %d with error %s\n", ret, ERR_error_string(ssl_err, NULL));

  if (bk_debug_and(B, 0x20) && ret > 0)
  {
    bk_vptr dbuf;

    dbuf.ptr = buf;
    dbuf.len = MIN(ret, 32);

    bk_debug_printbuf_and(B, 0x20, "Buffer just read in:", "\t", &dbuf);
  }

  errno = erno;
  BK_RETURN(B, ret);
}



/**
 * SSL version of write() for IOH API.
 *
 *	@param B BAKA Thread/global state
 *	@param ioh The ioh to use (may be NULL for those not including libbk_internal.h)
 *	@param opaque Common opaque data for read and write funs
 *	@param fd File descriptor
 *	@param iovec Data to write
 *	@param size Number of iovec buffers
 *	@param flags Fun for the future
 *	@return Standard @a writev() return codes
 */
int ssl_writefun(bk_s B, struct bk_ioh *ioh, void *opaque, int fd, struct iovec *buf, __SIZE_TYPE__ size, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkssl");
  size_t i;
  int cnt;
  struct iovec iov;
  int ret, erno;
  int ssl_err;
  struct bk_ssl *bs = (struct bk_ssl*) opaque;
  SSL *ssl = NULL;

  if (!bs || !bs->bs_ssl)
  {
    bk_error_printf(B, BK_ERR_ERR, "Internal error: invalid arguments.\n");
    BK_RETURN(B, -1);
  }

  ssl = bs->bs_ssl;

  ssl_err = 0;
  errno = 0;
  cnt = 0;
  ret = 0;
  for (i=0; i<size; i++)
  {
    iov = buf[i];

    if ((ret = SSL_write(ssl, iov.iov_base, iov.iov_len)) > 0)
    {
      cnt += ret;
    }
    else
    {
      ssl_err = SSL_get_error(ssl, ret);
      if ((ssl_err == SSL_ERROR_WANT_READ) || (ssl_err == SSL_ERROR_WANT_WRITE))
      {
	/*
	 * WANT_READ should never happen unless there's an error in OpenSSL
	 * (read source for SSL_get_error() for details).
	 */
	errno = EAGAIN;
      }
      else if (ssl_err == SSL_ERROR_SYSCALL)
      {
	// Errno set.  Let caller print error.
	cnt = ret;
      }
      else if ((ret < 0) || (ssl_err != SSL_ERROR_NONE))
      {
	bk_error_printf(B, BK_ERR_ERR, "SSL write error: %s.", ERR_error_string(ssl_err, NULL));
	cnt = ret;
      }
    }
  }

  erno = errno;

  bk_debug_printf_and(B, 1, "SSL writev returned %d with error %s\n", ret, ERR_error_string(ssl_err, NULL));

  if (bk_debug_and(B, 0x20) && ret > 0)
  {
    bk_vptr dbuf;

    dbuf.ptr = buf[0].iov_base;
    dbuf.len = MIN(MIN((u_int)buf[0].iov_len, 32U), (u_int)ret);

    bk_debug_printbuf_and(B, 0x20, "Buffer just wrote:", "\t", &dbuf);
  }

  errno = erno;
  BK_RETURN(B, cnt);
}



/**
 * Standard close() functionality in IOH API
 *
 *	@param B BAKA Thread/global state
 *	@param ioh The ioh to use (may be NULL for those not including libbk_internal.h)
 *	@param opaque Common opaque data for read and write funs
 *	@param fdin File descriptor
 *	@param fdout File descriptor
 *	@param flags Fun for the future
 *	@return Standard @a writev() return codes
 */
void ssl_closefun(bk_s B, struct bk_ioh *ioh, void *opaque, int fdin, int fdout, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkssl");
  // WRONG TYPE!!!
  struct bk_ssl *bs = NULL;

  if (!ioh || !opaque)
  {
    bk_error_printf(B, BK_ERR_ERR, "Internal error: invalid arguments.\n");
    BK_VRETURN(B);
  }

  bs = (struct bk_ssl *) opaque;

  if (fdout >= 0)
  {
    if (bk_run_handle(B, bs->bs_run, fdout, ssl_shutdown_handler, bs->bs_ssl, 0, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not add run handler for shutting down SSL.\n");
      goto error;
    }
  }
     
  if ((fdin >=0) && (fdin != fdout))
  {
    if (bk_run_handle(B, bs->bs_run, fdin, ssl_shutdown_handler, bs->bs_ssl, 0, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not add run handler for shutting down SSL.\n");
      goto error;
    }
  }

  ssl_shutdown_handler(B, bs->bs_run, fdout, BK_RUN_READREADY | BK_RUN_WRITEREADY, bs, NULL);

  BK_VRETURN(B);

 error:
  if (bs)
    bk_ssl_destroy(B, bs, 0);

  BK_VRETURN(B);
}



/**
 * SSL shutdown handler.
 *
 *	@param B BAKA thread/global state.
 *	@param run Baka run environment
 *	@param fd File descriptor of activity
 *	@param gottype Type of activity
 *	@param opaque Private data
 *	@param starttide Official Time of activity
 */
void
ssl_shutdown_handler(bk_s B, struct bk_run *run, int fd, u_int gottype, void *opaque, const struct timeval *startime)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkssl");
  int ret;
  int ssl_err;
  int fdin, fdout;
  struct bk_ssl *bs = (struct bk_ssl *) opaque;

  if (!bs)
  {
    bk_error_printf(B, BK_ERR_ERR, "Internal error: invalid arguments.\n");
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

  ret = SSL_shutdown(bs->bs_ssl);
 
  if (ret == 1)
    goto done;
 
  ssl_err = SSL_get_error(bs->bs_ssl, ret);

  /* <TODO>SSL has this stupid shutdown handshake.  This means that close is now blocking.
   * The IOH system can't really deal with this without major modifications, so we give 
   * SSL one shot to do its little shutdown dance.  If that's not enough, just close the
   * socket anyway to avoid leaking memory.  At some point IOH could be overhauled to 
   * better support this, but it doesn't really seem worthwhile at the moment.</TODO>
   */
  goto done;

  if (ssl_err == SSL_ERROR_WANT_WRITE)
  {
    if ((fdout = SSL_get_wfd(bs->bs_ssl)) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Invalid write fd.\n");
      goto error;
    }

    // Always want read in case of eof.
    if (bk_run_setpref(B, bs->bs_run, fdout, BK_RUN_WANTREAD | BK_RUN_WANTWRITE, BK_RUN_WANTREAD | BK_RUN_WANTWRITE, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not request write.\n");
      goto error;
    }
  }
  else if (ssl_err == SSL_ERROR_WANT_READ)
  {
    if ((fdin = SSL_get_rfd(bs->bs_ssl)) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Invalid read fd.\n");
      goto error;
    }

    if (bk_run_setpref(B, bs->bs_run, fdin, BK_RUN_WANTREAD, BK_RUN_WANTREAD | BK_RUN_WANTWRITE, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not request read.\n");
      goto error;
    }
  }
  else if (ssl_err == SSL_ERROR_SYSCALL)
  {
    if (errno)
    {
      // If errno==0, the other end shut down abruptly.  Not really worth logging.
      bk_error_printf(B, BK_ERR_ERR, "Failed to cleanly close SSL connection: (syserror) %s.\n", strerror(errno));
    }
    goto error;
  }
  else
  {
    bk_error_printf(B, BK_ERR_ERR, "Failed to cleanly close SSL connection: %s.\n", ERR_error_string(ssl_err, NULL));
    goto error;
  }

  BK_VRETURN(B);

 done:
 error:
  if (bs)
    bk_ssl_destroy(B, bs, 0);
}



#ifdef BK_USING_PTHREADS
/**
 * Initialize global objects for SSL threading.
 *
 * This and other threading code based on (or downright copied from)
 * openssl/crypto/threads/mttest.c.
 *
 * @param B BAKA Thread/global state
 * @return <i>0</i> on success
 * @return <i>-1</i> on failure
 */
int ssl_threads_init(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkssl");
  int i;

  if (!(lock_count = OPENSSL_malloc(CRYPTO_num_locks() * sizeof(long))))
  {
    bk_error_printf(B, BK_ERR_ERR, "Malloc failed.\n");
    goto error;
  }

  if (!(lock_cs = OPENSSL_malloc(CRYPTO_num_locks() * sizeof(pthread_mutex_t))))
  {
    bk_error_printf(B, BK_ERR_ERR, "Malloc failed.\n");
    goto error;
  }

  /* <WARNING> Don't go to error until after the pthread_mutex_init loop.
   * Nothing too bad will happen, but you'll spew errors at the user.</WARNING>
   */

  for (i=0; i<CRYPTO_num_locks(); i++)
  {
    lock_count[i]=0;
    pthread_mutex_init(&(lock_cs[i]),NULL);
  }

  CRYPTO_set_id_callback(pthreads_thread_id);
  CRYPTO_set_locking_callback(pthreads_locking_callback);

  BK_RETURN(B, 0);

 error:
  ssl_threads_destroy(B);
  BK_RETURN(B, -1);
}



/**
 * Cleanup global objects for SSL threading.
 *
 * @param B BAKA Thread/global state
 */
void ssl_threads_destroy(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkssl");
  int i;

  CRYPTO_set_locking_callback(NULL);

  if (lock_cs)
  {
    for (i=0; i<CRYPTO_num_locks(); i++)
    {
      if (pthread_mutex_destroy(&(lock_cs[i])) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Failed to destroy mutex: %s.\n", strerror(errno));
	goto error;
      }
    }

    OPENSSL_free(lock_cs);
    lock_cs = NULL;
  }

  if (lock_count)
  {
    OPENSSL_free(lock_count);
    lock_count = NULL;
  }

  BK_VRETURN(B);

 error:
  BK_VRETURN(B);
}



/**
 * See threads(3).
 */
void pthreads_locking_callback(int mode, int type, const char *file, int line)
{
  if (mode & CRYPTO_LOCK)
  {
    pthread_mutex_lock(&(lock_cs[type]));
    lock_count[type]++;
  }
  else
  {
    pthread_mutex_unlock(&(lock_cs[type]));
  }
}



/**
 * See threads(3).
 */
unsigned long pthreads_thread_id(void)
{
  unsigned long ret;

  ret = (unsigned long) pthread_self();
  return ret;
}
#endif // BK_USING_PTHREADS



/**
 * Function to decide verification.  Right now, it
 * just returns whatever the value for preverify_ok was.
 *
 * @param ok Whether the cert passed verification with CA file
 * @param ctx Certificate info
 * @return <i>1</i> to authenticate
 * @return <i>0</i> to deny
 */
int
verify_callback(int preverify_ok, X509_STORE_CTX *ctx)
{
  return(preverify_ok);
}
