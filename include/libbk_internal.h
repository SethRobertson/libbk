/*
 * 
 *
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
 * Internal information required between libbk files which should NOT
 * be used by those outside libbk.
 */

#ifndef _libbk_internal_h_
#define _libbk_internal_h_
#include <libbk_i18n.h>


#define SCRATCHLEN (MAX(PATH_MAX,1024))
#define SCRATCHLEN2 ((SCRATCHLEN)-100)

/**
 * Information about a queue of data pending for I/O
 */
struct bk_ioh_queue
{
  u_int32_t		biq_queuelen;		///< Amount of non-consumed data current in queue
  u_int32_t		biq_queuemax;		///< Maximum of non-consumed data storable queue
  dict_h		biq_queue;		///< Queued data
  union
  {
    struct
    {
      u_int32_t 	remaining;		///< Number of bytes in previous complete block remaining
      bk_flags		flags;			///< Private flags
    }			block;			///< Block message types
    /* Space for future private info of additional message types */
  }			biq;			///< Private data for message types
};



/**
 * Information about a specific ioh.
 */
struct bk_ioh
{
  int			ioh_fdin;		///< Input file descriptor
  u_int32_t		ioh_fdin_savestate;	///< Information about fdin which we changed
#define IOH_ADDED_NONBLOCK	0x01		///< O_NONBLOCK was added to the fd
#define IOH_ADDED_OOBINLINE	0x02		///< SO_OOBINLINE was added to the fd
#define IOH_NUKED_LINGER	0x04		///< SO_LINGER was deleted from the fd
  int			ioh_fdout;		///< Output file descriptor
  u_int32_t		ioh_fdout_savestate;	///< Information about fdout which we changed
  bk_iorfunc_f 		ioh_readfun;		///< Function to read data
  bk_iowfunc_f		ioh_writefun;		///< Function to write data
  bk_iocfunc_f		ioh_closefun;		///< Function to close fds
  void		       *ioh_iofunopaque;	///< Opaque data for iofuns
  bk_iohhandler_f	ioh_handler;		///< Callback function for event handling
  void		       *ioh_opaque;		///< Opaque data for handler
  u_int32_t		ioh_inbuf_hint;		///< Hint for input buffer sizes
  struct bk_run	       *ioh_run;		///< BK_RUN environment
  struct bk_ioh_queue	ioh_readq;		///< Data queued for reading
  struct bk_ioh_queue	ioh_writeq;		///< Data queued for writing
  char			ioh_eolchar;		///< End of line character
  bk_flags		ioh_deferredclosearg;	///< Flags argument to deferred close
  int			ioh_throttle_cnt;	///< How many people want to block reads.
  void		       *ioh_readallowedevent;	///< Event to schedule user queue drain after readallowed
  int			ioh_compress_level;	///< The level to use for compression
  int			ioh_errno;		///< Last errno for this ioh
  size_t		ioh_maxiov;		///< Maximum # iovs / writev
  off_t			ioh_size;		///< The size of the resource (for "follow" mode).
  off_t			ioh_tell;		///< My current position in the stream.
  int			ioh_follow_pause;	///< Time to wait between fstat(2)'s in follow mode.
  void *		ioh_recheck_event;	///< Event handle for recheck event.
  bk_flags		ioh_extflags;		///< Flags--see libbk.h
  bk_flags		ioh_intflags;		///< Flags
#define IOH_FLAGS_SHUTDOWN_INPUT	0x01	///< Input shut down
#define IOH_FLAGS_SHUTDOWN_OUTPUT	0x02	///< Output shut down
#define IOH_FLAGS_SHUTDOWN_OUTPUT_PEND	0x04	///< Output shut down is deferred
#define IOH_FLAGS_SHUTDOWN_CLOSING	0x08	///< Attempting to close
#define IOH_FLAGS_SHUTDOWN_DESTROYING	0x10	///< Attempting to destroy
#define IOH_FLAGS_DONTCLOSEFDS		0x40	///< Don't close FDs on death
#define IOH_FLAGS_ERROR_INPUT		0x80	///< Input had I/O error or EOF
#define IOH_FLAGS_ERROR_OUTPUT		0x100	///< Output had I/O error
#define IOH_FLAGS_CLOSE_PENDING		0x200	///< We want to close, but others are using the IOH
#define IOH_FLAGS_IN_WRITE		0x400	///< Outputting data now--further writes deferred
  u_int			ioh_incallback;		///< Number of callbacks to user
#ifdef BK_USING_PTHREADS
  u_int			ioh_waiting;		///< Number of people waiting
  pthread_t		ioh_userid;		///< Thread ID of person in user callback
  pthread_cond_t	ioh_cond;		///< Pthread condition for other threads to wait on
  pthread_mutex_t	ioh_lock;		///< Lock on this ioh
#endif /* BK_USING_PTHREADS */
};



#ifdef BK_USING_PTHREADS
extern pthread_mutex_t BkGlobalSignalLock;
#endif /* BK_USING_PTHREADS */


/* FRIENDLY FUNCTIONS */
// This macro is an internal, fast version of bk_ioh_data_seize_permitted().
#define IOH_DATA_SEIZE_PERMITTED(ioh) (BK_FLAG_ISSET((ioh)->ioh_extflags, BK_IOH_RAW | BK_IOH_VECTORED | BK_IOH_BLOCKED))


extern void bk_run_signal_ihandler(int signum);

/* b_netutils.c */
extern int bk_netutils_make_conn_verbose_std(bk_s B, struct bk_run *run, const char *rurl, const char *defrhost, const char *defrserv, const char *lurl, const char *deflhost, const char *deflserv, const char *defproto, u_long timeout, bk_bag_callback_f callback, void *args, bk_flags flags );
extern int bk_netutils_start_service_verbose_std(bk_s B, struct bk_run *run, const char *url, const char *defhoststr, const char *defservstr, const char *defprotostr, const char *securenets, bk_bag_callback_f callback, void *args, int backlog, bk_flags flags);

/* b_addrgroup (why?) */
extern int bk_netutils_commandeer_service_std(bk_s B, struct bk_run *run, int s, const char *securenets, bk_bag_callback_f callback, void *args, bk_flags flags);

/* b_ssl.c */
extern int bk_ssl_start_service_verbose(bk_s B, struct bk_run *run, const char *url, const char *defhoststr, const char *defservstr, const char *defprotostr, const char *securenets, bk_bag_callback_f callback, void *args, int backlog, const char *key_file, const char *cert_file, const char *ca_file, const char *dhparam_file, bk_flags ctx_flags, bk_flags flags);
extern int bk_ssl_make_conn_verbose(bk_s B, struct bk_run *run, const char *rurl, const char *defrhost, const char *defrserv, const char *lurl, const char *deflhost, const char *deflserv, const char *defproto, u_long timeout, bk_bag_callback_f callback, void *args, const char *key_file, const char *cert_file, const char *ca_file, const char *dhparam_file, bk_flags ctx_flags, bk_flags flags);
extern int bk_ssl_netutils_commandeer_service(bk_s B, struct bk_run *run, int s, const char *securenets, bk_bag_callback_f callback, void *args, const char *key_file, const char *cert_file, const char *ca_file, const char *dhparam_file, bk_flags ctx_flags, bk_flags flags);
extern struct bk_ioh *bk_ssl_ioh_init(bk_s B, struct bk_ssl *ssl, int fdin, int fdout, bk_iohhandler_f handler, void *opaque, u_int32_t inbufhint, u_int32_t inbufmax, u_int32_t outbufmax, struct bk_run *run, bk_flags flags);


/* b_ioh.c */
extern struct bk_ioh *bk_ioh_init_std(bk_s B, int fdin, int fdout, bk_iohhandler_f handler, void *opaque, u_int32_t inbufhint, u_int32_t inbufmax, u_int32_t outbufmax, struct bk_run *run, bk_flags flags);




#endif /* _libbk_internal_h_ */
