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
 * Association of file descriptor/handle to callback.  Queue data for
 * output, translate input stream into messages.
 */

#include <libbk.h>
#include "libbk_internal.h"

/*
 * Feel free to turn this back on if you find a use for it - awb doesn't use it
 * and it is unclear how well tested it is.
 */
#if 0
#define IOH_COMPRESS_SUPPORT
#include <zlib.h>
#endif


#define IOH_FLAG_ALREADYLOCKED		0x80000	///< Signal functions that ioh is already locked


#if defined(EWOULDBLOCK) && defined(EAGAIN)
#define IOH_EBLOCKINGINTR (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR) ///< real error or just normal behavior?
#else
#if defined(EWOULDBLOCK)
#define IOH_EBLOCKINGINTR (errno == EWOULDBLOCK || errno == EINTR)	///< real error or just normal behavior?
#else
#if defined(EAGAIN)
#define IOH_EBLOCKINGINTR (errno == EAGAIN || errno == EINTR)	///< real error or just normal behavior?
#else
#define IOH_EBLOCKINGINTR (errno == EINTR)		///< real error or just normal behavior?
#endif
#endif
#endif

#define IOH_COMPRESS_BLOCK_SIZE	32768	///< Basic block size to use in compression.

/*
 * Shutdown only woks on full duplex (eg. network) descriptors. Others have to be closed.
 */
#define BK_IOH_SHUTDOWN(i,h)			\
do {						\
  if ((i)->ioh_fdin == (i)->ioh_fdout)		\
  {						\
    shutdown((i)->ioh_fdin, (h));		\
  }						\
  else						\
  {						\
    if ((h) == SHUT_RD || (h) == SHUT_RDWR)	\
    {						\
      close((i)->ioh_fdin);			\
    }						\
						\
    if ((h) == SHUT_WR || (h) == SHUT_RDWR)	\
    {						\
      close((i)->ioh_fdout);			\
    }						\
  }						\
} while (0)

#ifdef BK_USING_PTHREADS
// Call user function: precondition--ioh locked, not in user callback
#define CALL_BACK(B, ioh, data, state)								\
 do												\
 {												\
   ioh->ioh_incallback++;                                                                       \
   if (BK_GENERAL_FLAG_ISTHREADON(B))								\
   {												\
     ioh->ioh_userid = pthread_self();                                                          \
     if (pthread_mutex_unlock(&ioh->ioh_lock) != 0)						\
       abort();											\
   }												\
   bk_debug_printf_and(B, 2, "Calling user callback for ioh %p with state %d\n",(ioh),(state));	\
   if ((ioh)->ioh_handler)                                                                      \
     ((*((ioh)->ioh_handler))((B),(data), (ioh)->ioh_opaque, (ioh), (state)));			\
   if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&ioh->ioh_lock) != 0)                \
     abort();                                                                                   \
   ioh->ioh_incallback--;                                                                       \
   if (BK_GENERAL_FLAG_ISTHREADON(B))								\
   {												\
     BK_ZERO(&ioh->ioh_userid);                                                                 \
     pthread_cond_broadcast(&ioh->ioh_cond);							\
   }												\
 } while (0)					///< Function to evaluate user callback with new data/state information
#else /* BK_USING_PTHREADS */

///< Function to evaluate user callback with new data/state information
#define CALL_BACK(B, ioh, data, state)								 \
 do												 \
 {												 \
   (ioh)->ioh_incallback++;									 \
   bk_debug_printf_and(B, 2, "Calling user callback for ioh %p with state %d\n", (ioh),(state)); \
   ((*((ioh)->ioh_handler))((B),(data), (ioh)->ioh_opaque, (ioh), (state)));			 \
   (ioh)->ioh_incallback--;									 \
 } while (0)
#endif /* BK_USING_PTHREADS */


#define IOH_DEFAULT_DATA_SIZE	128		///< Default read size (optimized for user and protocol traffic, not bulk data transfer)
#define IOH_VS			2		///< Number of vectors to hold length and msg
#define IOH_EOLCHAR		'\n'		///< End of line character (for line oriented mode--change to m


/**
 * Seek info
 */
struct ioh_seek_args
{
  off_t		isa_offset;			///< Seek offset a la lseek(2)
  int		isa_whence;			///< Whence a la lseek(2)
};



/**
 * Minimum size of printf buffer
 */
enum {MinBufSize = 512};



/**
 * @name Ioh Data Information
 */
// @{
/**
 * Data command types
 */
typedef enum
{
  IohDataCmdNone=0,				///< Convenience value for ignore cmds.
  IohDataCmdShutdown,				///< When mark arrives, shut everything down
  IohDataCmdClose,				///< When mark arrives, close everything down
  IohDataCmdSeek,				///< When mark arrives, seek.
} ioh_data_cmd_type_e;



/**
 * Command information stored in bid
 */
struct ioh_data_cmd
{
  bk_flags		idc_flags;		///< Everyone needs flags
  ioh_data_cmd_type_e	idc_type;		///< Command type
  void *		idc_args;		///< Command args
};



/**
 * Information about a chunk of user data in use by the ioh subsystem
 */
struct bk_ioh_data
{
  struct bk_generic_dll_element bid_hold;	///< Space holder for bk_dll
  char		       *bid_data;		///< Actual data
  u_int32_t		bid_allocated;		///< Allocated size of data
  u_int32_t		bid_inuse;		///< Amount actually used (!including consumed)
  u_int32_t		bid_used;		///< Amount virtually consumed (written or sent to user)
  bk_vptr	       *bid_vptr;		///< Stored vptr to return in callback
  bk_flags		bid_flags;		///< Additional information about this data
#define BID_FLAG_MESSAGE	0x01		///< This is a message boundary
  struct ioh_data_cmd	bid_idc;		///< Command info.
};

// @}


static int ioh_dequeue_byte(bk_s B, struct bk_ioh *ioh, struct bk_ioh_queue *iohq, u_int32_t bytes, bk_flags flags);
static int ioh_dequeue(bk_s B, struct bk_ioh *ioh, struct bk_ioh_queue *iohq, struct bk_ioh_data *bid, bk_flags flags);
#define IOH_DEQUEUE_ABORT		0x01	///< Tell user data is aborted
static int ioh_getlastbuf(bk_s B, struct bk_ioh_queue *queue, u_int32_t *size, char **data, struct bk_ioh_data **bid, bk_flags flags);
static int ioh_internal_read(bk_s B, struct bk_ioh *ioh, int fd, char *data, size_t len, bk_flags flags);
static void ioh_sendincomplete_up(bk_s B, struct bk_ioh *ioh, u_int32_t filter, bk_flags flags);
static int ioh_execute_ifspecial(bk_s B, struct bk_ioh *ioh, struct bk_ioh_queue *queue, bk_flags flags);
static int ioh_execute_cmds(bk_s B, struct bk_ioh *ioh, dict_h cmds, bk_flags flags);
static void ioh_flush_queue(bk_s B, struct bk_ioh *ioh, struct bk_ioh_queue *queue, dict_h *cmd, bk_flags flags);
#define IOH_FLUSH_DESTROY	1		///< Notify that queue is being destroyed
static int ioh_queue(bk_s B, struct bk_ioh_queue *iohq, char *data, u_int32_t allocated, u_int32_t inuse, u_int32_t used, bk_vptr *vptr, bk_flags msgflags, ioh_data_cmd_type_e cmd, void *cmd_args, bk_flags flags);
static void ioh_runhandler(bk_s B, struct bk_run *run, int fd, u_int gottypes, void *opaque, const struct timeval *starttime);
static int bk_ioh_fdctl(bk_s B, int fd, u_int32_t *savestate, bk_flags flags);
#define IOH_FDCTL_SET		1		///< Set the fd set to the ioh normal version
#define IOH_FDCTL_RESET		1		///< Set the fd set to the original defaults
static int ioh_close(bk_s B, struct bk_ioh *ioh, bk_flags flags);
static void bk_ioh_destroy(bk_s B, struct bk_ioh *ioh);
static struct ioh_data_cmd *idc_create(bk_s B);
static void idc_destroy(bk_s B, struct ioh_data_cmd *idc);
static void bk_ioh_userdrainevent(bk_s B, struct bk_run *run, void *opaque, const struct timeval starttime, bk_flags flags);
static void check_follow(bk_s B, struct bk_ioh *ioh, bk_flags flags);
static void recheck_follow(bk_s B, struct bk_run *run, void *opaque, const struct timeval starttime, bk_flags flags);
static int compress_write(bk_s B, struct bk_ioh *ioh, bk_iowfunc_f writefun, void *opaque, int fd, struct iovec *buf, __SIZE_TYPE__ size, bk_flags flags);



/**
 * @name IOH message type queuing functions
 * Data sent from the user must be placed on the output queue prior to actual system output.
 * This allows transformation (vectoring, blocking, etc) to take place.
 */
// @{
static int ioht_raw_queue(bk_s B, struct bk_ioh *ioh, bk_vptr *data, bk_flags flags);
static int ioht_block_queue(bk_s B, struct bk_ioh *ioh, bk_vptr *data, bk_flags flags);
static int ioht_vector_queue(bk_s B, struct bk_ioh *ioh, bk_vptr *data, bk_flags flags);
static int ioht_line_queue(bk_s B, struct bk_ioh *ioh, bk_vptr *data, bk_flags flags);
// @}


/**
 * @name IOH message type for exceptional events functions
 * When the user issues a shutdown, flush, close, etc: the message type functions
 * may have to take special actions with the queued data.
 */
// @{
static int ioht_raw_other(bk_s B, struct bk_ioh *ioh, u_int data, u_int cmd, bk_flags flags);
static int ioht_block_other(bk_s B, struct bk_ioh *ioh, u_int data, u_int cmd, bk_flags flags);
static int ioht_vector_other(bk_s B, struct bk_ioh *ioh, u_int data, u_int cmd, bk_flags flags);
static int ioht_line_other(bk_s B, struct bk_ioh *ioh, u_int data, u_int cmd, bk_flags flags);
#define IOHT_HANDLER		1		///< Other command is a run_handler (determine size)
#define IOHT_HANDLER_RMSG	2		///< Other command is a run_handler (read new data)
#define IOHT_FLUSH		3		///< Other command is a flush
// @}



/**
 * @name Defines: biq_clc
 * Queue of data pending for I/O CLC definition
 * to hide CLC choice.
 */
// @{
#define biq_create(o,k,f,a)		bk_dll_create((o),(k),(f))
#define biq_destroy(h)			bk_dll_destroy(h)
#define biq_insert(h,o)			bk_dll_insert(h,o)
#define biq_insert_uniq(h,n,o)		bk_dll_insert_uniq(h,n,o)
#define biq_append(h,o)			bk_dll_append(h,o)
#define biq_append_uniq(h,n,o)		bk_dll_append_uniq(h,n,o)
#define biq_search(h,k)			bk_dll_search(h,k)
#define biq_delete(h,o)			bk_dll_delete(h,o)
#define biq_minimum(h)			bk_dll_minimum(h)
#define biq_maximum(h)			bk_dll_maximum(h)
#define biq_successor(h,o)		bk_dll_successor(h,o)
#define biq_predecessor(h,o)		bk_dll_predecessor(h,o)
#define biq_iterate(h,d)		bk_dll_iterate(h,d)
#define biq_nextobj(h,i)		bk_dll_nextobj(h,i)
#define biq_iterate_done(h,i)		bk_dll_iterate_done(h,i)
#define biq_error_reason(h,i)		bk_dll_error_reason(h,i)
// @}

/**
 * @name Defines: IOH Data Commands List clc
 * Abstracts the list of commands to run from various queues.
 */
// @{
#define cmd_list_create(o,k,f)		dll_create(o,k,f)
#define cmd_list_destroy(h)		dll_destroy(h)
#define cmd_list_insert(h,o)		dll_insert(h,o)
#define cmd_list_insert_uniq(h,n,o)	dll_insert_uniq(h,n,o)
#define cmd_list_append(h,o)		dll_append(h,o)
#define cmd_list_append_uniq(h,n,o)	dll_append_uniq(h,n,o)
#define cmd_list_search(h,k)		dll_search(h,k)
#define cmd_list_delete(h,o)		dll_delete(h,o)
#define cmd_list_minimum(h)		dll_minimum(h)
#define cmd_list_maximum(h)		dll_maximum(h)
#define cmd_list_successor(h,o)		dll_successor(h,o)
#define cmd_list_predecessor(h,o)	dll_predecessor(h,o)
#define cmd_list_iterate(h,d)		dll_iterate(h,d)
#define cmd_list_nextobj(h,i)		dll_nextobj(h,i)
#define cmd_list_iterate_done(h,i)	dll_iterate_done(h,i)
#define cmd_list_error_reason(h,i)	dll_error_reason(h,i)
// @}


/**
 * Create and initialize the ioh environment.
 *
 * THREADS: MT-SAFE
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
struct bk_ioh *bk_ioh_init(bk_s B, struct bk_ssl *ssl, int fdin, int fdout, bk_iohhandler_f handler, void *opaque, u_int32_t inbufhint, u_int32_t inbufmax, u_int32_t outbufmax, struct bk_run *run, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

#ifndef NO_SSL
  if (ssl && bk_ssl_supported(B))
  {
    BK_RETURN(B, bk_ssl_ioh_init(B, ssl, fdin, fdout, handler, opaque, inbufhint, inbufmax, outbufmax, run, flags));
  }
#endif /* NO_SSL */

  BK_RETURN(B, bk_ioh_init_std(B, fdin, fdout, handler, opaque, inbufhint, inbufmax, outbufmax, run, flags));
}



/**
 * Create and initialize the ioh environment.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state
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
struct bk_ioh *bk_ioh_init_std(bk_s B, int fdin, int fdout, bk_iohhandler_f handler, void *opaque, u_int32_t inbufhint, u_int32_t inbufmax, u_int32_t outbufmax, struct bk_run *run, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ioh *curioh = NULL;
  int tmp;
  bk_iorfunc_f readfun = bk_ioh_stdrdfun;
  bk_iowfunc_f writefun = bk_ioh_stdwrfun;
  bk_iocfunc_f closefun = bk_ioh_stdclosefun;
  struct stat st;

  if ((fdin < 0 && fdout < 0) || !run)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!handler && BK_FLAG_ISCLEAR(flags, BK_IOH_NO_HANDLER))
  {
    bk_error_printf(B, BK_ERR_WARN, "No handler specified. I hope you know what you are doing (like entering a relay)\n");
  }


  // Check for invalid flags combinations
  tmp = 0;
  if (BK_FLAG_ISSET(flags, BK_IOH_RAW)) tmp++;
  if (BK_FLAG_ISSET(flags, BK_IOH_BLOCKED)) tmp++;
  if (BK_FLAG_ISSET(flags, BK_IOH_VECTORED)) tmp++;
  if (BK_FLAG_ISSET(flags, BK_IOH_LINE)) tmp++;

  if (tmp != 1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Must have (exactly) one parse method in flags (RAW/BLOCKED/VECTORED/LINE)\n");
    BK_RETURN(B, NULL);
  }

  if (BK_FLAG_ISSET(flags, BK_IOH_WRITE_ALL) && !BK_FLAG_ISSET(flags, BK_IOH_RAW))
  {
    bk_error_printf(B, BK_ERR_ERR, "IOH_WRITE_ALL is only valid with IOH_RAW\n");
    BK_RETURN(B, NULL);
  }

  // Check for invalid flags combinations
  tmp = 0;
  if (BK_FLAG_ISSET(flags, BK_IOH_STREAM)) tmp++;

  if (tmp != 1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Must have exactly one input type in flags (STREAM)\n");
    BK_RETURN(B, NULL);
  }

  if (BK_FLAG_ISSET(flags, BK_IOH_FOLLOW) && (fdin < 0))
  {
    bk_error_printf(B, BK_ERR_WARN, "Follow mode may only be used when reading\n");
    BK_FLAG_CLEAR(flags, BK_IOH_FOLLOW);
  }


  if (!BK_CALLOC(curioh))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate ioh structure: %s\n",strerror(errno));
    BK_RETURN(B, NULL);
  }

#ifdef BK_USING_PTHREADS
  pthread_mutex_init(&curioh->ioh_lock, NULL);
  if (pthread_cond_init(&curioh->ioh_cond, NULL) < 0)
    abort();
#endif /* BK_USING_PTHREADS */

  curioh->ioh_fdin = -1;
  curioh->ioh_fdout = -1;

  if (!(curioh->ioh_readq.biq_queue = biq_create(NULL, NULL, DICT_UNORDERED, NULL)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate read queue: %s\n",biq_error_reason(NULL, NULL));
    goto error;
  }

  if (!(curioh->ioh_writeq.biq_queue = biq_create(NULL, NULL, DICT_UNORDERED, NULL)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate write queue: %s\n",biq_error_reason(NULL, NULL));
    goto error;
  }

  curioh->ioh_readfun = readfun;
  curioh->ioh_writefun = writefun;
  curioh->ioh_closefun = closefun;
  curioh->ioh_handler = handler;
  curioh->ioh_opaque = opaque;
  curioh->ioh_inbuf_hint = inbufhint?inbufhint:IOH_DEFAULT_DATA_SIZE;
  curioh->ioh_readq.biq_queuemax = inbufmax;
  curioh->ioh_writeq.biq_queuemax = outbufmax;
  curioh->ioh_run = run;
  curioh->ioh_extflags = flags;
  curioh->ioh_eolchar = IOH_EOLCHAR;

  // this is basically a system limit, but it's just as easy to make it per-ioh
#if defined(_SC_IOV_MAX)
  curioh->ioh_maxiov = sysconf(_SC_IOV_MAX);
#elif defined(UIO_MAXIOV)
  curioh->ioh_maxiov = UIO_MAXIOV;
#elif defined(_XOPEN_IOV_MAX)
  curioh->ioh_maxiov = _XOPEN_IOV_MAX;
#else
  curioh->ioh_maxiov = 8;			// conservative guesstimate
#endif

  curioh->ioh_fdin = fdin;
  if (curioh->ioh_fdin >= 0)
  {
    if (BK_FLAG_ISCLEAR(flags, BK_IOH_DONT_ACTIVATE) && BK_FLAG_ISCLEAR(flags, BK_IOH_NO_HANDLER))
    {
      if (bk_run_handle(B, curioh->ioh_run, curioh->ioh_fdin, ioh_runhandler, curioh, BK_RUN_WANTREAD, 0) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not put this new read ioh into the bk_run environment\n");
	goto error;
      }
    }

    bk_ioh_fdctl(B, curioh->ioh_fdin, &curioh->ioh_fdin_savestate, IOH_FDCTL_SET);
  }
  else
  {
    BK_FLAG_SET(curioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_INPUT);
  }

  curioh->ioh_fdout = fdout;
  if (curioh->ioh_fdout >= 0)
  {
    if (curioh->ioh_fdin != curioh->ioh_fdout)
    {
      if (BK_FLAG_ISCLEAR(flags, BK_IOH_DONT_ACTIVATE) && BK_FLAG_ISCLEAR(flags, BK_IOH_NO_HANDLER))
      {
	if (bk_run_handle(B, curioh->ioh_run, curioh->ioh_fdout, ioh_runhandler, curioh, 0, 0) < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not put this new write ioh into the bk_run environment\n");
	  goto error;
	}
      }

      // Examine file descriptor for proper file and socket options
      bk_ioh_fdctl(B, curioh->ioh_fdout, &curioh->ioh_fdout_savestate, IOH_FDCTL_SET);
    }
  }
  else
  {
    BK_FLAG_SET(curioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_OUTPUT);
  }

  // We'll also stat in check_follow(), but how often is this really called?
  if (BK_FLAG_ISSET(flags, BK_IOH_FOLLOW))
  {
    if (fstat(fdin, &st) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not stat input descriptor: %s\n", strerror(errno));
      goto error;
    }

    if (!S_ISREG(st.st_mode))
    {
      bk_error_printf(B, BK_ERR_NOTICE, "Non-regular files are implicitly followed\n");
      BK_FLAG_CLEAR(flags, BK_IOH_FOLLOW);
    }
    else
    {
      if (BK_STRING_ATOI(B, BK_GWD(B, "read_follow_pause", "1"),
			 &curioh->ioh_follow_pause, 0) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "invalid read_follow_pause\n");
	goto error;
      }
      check_follow(B, curioh, 0);
    }
  }

  bk_debug_printf_and(B, 1, "Created IOH %p for fds %d %d\n",curioh,fdin, fdout);
  BK_RETURN(B, curioh);

 error:
  if (curioh)
  {
    if (curioh->ioh_readq.biq_queue)
      biq_destroy(curioh->ioh_readq.biq_queue);
    if (curioh->ioh_writeq.biq_queue)
      biq_destroy(curioh->ioh_writeq.biq_queue);
    if (curioh->ioh_fdin >= 0)
      bk_run_close(B, curioh->ioh_run, curioh->ioh_fdin, 0);
    if (curioh->ioh_fdout >= 0 && curioh->ioh_fdin != curioh->ioh_fdout)
      bk_run_close(B, curioh->ioh_run, curioh->ioh_fdout, 0);
#ifdef BK_USING_PTHREADS
    if (pthread_mutex_destroy(&curioh->ioh_lock) != 0)
      abort();
    if (pthread_cond_destroy(&curioh->ioh_cond) != 0)
      abort();
#endif /* BK_USING_PTHREADS */
    free(curioh);
  }
  BK_RETURN(B, NULL);
}



/**
 * Update various configuration parameters of a IOH instance
 *
 * THREADS: MT-SAFE (assuming different ioh)
 * THREADS: THREAD-REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state
 *	@param ioh The IOH environment to update
 *	@param readfun The function to use to read data.
 *	@param writefun The function to use to write data
 *	@param closefun The function to use to close fds
 *	@param iofunopaque The opaque data for the I/O functions
 *	@param handler The user callback to notify on complete I/O or other events
 *	@param opaque The opaque data for the user callback.
 *	@param inbufhint A hint for the input routines (0 for 128 bytes)
 *	@param inbufmax The maximum buffer size of incomplete data (0 for unlimited)
 *	@param outbufmax The maximum amount of data queued for transmission (0 for unlimited)
 *	       -- lowered maximum will not affect previously queued data
 *	@param flags The type of data on the file descriptors.
 *	       -- not all (any?) flags changes will take affect or will have a positive effect--handle with care
 *	@return <i>-1<i> on call failure.
 *	@return <i>2<i> on success, but ioh destroyed (probably peer reset).
 *	@return <br><i>0</i> on success.
 */
int bk_ioh_update(bk_s B, struct bk_ioh *ioh, bk_iorfunc_f readfun, bk_iowfunc_f writefun, bk_iocfunc_f closefun, void *iofunopaque, bk_iohhandler_f handler, void *opaque, u_int32_t inbufhint, u_int32_t inbufmax, u_int32_t outbufmax, bk_flags flags, bk_flags updateflags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  bk_flags old_extflags = 0;
  int ret = 0;

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  bk_debug_printf_and(B, 1, "Updating IOH parameters %p\n",ioh);

  old_extflags = ioh->ioh_extflags;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&ioh->ioh_lock) != 0)
    abort();
  if (BK_GENERAL_FLAG_ISTHREADON(B))
  {
    while (ioh->ioh_incallback && !pthread_equal(ioh->ioh_userid, pthread_self()))
    {
      ioh->ioh_waiting++;
      pthread_cond_wait(&ioh->ioh_cond, &ioh->ioh_lock);
      ioh->ioh_waiting--;
    }
  }
#endif /* BK_USING_PTHREADS */

  if (BK_FLAG_ISSET(updateflags, BK_IOH_UPDATE_READFUN))
    ioh->ioh_readfun = readfun;
  if (BK_FLAG_ISSET(updateflags, BK_IOH_UPDATE_WRITEFUN))
    ioh->ioh_writefun = writefun;
  if (BK_FLAG_ISSET(updateflags, BK_IOH_UPDATE_CLOSEFUN))
    ioh->ioh_closefun = closefun;
  if (BK_FLAG_ISSET(updateflags, BK_IOH_UPDATE_IOFUNOPAQUE))
    ioh->ioh_iofunopaque = iofunopaque;
  if (BK_FLAG_ISSET(updateflags, BK_IOH_UPDATE_HANDLER))
    ioh->ioh_handler = handler;
  if (BK_FLAG_ISSET(updateflags, BK_IOH_UPDATE_OPAQUE))
    ioh->ioh_opaque = opaque;
  if (BK_FLAG_ISSET(updateflags, BK_IOH_UPDATE_INBUFHINT))
    ioh->ioh_inbuf_hint = inbufhint;
  if (BK_FLAG_ISSET(updateflags, BK_IOH_UPDATE_INBUFMAX))
    ioh->ioh_readq.biq_queuemax = inbufmax;
  if (BK_FLAG_ISSET(updateflags, BK_IOH_UPDATE_OUTBUFMAX))
    ioh->ioh_writeq.biq_queuemax = outbufmax;
  if (BK_FLAG_ISSET(updateflags, BK_IOH_UPDATE_FLAGS))
    ioh->ioh_extflags = flags;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B))
    pthread_cond_broadcast(&ioh->ioh_cond);
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (ioh->ioh_handler)
    BK_FLAG_CLEAR(ioh->ioh_extflags, BK_IOH_NO_HANDLER);

  if ((BK_FLAG_ISSET(old_extflags, BK_IOH_DONT_ACTIVATE) && BK_FLAG_ISCLEAR(flags, BK_IOH_DONT_ACTIVATE)) ||
      (BK_FLAG_ISSET(old_extflags, BK_IOH_NO_HANDLER) && ioh->ioh_handler))
  {

    // Want activation
    if (ioh->ioh_fdin >= 0)
    {
      if (bk_run_handle(B, ioh->ioh_run, ioh->ioh_fdin, ioh_runhandler, ioh, BK_RUN_WANTREAD, 0) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not put this new read ioh into the bk_run environment\n");
	goto error;
      }
    }

    if ((ioh->ioh_fdout >= 0) && (ioh->ioh_fdout != ioh->ioh_fdin))
    {
      if (bk_run_handle(B, ioh->ioh_run, ioh->ioh_fdout, ioh_runhandler, ioh, 0, 0) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not put this new write ioh into the bk_run environment\n");
	goto error;
      }
    }
  }
  else if (BK_FLAG_ISCLEAR(old_extflags, BK_IOH_DONT_ACTIVATE) && BK_FLAG_ISSET(flags, BK_IOH_DONT_ACTIVATE))
  {
    // Want deactivation
    if (ioh->ioh_fdin >= 0)
    {
      bk_run_close(B, ioh->ioh_run, ioh->ioh_fdin, 0);
    }

    if ((ioh->ioh_fdout >= 0) && (ioh->ioh_fdout != ioh->ioh_fdin))
    {
      bk_run_close(B, ioh->ioh_run, ioh->ioh_fdout, 0);
    }
  }

  if (BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_CLOSE_PENDING))
  {
    BK_FLAG_CLEAR(ioh->ioh_intflags, IOH_FLAGS_CLOSE_PENDING);
    if (ioh_close(B, ioh, ioh->ioh_deferredclosearg))
      ret = 2;					// ioh is history
  }

  BK_RETURN(B, ret);

 error:
  BK_RETURN(B, -1);
}



/**
 * Request various configuration parameters of a IOH instance.
 *
 * THREADS: MT-SAFE (assuming different ioh)
 * THREADS: THREAD-REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state
 *	@param ioh The IOH environment to update
 *	@param fdin The file descriptor to read from.  -1 if no input is desired.
 *	@param fdout The file descriptor to write to.  -1 if no output is desired.
 *	@param readfun The function to use to read data.
 *	@param writefun The function to use to write data.
 *	@param closefun The function to use to close fds.
 *	@param iofunopaque The I/O functions opaque data
 *	@param handler The user callback to notify on complete I/O or other events
 *	@param opaque The opaque data for the user callback.
 *	@param inbufhint A hint for the input routines (0 for 128 bytes)
 *	@param inbufmax The maximum buffer size of incomplete data (0 for unlimited)
 *	@param outbufmax The maximum amount of data queued for transmission (0 for unlimited)
 *	@param run The bk run environment to use with the fd.
 *	@param flags The type of data on the file descriptors.
 *	@return <i>-1</i> on call failure.
 *	@return <br><i>0</i> on success.
 */
int bk_ioh_get(bk_s B, struct bk_ioh *ioh, int *fdin, int *fdout, bk_iorfunc_f *readfun, bk_iowfunc_f *writefun, bk_iocfunc_f *closefun, void **iofunopaque, bk_iohhandler_f *handler, void **opaque, u_int32_t *inbufhint, u_int32_t *inbufmax, u_int32_t *outbufmax, struct bk_run **run, bk_flags *flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  bk_debug_printf_and(B, 1, "Obtaining IOH parameters %p\n",ioh);

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (fdin) *fdin = ioh->ioh_fdin;
  if (fdout) *fdout = ioh->ioh_fdout;
  if (readfun) *readfun = ioh->ioh_readfun;
  if (writefun) *writefun = ioh->ioh_writefun;
  if (closefun) *closefun = ioh->ioh_closefun;
  if (iofunopaque) *iofunopaque = ioh->ioh_iofunopaque;
  if (handler) *handler = ioh->ioh_handler;
  if (opaque) *opaque = ioh->ioh_opaque;
  if (inbufhint) *inbufhint = ioh->ioh_inbuf_hint;
  if (inbufmax) *inbufmax = ioh->ioh_readq.biq_queuemax;
  if (outbufmax) *outbufmax = ioh->ioh_readq.biq_queuemax;
  if (run) *run = ioh->ioh_run;
  if (flags) *flags = ioh->ioh_extflags;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B, 0);
}



/**
 * Enqueue data for output, if allowed.
 *
 * THREADS: MT-SAFE (assuming different ioh)
 * THREADS: THREAD-REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state
 *	@param ioh The IOH environment to update
 *	@param data The data to be sent (vptr and inside data will be returned in callback for free or other handling--must remain valid until then)
 *	@param flags BK_IOH_BYPASSQUEUEFULL will bypass checks for queue size
 *	@return <i>-1</i> on call failure or subsystem refusal
 *	@return <i>0</i> on success
 *	@return <i>1</i> on queue too full
 */
int bk_ioh_write(bk_s B, struct bk_ioh *ioh, bk_vptr *data, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret;

  if (!ioh || !data)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  bk_debug_printf_and(B, 1, "Writing %d bytes to IOH %p\n",data->len, ioh);


  if (BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_CLOSING | IOH_FLAGS_SHUTDOWN_DESTROYING | IOH_FLAGS_ERROR_OUTPUT | IOH_FLAGS_SHUTDOWN_OUTPUT | IOH_FLAGS_SHUTDOWN_OUTPUT_PEND))
  {
    bk_error_printf(B, BK_ERR_ERR, "Cannot write after shutdown/close\n");
    BK_RETURN(B, -1);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_RAW))
  {
    ret = ioht_raw_queue(B, ioh, data, flags);
  }
  else if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_BLOCKED))
  {
    ret = ioht_block_queue(B, ioh, data, flags);
  }
  else if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_VECTORED))
  {
    ret = ioht_vector_queue(B, ioh, data, flags);
  }
  else if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_LINE))
  {
    ret = ioht_line_queue(B, ioh, data, flags);
  }
  else
  {
    bk_error_printf(B, BK_ERR_ERR, "Unknown message format type %x\n",ioh->ioh_extflags);
    CALL_BACK(B, ioh, data, BkIohStatusWriteAborted);

#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&ioh->ioh_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */

    BK_RETURN(B, -1);
  }

  if (ret < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not append user data to outgoing message queue\n");
    CALL_BACK(B, ioh, data, BkIohStatusWriteAborted);

#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&ioh->ioh_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */

    BK_RETURN(B, -1);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B, ret);
}



/**
 * Tell the system that no further system I/O will be permitted.
 *
 * If data is currently queued for input, this data will sent to the user as incomplete.
 * If data is currently queued for output, this data will be drained before the shutdown takes effect.
 * See @a bk_ioh_flush for a way to avoid this.
 *
 * Note: ioh *may* be destroyed after this call, due to connection reset or
 * pending close.
 *
 * THREADS: MT-SAFE (assuming different ioh)
 * THREADS: THREAD-REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state
 *	@param ioh The IOH environment to update
 *	@param how -- SHUT_RD to shut down reads, SHUT_WR to shut down writes, SHUT_RDWR for both.
 *	@param flags Future expansion
 *	@see bk_ioh_flush
 */
void bk_ioh_shutdown(bk_s B, struct bk_ioh *ioh, int how, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret;

  if (!ioh || (how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR))
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  bk_debug_printf_and(B, 1, "Starting shutdown %d of IOH %p (in: %d, out: %d)\n", how, ioh, ioh->ioh_fdin, ioh->ioh_fdout);

  if (BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_CLOSING | IOH_FLAGS_SHUTDOWN_DESTROYING))
  {
    bk_error_printf(B, BK_ERR_ERR, "Cannot perform action after close\n");
    BK_VRETURN(B);
  }

  if (how == SHUT_RD || how == SHUT_RDWR)
  {
    if (BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_INPUT | IOH_FLAGS_ERROR_INPUT))
    {
      if (how == SHUT_RD)
	BK_VRETURN(B);				// Already done

      how = SHUT_WR;
    }
  }

  if (how == SHUT_WR || how == SHUT_RDWR)
  {
    if (BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_OUTPUT | IOH_FLAGS_ERROR_OUTPUT | IOH_FLAGS_SHUTDOWN_OUTPUT_PEND))
    {
      if (how == SHUT_WR)
	BK_VRETURN(B);				// Already done

      how = SHUT_RD;
    }
  }

#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISCLEAR(flags, IOH_FLAG_ALREADYLOCKED) && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&ioh->ioh_lock) != 0)
    abort();
  if (BK_GENERAL_FLAG_ISTHREADON(B))
  {
    while (ioh->ioh_incallback && !pthread_equal(ioh->ioh_userid, pthread_self()))
    {
      ioh->ioh_waiting++;
      pthread_cond_wait(&ioh->ioh_cond, &ioh->ioh_lock);
      ioh->ioh_waiting--;
    }
  }
#endif /* BK_USING_PTHREADS */

  if (how == SHUT_RD || how == SHUT_RDWR)
  {
    ioh_sendincomplete_up(B, ioh, BID_FLAG_MESSAGE, 0);
    bk_debug_printf_and(B,4,"Shutting down read on descriptor: %d\n", ioh->ioh_fdin);
    BK_IOH_SHUTDOWN(ioh, SHUT_RD);
  }

  if (how == SHUT_WR || how == SHUT_RDWR)
  {
    if ((ret = ioh_queue(B, &ioh->ioh_writeq, NULL, 0, 0, 0, NULL, 0, IohDataCmdShutdown, NULL, BK_IOH_BYPASSQUEUEFULL)) < 0)
    {
      bk_debug_printf_and(B,4,"Shutting down write on descriptor: %d\n", ioh->ioh_fdout);
      BK_IOH_SHUTDOWN(ioh, SHUT_WR);
    }
  }

  ret = ioh_execute_ifspecial(B, ioh, &ioh->ioh_writeq, 0); // Execute shutdown immediately if queue was empty

  if (ret == 2)
    BK_VRETURN(B);				// IOH was destroyed (and thus unlocked)

  if (ret == -1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Message type %x somehow failed to shutdown\n",ioh->ioh_extflags);
    // Intentionally no return/goto
  }

  // Mark we won't be seeing this any more
  if (how == SHUT_RD || how == SHUT_RDWR)
  {
    BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_INPUT);
  }

  if (how == SHUT_WR || how == SHUT_RDWR)
  {
    if (ret > 0)
    {
      BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_OUTPUT_PEND);
      bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdout, BK_RUN_WANTWRITE, BK_RUN_WANTWRITE, 0);
    }
    else
      BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_OUTPUT);
  }

#if 0
  /*
   * <TODO>ioh_readallowed is pointless here, since it has no effect if
   * SHUTDOWN_INPUT or ERROR_INPUT are set.  Figure out what, if anything,
   * should be done here, or just remove the useless code entirely</TODO>
   */
  if (ioh->ioh_fdin >= 0 && BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_INPUT|IOH_FLAGS_ERROR_INPUT))
  {
    // Clear read
    // bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdin, 0, BK_RUN_WANTREAD, 0);
    bk_ioh_readallowed(B, ioh, 0, IOH_FLAG_ALREADYLOCKED);
  }
#endif

  if (ioh->ioh_fdout >= 0 && BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_OUTPUT|IOH_FLAGS_ERROR_OUTPUT))
    bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdout, 0, BK_RUN_WANTWRITE, 0); // Clear write

  if (BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_CLOSE_PENDING))
  {
    BK_FLAG_CLEAR(ioh->ioh_intflags, IOH_FLAGS_CLOSE_PENDING);
    if (ioh_close(B, ioh, ioh->ioh_deferredclosearg|IOH_FLAG_ALREADYLOCKED))
      BK_VRETURN(B);				// nothing left to unlock
    goto unlockexit;
  }

 unlockexit:
#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISCLEAR(flags, IOH_FLAG_ALREADYLOCKED) && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_VRETURN(B);
}



/**
 * Flush data queued in the ioh.
 *
 * Data may be queued for input or output.
 *
 * THREADS: MT-SAFE (assuming different ioh)
 * THREADS: THREAD-REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state
 *	@param ioh The IOH environment to update
 *	@param how -- SHUT_RD to flush input, SHUT_WR to flush output, SHUT_RDWR for both.
 *	@param flags BK_IOH_FLUSH_NOEXECUTE close telling us not to execute commands
 *	@return <i>-1</i> on call failure or subsystem refusal
 *	@return <br><i>0</i> on success
 */
void bk_ioh_flush(bk_s B, struct bk_ioh *ioh, int how, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  dict_h cmds = NULL;
  int ret = 0;

  if (!ioh || (how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR))
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  bk_debug_printf_and(B, 1, "Starting flush %d of IOH %p\n", how, ioh);

  if (BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_DESTROYING))
  {
    bk_error_printf(B, BK_ERR_ERR, "Cannot perform action after close\n");
    BK_VRETURN(B);
  }

#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISCLEAR(flags, IOH_FLAG_ALREADYLOCKED) && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (how == SHUT_RD || how == SHUT_RDWR)
  {
    ioh_flush_queue(B, ioh, &ioh->ioh_readq, NULL, 0);
  }
  if (how == SHUT_WR || how == SHUT_RDWR)
  {
    ioh_flush_queue(B, ioh, &ioh->ioh_writeq, BK_FLAG_ISCLEAR(flags, BK_IOH_FLUSH_NOEXECUTE)?&cmds:0, 0);
  }

  if (cmds && BK_FLAG_ISCLEAR(flags, BK_IOH_FLUSH_NOEXECUTE))
    ret = ioh_execute_cmds(B, ioh, cmds, 0);

#ifdef BK_USING_PTHREADS
  if (ret != 2 && BK_FLAG_ISCLEAR(flags, IOH_FLAG_ALREADYLOCKED) && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_VRETURN(B);
}



/**
 * Control whether or not new input will be accepted from the system.
 *
 * Note there may be incomplete data on the input queue which will remain pending.
 *
 * Note too that, by keeping count, we allow multiple people to throttle
 * reads and restart them without clobbering each other. Keep in mind that
 * we never permit throttle_cnt to dip below zero.
 *
 * THREADS: MT-SAFE (assuming different ioh)
 * THREADS: THREAD-REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state
 *	@param ioh The IOH environment to update
 *	@param isallowed Zero if no reads desired, non-zero if reads allowed
 *	@param flags Future expansion
 *	@return <i>-1</i> on call failure or subsystem refusal
 *	@return <br><i>old state</i> on success.
 */
int bk_ioh_readallowed(bk_s B, struct bk_ioh *ioh, int isallowed, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int old_state;
  int new_state;

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  if (ioh->ioh_fdin < 0 || BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_INPUT | IOH_FLAGS_ERROR_INPUT | IOH_FLAGS_SHUTDOWN_DESTROYING))
  {
    bk_error_printf(B, BK_ERR_WARN, "Cannot enable/disable read on ioh after"
		    " error or shutdown\n");
    BK_RETURN(B,-1);
  }

#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISCLEAR(flags, IOH_FLAG_ALREADYLOCKED) && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  old_state=bk_run_getpref(B, ioh->ioh_run, ioh->ioh_fdin, 0);
  old_state &= BK_RUN_WANTREAD;

  if (!isallowed)
  {
    // Any throttle should turn off reads.
    new_state = 0;
    ioh->ioh_throttle_cnt++;
  }
  else
  {
    if (ioh->ioh_throttle_cnt > 1)
    {
      // If we're still waiting for people to release keep being throttled.
      new_state = 0;
    }
    else
    {
      // If we're the last to throttle turn on reads.
      new_state = 1;
    }

    ioh->ioh_throttle_cnt--;

    if (ioh->ioh_throttle_cnt < 0)
    {
      bk_error_printf(B, BK_ERR_WARN, "Throttle cnt dipped below zero. Resetting\n");
      ioh->ioh_throttle_cnt = 0;
    }
  }

  bk_debug_printf_and(B, 1, "Setting read state to %d of IOH %p\n", new_state, ioh);

  // Set new preference
  if (bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdin, new_state?BK_RUN_WANTREAD:0, BK_RUN_WANTREAD, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Cannot set I/O preferences\n");
    goto error;
  }

  if (ioh->ioh_throttle_cnt < 1 && ioh->ioh_readq.biq_queuelen > 0 && BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_LINE) && !ioh->ioh_readallowedevent)
  {
    if (bk_run_enqueue_delta(B, ioh->ioh_run, 0, bk_ioh_userdrainevent, ioh, &ioh->ioh_readallowedevent, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not enqueue event to potential drain remaining lines\n");
      ioh->ioh_readallowedevent = NULL;
    }
  }
  else if (ioh->ioh_readallowedevent)
  {
    bk_run_dequeue(B, ioh->ioh_run, ioh->ioh_readallowedevent, BK_RUN_DEQUEUE_EVENT);
    ioh->ioh_readallowedevent = NULL;
  }

#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISCLEAR(flags, IOH_FLAG_ALREADYLOCKED) && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */


  BK_RETURN(B,old_state);

 error:
#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISCLEAR(flags, IOH_FLAG_ALREADYLOCKED) && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B,-1);
}



/**
 * Indicate that no further activity is desired on this ioh.
 *
 * The ioh may linger if data requires draining, unless abort.  Incomplete data
 * pending on the input queue will be sent to user as incomplete unless abort.
 * Additional callbacks may occur--WRITECOMPLETEs or IOHWRITEERRORs (if no
 * abort), WRITEABORTEDs (if abort), IOHCLOSING (if NOTIFYANYWAY)
 *
 * THREADS: MT-SAFE (assuming different ioh)
 * THREADS: THREAD-REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state
 *	@param ioh The IOH environment to update
 *	@param flags BK_IOH_DONTCLOSEFDS to prevent close() from being
 *	executed on the fds, BK_IOH_ABORT to cause automatic flush of input
 *	and output queues (to prevent indefinite wait for output to drain),
 *	BK_IOH_NOTIFYANYWAY to cause user to be notified of this close()).
 */
void bk_ioh_close(bk_s B, struct bk_ioh *ioh, bk_flags flags)
{
  ioh_close(B, ioh, flags);
}



/**
 * Internal close, with status return.
 *
 * This provides the functionality of bk_ioh_close, as above, but has a
 * non-void return so that other ioh routines won't try to use an ioh
 * that has been destroyed.
 *
 * THREADS: MT-SAFE (assuming different ioh)
 * THREADS: THREAD-REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state
 *	@param ioh The IOH environment to update
 *	@param flags BK_IOH_DONTCLOSEFDS to prevent close() from being
 *	executed on the fds, BK_IOH_ABORT to cause automatic flush of input
 *	and output queues (to prevent indefinite wait for output to drain),
 *	BK_IOH_NOTIFYANYWAY to cause user to be notified of this close()).
 *	@return non-zero if ioh does not (no longer) exists, 0 otherwise.
 */
static int ioh_close(bk_s B, struct bk_ioh *ioh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret = 0;					// until ioh destroyed
  dict_h cmds = NULL;

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);				// ioh already nuked, eh?
  }

  bk_debug_printf_and(B, 1, "Closing with flags %x ioh IOH %p/%x\n", flags, ioh, ioh->ioh_intflags);
  if (bk_debug_and(B, 0x10))
  {
    bk_debug_print(B, "Printing stack trace\n");
    bk_fun_trace(B, stderr, BK_ERR_NONE, 0);
  }

#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISCLEAR(flags, IOH_FLAG_ALREADYLOCKED) && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (ioh->ioh_incallback
#ifdef BK_USING_PTHREADS
      || ioh->ioh_waiting
#endif /* BK_USING_PTHREADS */
      )
  {
    BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_CLOSE_PENDING);
    ioh->ioh_deferredclosearg = flags;
    goto unlockexit;
  }

  // Save flags for _destroy()
  if (BK_FLAG_ISSET(flags, BK_IOH_DONTCLOSEFDS))
    BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_DONTCLOSEFDS);

  // Don't do it if we are already doing it
  if ((BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_CLOSING) &&
       BK_FLAG_ISCLEAR(flags, BK_IOH_ABORT)) ||
      BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_DESTROYING))
  {
    bk_error_printf(B, BK_ERR_WARN, "Close already in progress\n");
    goto unlockexit;
  }

  BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_CLOSING);

  if (BK_FLAG_ISSET(flags, BK_IOH_ABORT))
    bk_ioh_flush(B, ioh, SHUT_RDWR, BK_IOH_FLUSH_NOEXECUTE|IOH_FLAG_ALREADYLOCKED);

  ioh_sendincomplete_up(B, ioh, BID_FLAG_MESSAGE, 0);

  BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_INPUT|IOH_FLAGS_SHUTDOWN_OUTPUT_PEND);

  // Queue up a close command after any pending data commands
  if (ioh_queue(B, &ioh->ioh_writeq, NULL, 0, 0, 0, NULL, 0, IohDataCmdClose, NULL, BK_IOH_BYPASSQUEUEFULL) <  0)
  {
    ioh_flush_queue(B, ioh, &ioh->ioh_writeq, NULL, 0);

    // Sometimes a clc makes things easier, sometimes it *really* doesn't...
    if ((cmds = cmd_list_create(NULL, NULL, DICT_UNORDERED)))
    {
      struct ioh_data_cmd *idc;
      if (!(idc = idc_create(B)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not create idc. Command lost\n");
	cmd_list_destroy(cmds);
	cmds = NULL;
      }
      idc->idc_type = IohDataCmdClose;
      if (cmd_list_append(cmds, idc) != DICT_OK)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not insert cmd in list (cmd lost): %s\n", cmd_list_error_reason(cmds,NULL));
	idc_destroy(B,idc);
	cmd_list_destroy(cmds);
	cmds = NULL;
      }
    }
  }

  // Execute close if it is the only thing on the output queue
  if (cmds)
    ret = ioh_execute_cmds(B, ioh, cmds, 0);
  else
    ret = ioh_execute_ifspecial(B, ioh, &ioh->ioh_writeq, 0);

  if (ret == 2)
  {
    // IOH has been destroyed
    BK_RETURN(B, ret);
  }

  // Propagate close to RUN level
  if (ioh->ioh_fdin >= 0 && BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_INPUT|IOH_FLAGS_ERROR_INPUT))
  {
    /*
     * <TODO>ioh_readallowed is probably not wanted here, since it has no
     * effect if SHUTDOWN_INPUT or ERROR_INPUT are set.  figure out what, if
     * anything, should be done here</TODO>
     */
    bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdin, 0, BK_RUN_WANTREAD, 0);
    bk_ioh_readallowed(B, ioh, 0, IOH_FLAG_ALREADYLOCKED);
  }
  if (ioh->ioh_fdout >= 0 && BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_OUTPUT|IOH_FLAGS_ERROR_OUTPUT))
    bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdout, 0, BK_RUN_WANTWRITE, 0); // Clear write

  if (BK_FLAG_ISSET(flags, BK_IOH_ABORT) || ret < 1)
  {
    bk_ioh_destroy(B, ioh);
    BK_RETURN(B, 2);				// Already unlocked (well, destroyed even)
  }
  else if (ret == 1)				// IOH still exists
    ret = 0;					// keep it simple for caller

 unlockexit:
#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISCLEAR(flags, IOH_FLAG_ALREADYLOCKED) && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */
  BK_RETURN(B, ret);
}



/**
 * Indicate that no further activity is desired on this ioh.
 *
 * All queued data will be flushed (use _close instead).
 * User data queued on system probably will not be freed (use _close instead).
 * No user notification (use _close instead).
 * Did we mention you should use _close instead of this interface?
 *
 * <WARNING>
 * jtt made this static since no one outside this file was using it and
 * you're really not supposed to. Seth is dubious; he feels that it just
 * <em>might</em> happen that someone will get in a weird state where they
 * <em>really</em> need to get rid of their ioh without getting called back
 * or anything. So you may make this extern if you like, but it might
 * nice to document the reason why.
 *
 * This policy is now law due to locking.
 * </WARNING>
 *
 * THREADS: REENTRANT (ioh must already be locked)
 *
 *	@param B BAKA thread/global state
 *	@param ioh The IOH environment to update
 *	@param flags See above
 */
static void bk_ioh_destroy(bk_s B, struct bk_ioh *ioh)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  bk_debug_printf_and(B, 1, "Destroying ioh IOH %p\n", ioh);

  if (bk_debug_and(B, 0x10))
  {
    bk_debug_print(B, "Printing stack trace\n");
    bk_fun_trace(B, stderr, BK_ERR_NONE, 0);
  }

  // Prevent recursive activity
  if (BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_DESTROYING))
  {
    bk_error_printf(B, BK_ERR_WARN, "Close already in progress\n");
    BK_VRETURN(B);
  }
  BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_DESTROYING);

  if (ioh->ioh_recheck_event &&
      (bk_run_dequeue(B, ioh->ioh_run, ioh->ioh_recheck_event, 0) < 0))
  {
    bk_error_printf(B, BK_ERR_WARN, "Could not remove follow mode recheck event from event queue\n");
    // Forge on
  }

  BK_SIMPLE_UNLOCK(B, &ioh->ioh_lock);

  /*
   * Bug 7666: Ensure we do not have ioh lock when acquiring bk_run's
   * brf pseudo-lock Do this by ensuring that read and write
   * preferences have been disabled (in bk_ioh_close) and windowing
   * the ioh lock open while we close the run.  This will allow any
   * pending ioh handlers to get through (and be discarded due to the
   * IOH_SHUTDOWN state) while we turn off the run to prevent any
   * others from ever happening.
   */

  // Remove from RUN level
  if (ioh->ioh_fdin >= 0)
    bk_run_close(B, ioh->ioh_run, ioh->ioh_fdin, 0);
  if (ioh->ioh_fdout >= 0 && ioh->ioh_fdout != ioh->ioh_fdin)
    bk_run_close(B, ioh->ioh_run, ioh->ioh_fdout, 0);

  if (ioh->ioh_readallowedevent)
    bk_run_dequeue(B, ioh->ioh_run, ioh->ioh_readallowedevent, BK_RUN_DEQUEUE_EVENT);

  BK_SIMPLE_LOCK(B, &ioh->ioh_lock);

  if (ioh->ioh_readallowedevent)
    ioh->ioh_readallowedevent = NULL;

  bk_ioh_cancel_unregister(B, ioh, BK_FD_ADMIN_FLAG_WANT_ALL);

  if (BK_FLAG_ISCLEAR(ioh->ioh_intflags, IOH_FLAGS_DONTCLOSEFDS))
  {						// Close FDs if we can
    (*ioh->ioh_closefun)(B, ioh, ioh->ioh_iofunopaque, ioh->ioh_fdin, ioh->ioh_fdout, 0);
  }
  else
  {						// Reset FDs to origin states if we cannot
    if (ioh->ioh_fdin >= 0)
    {
      bk_ioh_fdctl(B, ioh->ioh_fdin, &ioh->ioh_fdin_savestate, IOH_FDCTL_RESET);
    }
    if (ioh->ioh_fdout >= 0 && ioh->ioh_fdin != ioh->ioh_fdout)
    {
      bk_ioh_fdctl(B, ioh->ioh_fdout, &ioh->ioh_fdout_savestate, IOH_FDCTL_RESET);
    }
  }

  ioh_flush_queue(B, ioh, &ioh->ioh_readq, NULL, IOH_FLUSH_DESTROY);
  ioh_flush_queue(B, ioh, &ioh->ioh_writeq, NULL, IOH_FLUSH_DESTROY);

  // Notify user
  CALL_BACK(B, ioh, ioh->ioh_iofunopaque, BkIohStatusIohClosing);

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B))
    pthread_mutex_unlock(&ioh->ioh_lock);
  pthread_mutex_destroy(&ioh->ioh_lock);
  pthread_cond_destroy(&ioh->ioh_cond);
#endif /* BK_USING_PTHREADS */

  free(ioh);

  bk_debug_printf_and(B, 1, "IOH %p is now gone\n", ioh);

  BK_VRETURN(B);
}



/**
 * Get length of IOH queues.  Note that this may only have a passing
 * resemblance to the amount of user data that can still be placed in
 * the queue, the amount that was placed in the queue, or any other
 * version thereof.  The message types may compress or expand the
 * number of bytes which get enqueued for transmission.
 *
 * THREADS: MT-SAFE (assuming different ioh)
 * THREADS: THREAD-REENTRANT (otherwise)
 *
 *	@param B BAKA Global/thread state
 *	@param ioh The IOH environment handle
 *	@param inqueue The copy-out size of the input queue
 *	@param outqueue The copy-out size of the output queue
 *	@param flags Fun for the future.
 *	@return <i>-1</i> on call failure
 *	@return <BR><i>0</i> on success
 */
int bk_ioh_getqlen(bk_s B, struct bk_ioh *ioh, u_int32_t *inqueue, u_int32_t *outqueue, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!ioh || (!inqueue && !outqueue))
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, -1);
  }

  bk_debug_printf_and(B, 1, "Getting queue length for IOH %p\n", ioh);

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (inqueue)
    *inqueue = ioh->ioh_readq.biq_queuelen;
  if (outqueue)
    *outqueue = ioh->ioh_writeq.biq_queuelen;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B, 0);
}



/**
 * Run's interface into the IOH.  The callback which it calls when activity
 * was referenced.
 *
 * THREADS: MT-SAFE (assuming different ioh)
 * THREADS: THREAD-REENTRANT (otherwise)
 *
 *	@param B BAKA Thread/global state
 *	@param run Handle into run environment
 *	@param fd File descriptor which had the activity
 *	@param gottypes Description of activity seen
 *	@param opaque The ioh we passed in previously
 *	@param starttime The "current time" of when the select loop
 *		terminated, which may have a casual relationship with the
 *		actual time.  Useful to save system calls when you don't care
 *		that much, or want to avoid starvation.
 */
static void ioh_runhandler(bk_s B, struct bk_run *run, int fd, u_int gottypes, void *opaque, const struct timeval *starttime)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ioh *ioh = opaque;
  char *data = NULL;
  u_int32_t room = 0;
  int ret = 0;
  struct bk_ioh_data *bid;

  if (!opaque)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  bk_debug_printf_and(B, 1, "Received run notification of %x ready for IOH %p/%x (fd: %d)\n", gottypes, ioh, ioh->ioh_intflags, fd);

  // Check for error or exceptional conditions
  if (BK_FLAG_ISSET(gottypes, BK_RUN_DESTROY) || BK_FLAG_ISSET(gottypes, BK_RUN_CLOSE))
  {
    if (BK_FLAG_ISCLEAR(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_DESTROYING))
      bk_ioh_close(B, ioh, BK_IOH_ABORT);
    BK_VRETURN(B);
  }

  // Don't do anything if we are shut down. This is weird and probably shouldn't have happened but whatever...
  if (BK_FLAG_ISSET(gottypes, BK_RUN_READREADY) &&
      BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_INPUT|IOH_FLAGS_ERROR_INPUT))
  {
    BK_FLAG_CLEAR(gottypes, BK_RUN_READREADY);
    bk_run_setpref(B, ioh->ioh_run, fd, 0, BK_RUN_WANTREAD, 0);
  }

  if (BK_FLAG_ISSET(gottypes, BK_RUN_WRITEREADY) &&
      BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_OUTPUT|IOH_FLAGS_ERROR_OUTPUT))
  {
    BK_FLAG_CLEAR(gottypes, BK_RUN_WRITEREADY);
    bk_run_setpref(B, ioh->ioh_run, fd, 0, BK_RUN_WANTWRITE, 0);
  }

  if (BK_FLAG_ISSET(gottypes, BK_RUN_XCPTREADY))
  {
    bk_run_setpref(B, ioh->ioh_run, fd, 0, BK_RUN_WANTXCPT, 0);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&ioh->ioh_lock) != 0)
    abort();

  if (BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_DESTROYING))
    goto unlockexit;

  if (BK_GENERAL_FLAG_ISTHREADON(B))
  {
    while (ioh->ioh_incallback && !pthread_equal(ioh->ioh_userid, pthread_self()))
    {
      ioh->ioh_waiting++;
      pthread_cond_wait(&ioh->ioh_cond, &ioh->ioh_lock);
      ioh->ioh_waiting--;
    }
  }
#endif /* BK_USING_PTHREADS */

  // Write first to hopefully free memory for read if necessary
  if (ret >= 0 && BK_FLAG_ISSET(gottypes, BK_RUN_WRITEREADY))
  {
    if (BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_IN_WRITE))
      goto bypasswrite;
    BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_IN_WRITE);
    if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_RAW))
    {
      ret = ioht_raw_other(B, ioh, BK_RUN_WRITEREADY, IOHT_HANDLER, 0);
    }
    else if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_BLOCKED))
    {
      ret = ioht_block_other(B, ioh, BK_RUN_WRITEREADY, IOHT_HANDLER, 0);
    }
    else if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_VECTORED))
    {
      ret = ioht_vector_other(B, ioh, BK_RUN_WRITEREADY, IOHT_HANDLER, 0);
    }
    else if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_LINE))
    {
      ret = ioht_line_other(B, ioh, BK_RUN_WRITEREADY, IOHT_HANDLER, 0);
    }
    else
    {
      bk_error_printf(B, BK_ERR_ERR, "Unknown message format type %x\n",ioh->ioh_extflags);
    }
    BK_FLAG_CLEAR(ioh->ioh_intflags, IOH_FLAGS_IN_WRITE);
  }
 bypasswrite:

  // Time for reading
  if (ret >= 0 && BK_FLAG_ISSET(gottypes, BK_RUN_READREADY|BK_RUN_USERFLAG1))
  {
    if (BK_FLAG_ISSET(gottypes, BK_RUN_USERFLAG1))
      goto processonly;

    // Ask each algorithm to specify how many bytes it wants
    if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_RAW))
    {
      ret = ioht_raw_other(B, ioh, BK_RUN_READREADY, IOHT_HANDLER, 0);
    }
    else if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_BLOCKED))
    {
      ret = ioht_block_other(B, ioh, BK_RUN_READREADY, IOHT_HANDLER, 0);
    }
    else if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_VECTORED))
    {
      ret = ioht_vector_other(B, ioh, BK_RUN_READREADY, IOHT_HANDLER, 0);
    }
    else if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_LINE))
    {
      ret = ioht_line_other(B, ioh, BK_RUN_READREADY, IOHT_HANDLER, 0);
    }
    else
    {
      bk_error_printf(B, BK_ERR_ERR, "Unknown message format type %x\n",ioh->ioh_extflags);
    }

    if (ret > 0 && ioh_getlastbuf(B, &ioh->ioh_readq, &room, &data, &bid, 0) == 0 && data && room > 0)
    {
      // Get some data
      ret = ioh_internal_read(B, ioh, ioh->ioh_fdin, data, MIN(room, (u_int32_t)ret), 0);

      errno = ioh->ioh_errno;
      if (ret < 0 && IOH_EBLOCKINGINTR)
      {
	// Not ready after all.  Do nothing.
	ret = 0;				// Don't claim we failed
      }
      else if (ret < 0)
      {
	// Error
	ioh_sendincomplete_up(B, ioh, BID_FLAG_MESSAGE, 0);
	CALL_BACK(B, ioh, NULL, BkIohStatusIohReadError);
	BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_ERROR_INPUT);
	bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdin, 0, BK_RUN_WANTREAD, 0); // Clear read from select
      }
      else if (ret == 0)
      {
	// EOF
	ioh_sendincomplete_up(B, ioh, BID_FLAG_MESSAGE, 0);
	CALL_BACK(B, ioh, NULL, BkIohStatusIohReadEOF);
	/*
	 * <TODO>don't set ERROR_INPUT here, since seek could be used to undo
	 * EOF-ness.  use FLAGS_EOF_SEEN instead?</TODO>
	 */
	BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_ERROR_INPUT); // This is a little bogus.....
	bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdin, 0, BK_RUN_WANTREAD, 0); // Clear read from select
      }
      else
      {
	// Got ret bytes
	bid->bid_inuse += ret;
	ioh->ioh_readq.biq_queuelen += ret;

      processonly:
	if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_RAW))
	{
	  ret = ioht_raw_other(B, ioh, ret, IOHT_HANDLER_RMSG, 0);
	}
	else if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_BLOCKED))
	{
	  ret = ioht_block_other(B, ioh, ret, IOHT_HANDLER_RMSG, 0);
	}
	else if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_VECTORED))
	{
	  ret = ioht_vector_other(B, ioh, ret, IOHT_HANDLER_RMSG, 0);
	}
	else if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_LINE))
	{
	  ret = ioht_line_other(B, ioh, ret, IOHT_HANDLER_RMSG, 0);
	}
	else
	{
	  bk_error_printf(B, BK_ERR_ERR, "Unknown message format type %x\n",ioh->ioh_extflags);
	}

	if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_FOLLOW))
	  check_follow(B, ioh, 0);
      }
    }
  }


  if (BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_CLOSE_PENDING))
  {
    BK_FLAG_CLEAR(ioh->ioh_intflags, IOH_FLAGS_CLOSE_PENDING);
    if (ioh_close(B, ioh, ioh->ioh_deferredclosearg|IOH_FLAG_ALREADYLOCKED))
      BK_VRETURN(B);				// nothing left to unlock
    goto unlockexit;
  }

  if (ret >= 0)
    ret = ioh_execute_ifspecial(B, ioh, &ioh->ioh_writeq, 0);


  if (ret < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Message type handler failed severely\n");
    if (ioh_close(B, ioh, BK_IOH_ABORT|IOH_FLAG_ALREADYLOCKED))
      BK_VRETURN(B);				// nothing left to unlock
    goto unlockexit;
  }

 unlockexit:

#ifdef BK_USING_PTHREADS
  if (ret != 2 && BK_GENERAL_FLAG_ISTHREADON(B))
  {
    pthread_cond_broadcast(&ioh->ioh_cond);
    if (pthread_mutex_unlock(&ioh->ioh_lock) != 0)
      abort();
  }
#endif /* BK_USING_PTHREADS */
  BK_VRETURN(B);
}



/**
 * Get or set standard IOH file/network options.  Normally sets
 * O_NONBLOCK (nonblocking), OOBINLINE (urgent data is treated
 * normally), -SOLINGER (shutdown/close do not block).
 *
 * THREADS: MT_SAFE
 *
 *	@param B BAKA Thread/global state
 *	@param fd File descriptor to set or reset
 *	@param savestate Copy-in or copy-out (depending on mode) of
 *			 what options need reset, or were set
 *	@param flags IOH_FDCTL_SET -- set standard fd options.
 *		     IOH_FDCTL_RESET -- reset to original defaults.
 *	@return <i>-1</i> on call failure
 *	@return <br><i>0</i> no changes made
 *	@return <br><i>positive</i> some changes made
 */
static int bk_ioh_fdctl(bk_s B, int fd, u_int32_t *savestate, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret = 0;
  int fdflags;
  int oobinline = -1;
  int linger = -1;
  struct linger sling;
  socklen_t size;

  if (!savestate)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  bk_debug_printf_and(B, 1, "Setting fd policy for fd %d\n", fd);

  if (fd < 0)
  {
    bk_error_printf(B, BK_ERR_NOTICE, "Cannot set fd mode on illegal fd\n");
    BK_RETURN(B,0);
  }

  /*
   * Contrary to Linux manpage, SO_LINGER only affects close(), not shutdown()
   * (see http://www.cs.helsinki.fi/linux/linux-kernel/2001-06/0132.html) and
   * in fact is rarely implemented correctly - most stacks act as if linger is
   * off - and http://httpd.apache.org/docs/misc/perf-tuning.html#compiletime a
   * few pages down describes a more dependable way to get lingering behavior.
   *
   * We knock ourselves out turning it off just in case.  It probably doesn't
   * matter, since this code was using an int instead of struct linger, which
   * hasn't been correct since 4.2BSD.  Incidentally, if you find a 4.2BSD
   * machine (maybe SunOS 3?) on which the struct linger stuff doesn't compile,
   * just comment it out - the cost/benefit ratio of this code is already far
   * too high.
   */

  // Examine file descriptor for proper file and socket options
  fdflags = fcntl(fd, F_GETFL);
  size = sizeof(oobinline);
  if (getsockopt(fd, SOL_SOCKET, SO_OOBINLINE, &oobinline, &size) == 0
      || errno != ENOTSOCK)
  {
    // only bother trying linger if we have a socket
    size = sizeof(sling);
    if (getsockopt(fd, SOL_SOCKET, SO_LINGER, &sling, &size) == 0)
      linger = sling.l_onoff != 0;
  }

  if (BK_FLAG_ISSET(flags,IOH_FDCTL_SET))
  {
    *savestate = 0;

    if (fdflags >= 0 && BK_FLAG_ISCLEAR(fdflags, O_NONBLOCK))
    {
      BK_FLAG_SET(fdflags, O_NONBLOCK);
      *savestate |= IOH_ADDED_NONBLOCK;
    }
    if (oobinline == 0)
    {
      oobinline = 1;
      *savestate |= IOH_ADDED_OOBINLINE;
    }
    if (linger > 0)
    {
      /*
       * Linger was turned on; some OS's may lose the actual linger time when
       * we turn it off, so we have to stash this into savestate.  To make it
       * fit in the upper 16 bits of savestate, we bound it to 2^16-1.
       */
      linger = MIN((int)sling.l_linger, (int)USHRT_MAX);
      sling.l_onoff = 0;
      *savestate |= (linger << 16) | IOH_NUKED_LINGER;
    }
  }

  if (BK_FLAG_ISSET(flags,IOH_FDCTL_RESET))
  {
    if (BK_FLAG_ISSET(*savestate, IOH_ADDED_NONBLOCK) && fdflags >= 0 &&
	BK_FLAG_ISSET(fdflags, O_NONBLOCK))
    {
      BK_FLAG_CLEAR(fdflags, O_NONBLOCK);
    }
    if (BK_FLAG_ISSET(*savestate, IOH_ADDED_OOBINLINE) && oobinline > 0)
    {
      oobinline = 0;
    }
    if (BK_FLAG_ISSET(*savestate, IOH_NUKED_LINGER) && linger == 0)
    {
      sling.l_onoff = 1;
      if (!sling.l_linger)			// appears to have been nuked
	sling.l_linger = (*savestate >> 16) & 0xffff;
    }
  }

  // set modified file and socket options where appropriate
  if (fdflags >= 0 && fcntl(fd, F_SETFL, fdflags) >= 0)
    ret++;
  if (oobinline >= 0 && setsockopt(fd, SOL_SOCKET, SO_OOBINLINE,
				   &oobinline, sizeof(oobinline)) >= 0)
    ret++;
  if (linger >= 0 && setsockopt(fd, SOL_SOCKET, SO_LINGER,
				&sling, sizeof(sling)) >= 0)
    ret++;

  BK_RETURN(B, ret);
}



/**
 * Remove a certain number of bytes from the queue in question
 *
 * THREADS: REENTRANT (ioh must already be locked)
 *
 *	@param B BAKA Thread/global state
 *	@param iohq Input/Output Data Structure
 *	@param bytes Number of bytes to remove
 *	@param flags Fun for the future
 *	@return <i>-1</i> on call failure, allocation failure, CLC failure, etc
 *	@return <BR><i>0</i> on success
 */
static int ioh_dequeue_byte(bk_s B, struct bk_ioh *ioh, struct bk_ioh_queue *iohq, u_int32_t bytes, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ioh_data *bid, *next_bid;
  int origbytes = bytes;


  if (!iohq)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_RETURN(B, -1);
  }

  // Figure out what buffers have been fully written


  for (bid = biq_minimum(iohq->biq_queue);
       bid && (bytes > 0);
       bid = next_bid)
  {
    next_bid = biq_successor(iohq->biq_queue, bid);

    if (!bid->bid_data)
      continue;

    // Allocated but unused items get nuked immediately
    if (!bid->bid_inuse)
    {
      ioh_dequeue(B, ioh, iohq, bid, 0);
      continue;
    }


    if (bytes < bid->bid_inuse)
    {					// Partially written--adjust
      bid->bid_used += bytes;
      bid->bid_inuse -= bytes;
      iohq->biq_queuelen -= bytes;
      break;
    }

    // Buffer fully written
    bytes -= bid->bid_inuse;
    ioh_dequeue(B, ioh, iohq, bid, 0);
  }

  bk_debug_printf_and(B, 1, "Dequeueing %d bytes (now %d) for IOH queue %p\n", origbytes, iohq->biq_queuelen, iohq);

  BK_RETURN(B, 0);
}



/**
 * Remove and destroy a bid from a queue
 *
 * THREADS: REENTRANT (ioh must already be locked)
 *
 *	@param B BAKA Thread/global state
 *	@param iohq Input/Output Data Structure
 *	@param bid The data to delete
 *	@param flags Fun for the future
 *	@return <i>-1</i> on call failure, allocation failure, CLC failure, etc
 *	@return <BR><i>0</i> on success
 */
static int ioh_dequeue(bk_s B, struct bk_ioh *ioh, struct bk_ioh_queue *iohq, struct bk_ioh_data *bid, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!iohq || !bid)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_RETURN(B, -1);
  }

  bk_debug_printf_and(B, 1, "Dequeuing bid %p data %p/%d/%d for IOH queue %p\n", bid, bid->bid_data, bid->bid_inuse, bid->bid_allocated, iohq);

  if (biq_delete(iohq->biq_queue, bid) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not find bid %p to delete from IOH queue %p: %s\n", bid, iohq, biq_error_reason(iohq->biq_queue, NULL));
  }
  else
  {
    iohq->biq_queuelen -= bid->bid_inuse;
  }

  if (bid->bid_vptr && ioh)
  {						// Either give the data back to the user to free
    CALL_BACK(B, ioh, bid->bid_vptr, BK_FLAG_ISSET(flags, IOH_DEQUEUE_ABORT)?BkIohStatusWriteAborted:BkIohStatusWriteComplete);
  }
  else
  {						// Or free the data yourself
    if (bid->bid_data)
      free(bid->bid_data);
  }
  free(bid);

  BK_RETURN(B, 0);
}



/**
 * Insert data into an I/O queue.
 *
 * THREADS: REENTRANT (must already be locked)
 *
 *	@param B BAKA Thread/global state
 *	@param iohq Input/Output data structure
 *	@param data The data to store
 *	@param allocated Size of memory allocated for data
 *	@param inuse Amount of data used but not yet consumed
 *	@param used Amount of data consumed
 *	@param vptr Saved user buffer to be returned at some future point
 *	@param msgflags Special commands from IOH to itself about things to do in the future
 *	@param flags BYPASSQUEUEFULL to bypass queue full check (stuff it in anyway)
 *	@return <i>-1</i> on call failure, allocation failure, CLC failure, etc
 *	@return <BR><i>0</i> on success
 *	@return <BR><i>1</i> on queue-too-full
 */
static int ioh_queue(bk_s B, struct bk_ioh_queue *iohq, char *data, u_int32_t allocated, u_int32_t inuse, u_int32_t used, bk_vptr *vptr, bk_flags msgflags, ioh_data_cmd_type_e cmd, void *cmd_args, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ioh_data *bid;

  if (!iohq || ((!data || !allocated) && cmd == IohDataCmdNone))
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  bk_debug_printf_and(B, 1, "Enqueuing data %p/%d/%d or flags %x for IOH queue %p(%d)\n", data, inuse, allocated, msgflags, iohq, iohq->biq_queuelen);

  /*
   * Refuse to queue the message if all 3 of the following conditions exist:
   * 1) The queue already has something in it.
   * 2) The new data would exceed the queue max.
   * 3) BK_IOH_BYPASSQUEUEFULL is *not* in effect.
   */
  if (BK_FLAG_ISCLEAR(flags, BK_IOH_BYPASSQUEUEFULL) && iohq->biq_queuelen)
  {
    if (iohq->biq_queuemax && (inuse + iohq->biq_queuelen > iohq->biq_queuemax))
    {
      bk_debug_printf_and(B, 1, "IOH queue filling up\n");
      bk_error_printf(B, BK_ERR_NOTICE, "IOH queue %p has filled up (%d + %d > %d)\n", iohq, inuse, iohq->biq_queuelen, iohq->biq_queuemax);
      BK_RETURN(B,1);
    }
  }

  if (!BK_CALLOC(bid))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate data management structure: %s\n", strerror(errno));
    BK_RETURN(B,-1);
  }

  bid->bid_data = data;
  bid->bid_allocated = allocated;
  bid->bid_inuse = inuse;
  bid->bid_used = used;
  bid->bid_vptr = vptr;
  bid->bid_flags = msgflags;
  bid->bid_idc.idc_type = cmd;
  bid->bid_idc.idc_args = cmd_args;


  // Put data on queue
  if (biq_append(iohq->biq_queue, bid) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert data management structure into IOH queue: %s\n", biq_error_reason(iohq->biq_queue, NULL));
    goto error;
  }

  // Update total queue size
  iohq->biq_queuelen += bid->bid_inuse;

  BK_RETURN(B, 0);

 error:
  if (bid)
  {
    biq_delete(iohq->biq_queue, bid);
    free(bid);
  }
  BK_RETURN(B, -1);
}



/**
 * Raw--no particular messaging format--IOH Type routines to queue data sent from user for output
 *
 * THREADS: REENTRANT (must already be locked)
 *
 *	@param B BAKA Thread/Global state
 *	@param ioh The IOH environment handle
 *	@param data Vectored data from user
 *	@param flags BK_IOH_BYPASSQUEUEFULL--don't worry about the queue being too full
 *	@return <i>-1</i> Call failure, allocation failure, CLC failure, other failure
 *	@return <br><i>0</i> Success
 *	@return <br><i>1</i> Queue too full to hold this data
 */
static int ioht_raw_queue(bk_s B, struct bk_ioh *ioh, bk_vptr *data, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret;

  if (!ioh || !data)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  bk_debug_printf_and(B, 1, "Raw enqueuing data %p/%d for IOH %p\n", data->ptr, data->len, ioh);

  if ((ret = ioh_queue(B, &ioh->ioh_writeq, data->ptr, data->len, data->len, 0, data, BID_FLAG_MESSAGE, IohDataCmdNone, NULL, flags)) == 0)
  {
    bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdout, BK_RUN_WANTWRITE, BK_RUN_WANTWRITE, 0);
  }

  BK_RETURN(B, ret);
}



/**
 * Complete full block--IOH Type routines to queue data sent from user for output
 *
 * THREADS: REENTRANT (must already be locked)
 *
 *	@param B BAKA Thread/Global state
 *	@param ioh The IOH environment handle
 *	@param data Vectored data from user
 *	@param flags BK_IOH_BYPASSQUEUEFULL--don't worry about the queue being too full
 *	@return <i>-1</i> Call failure, allocation failure, CLC failure, other failure
 *	@return <br><i>0</i> Success
 *	@return <br><i>1</i> Queue too full to hold this data
 */
static int ioht_block_queue(bk_s B, struct bk_ioh *ioh, bk_vptr *data, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret;

  if (!ioh || !data)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  bk_debug_printf_and(B, 1, "Block enqueuing data %p/%d for IOH %p\n", data->ptr, data->len, ioh);

  if ((ret = ioh_queue(B, &ioh->ioh_writeq, data->ptr, data->len, data->len, 0, data, BID_FLAG_MESSAGE, IohDataCmdNone, NULL, flags)) == 0)
  {
    /*
     * Yes, there may be blocks already partially sent, but if this condition
     * is true, then we are guaranteed that there is at least one block ready.
     * This block may be the partially sent block, and in that case write should
     * already be set, and this will do no harm.
     */
    if (ioh->ioh_writeq.biq_queuelen >= ioh->ioh_inbuf_hint)
      bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdout, BK_RUN_WANTWRITE, BK_RUN_WANTWRITE, 0);
  }

  BK_RETURN(B, ret);
}



/**
 * Vectored (size sent before data)--IOH Type routines to queue data sent from user for output
 *
 * THREADS: REENTRANT (must already be locked)
 *
 *	@param B BAKA Thread/Global state
 *	@param ioh The IOH environment handle
 *	@param data Vectored data from user
 *	@param flags BK_IOH_BYPASSQUEUEFULL--don't worry about the queue being too full
 *	@return <i>-1</i> Call failure, allocation failure, CLC failure, other failure
 *	@return <br><i>0</i> Success
 *	@return <br><i>1</i> Queue too full to hold this data
 */
static int ioht_vector_queue(bk_s B, struct bk_ioh *ioh, bk_vptr *data, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  u_int32_t *netdatalen;

  if (!ioh || !data)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  bk_debug_printf_and(B, 1, "Vector enqueuing data %p/%d for IOH %p\n", data->ptr, data->len, ioh);

  // Do our own checks for queue size since we have two buffers which either both have to be on, or both off.
  // See comments in ioh_write for an explanation of what's going on here
  if (BK_FLAG_ISCLEAR(flags, BK_IOH_BYPASSQUEUEFULL) && ioh->ioh_writeq.biq_queuelen)
  {
    if (ioh->ioh_writeq.biq_queuemax && (sizeof(u_int32_t) + data->len + ioh->ioh_writeq.biq_queuelen > ioh->ioh_writeq.biq_queuemax))
    {
      bk_error_printf(B, BK_ERR_NOTICE, "IOH queue %p has filled up\n", &ioh->ioh_writeq);
      BK_RETURN(B,1);
    }
  }

  // Create the vector pointer and insert it on the output queue
  if (!BK_CALLOC(netdatalen))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate vector data length: %s\n", strerror(errno));
    goto error;
  }
  *netdatalen = htonl(data->len);
  if (ioh_queue(B, &ioh->ioh_writeq, (char *)netdatalen, sizeof(*netdatalen), sizeof(*netdatalen), 0, NULL, 0, IohDataCmdNone, NULL, BK_IOH_BYPASSQUEUEFULL) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert vector length onto output stack\n");
    goto error;
  }

  if (ioh_queue(B, &ioh->ioh_writeq, data->ptr, data->len, data->len, 0, data, BID_FLAG_MESSAGE, IohDataCmdNone, NULL, BK_IOH_BYPASSQUEUEFULL) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert post-vector data onto output stack--now we are in trouble\n");
    goto error;
  }

  bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdout, BK_RUN_WANTWRITE, BK_RUN_WANTWRITE, 0);

  BK_RETURN(B, 0);

 error:
  if (netdatalen)
  {
    struct bk_ioh_data *bid;

    // Find the possibly not-inserted vector length and dequeue it
    for (bid=biq_maximum(ioh->ioh_writeq.biq_queue); bid; bid = biq_predecessor(ioh->ioh_writeq.biq_queue, bid))
    {
      if (bid->bid_data == (char *)netdatalen)
      {
	ioh_dequeue(B, ioh, &ioh->ioh_writeq, bid, 0);
	break;
      }
    }
    free(netdatalen);
  }
  BK_RETURN(B, -1);
}



/**
 * Line oriented messaging format--IOH Type routines to queue data sent from user for output.
 * Note that we do not enforce line message boundaries on output.
 *
 * <TODO>Perhaps we *should* do "enforcement" in order to achieve buffering.  Worry about UDP case as well.</TODO>
 *
 * THREADS: REENTRANT (must already be locked)
 *
 *	@param B BAKA Thread/Global state
 *	@param ioh The IOH environment handle
 *	@param data Vectored data from user
 *	@param flags BK_IOH_BYPASSQUEUEFULL--don't worry about the queue being too full
 *	@return <i>-1</i> Call failure, allocation failure, CLC failure, other failure
 *	@return <br><i>0</i> Success
 *	@return <br><i>1</i> Queue too full to hold this data
 */
static int ioht_line_queue(bk_s B, struct bk_ioh *ioh, bk_vptr *data, bk_flags flags)
{
  return(ioht_raw_queue(B, ioh, data, flags));
}



/**
 * Raw--no particular messaging format--IOH Type routines to perform I/O maintenance and activity
 *
 * THREADS: REENTRANT (ioh must already be locked)
 *
 *	@param B BAKA Thread/Global state
 *	@param ioh The IOH environment handle
 *	@param aux Auxiliary information for the command
 *	@param cmd Command we are supposed to perform
 *	@param flags Fun for the future
 *	@return <i>-1</i> Call failure, allocation failure, CLC failure, other failure
 *	@return <br><i>0</i> Success & queue empty
 *	@return <br><i>1</i> Success & queue non-empty
 */
static int ioht_raw_other(bk_s B, struct bk_ioh *ioh, u_int aux, u_int cmd, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret = 0;
  struct bk_ioh_data *bid;
  u_int32_t size = 0;

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  bk_debug_printf_and(B, 1, "Raw other cmd %d/%d for IOH %p\n", cmd, aux, ioh);

  switch (cmd)
  {
  case IOHT_FLUSH:
    // Clean any algorithm private data, or nuke it if ABORT
    break;

  case IOHT_HANDLER:
    if (aux == BK_RUN_WRITEREADY)
    {
      // find first non-cmd bid
      bid = biq_minimum(ioh->ioh_writeq.biq_queue);
      while (bid && !bid->bid_data)
      {
	bid = biq_successor(ioh->ioh_writeq.biq_queue, bid);
      }

      // process up to the next cmd bid <TODO>Make message boundary aware?</TODO>
      while(bid && bid->bid_data)
      {
	int cnt = 0;
	int vectors_in_use;
	struct iovec *iov;

	while (bid && bid->bid_data)
	{
	  cnt++;
	  if (BK_FLAG_ISCLEAR(ioh->ioh_extflags, BK_IOH_WRITE_ALL))
	    break;
	  bid = biq_successor(ioh->ioh_writeq.biq_queue, bid);
	}

	if (!BK_MALLOC_LEN(iov, sizeof(*iov)*(cnt)))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not allocate writev iovec\n");
	  BK_RETURN(B, -1);
	}

	// fill out iovec with ptrs from user buffers
	for (vectors_in_use = 0, bid = biq_minimum(ioh->ioh_writeq.biq_queue);
	     vectors_in_use < cnt && bid && bid->bid_data;
	     bid = biq_successor(ioh->ioh_writeq.biq_queue, bid))
	{
	  iov[vectors_in_use].iov_base = bid->bid_data + bid->bid_used;
	  iov[vectors_in_use].iov_len = bid->bid_inuse;
	  if (bk_debug_and(B, 0x20))
	  {
	    bk_vptr dbuf;
	    dbuf.ptr = iov[vectors_in_use].iov_base;
	    dbuf.len = MIN((u_int)iov[vectors_in_use].iov_len, 128U);
	    bk_debug_printbuf_and(B, 0x20, "Buffer ready to write:", "\t", &dbuf);
	  }
	  vectors_in_use++;
	}
	bk_debug_printf_and(B, 64, "Stopped enqueueing data, vectors %d, bid %p, bid->data %p, bid->flags %x, bid->idc_type %d\n", vectors_in_use, bid, bid?bid->bid_data:NULL, bid?bid->bid_flags:0, bid?bid->bid_idc.idc_type:0);

#ifdef BK_USING_PTHREADS
	ioh->ioh_incallback++;
	if (BK_GENERAL_FLAG_ISTHREADON(B))
	{
	  ioh->ioh_userid = pthread_self();
	  if (pthread_mutex_unlock(&ioh->ioh_lock) != 0)
	    abort();
	}
#endif /* BK_USING_PTHREADS */

	cnt = compress_write(B, ioh, ioh->ioh_writefun, ioh->ioh_iofunopaque, ioh->ioh_fdout, iov, vectors_in_use, 0);
	ioh->ioh_errno = errno;

#ifdef BK_USING_PTHREADS
	if (BK_GENERAL_FLAG_ISTHREADON(B))
	{
	  if (pthread_mutex_lock(&ioh->ioh_lock) != 0)
	    abort();
	  BK_ZERO(&ioh->ioh_userid);
	  pthread_cond_broadcast(&ioh->ioh_cond);
	}
	ioh->ioh_incallback--;
#endif /* BK_USING_PTHREADS */

	free(iov);

	errno = ioh->ioh_errno;
	if (cnt == 0 || (cnt < 0 && IOH_EBLOCKINGINTR))
	{
	  // Not quite ready for writing yet
	  cnt = 0;
	  ret = 1;
	  break;
	}
	else if (cnt < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Write (in raw mode) failed\n");
	  BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_ERROR_OUTPUT);
	  ioh_flush_queue(B, ioh, &ioh->ioh_writeq, NULL, 0);
	  CALL_BACK(B, ioh, NULL, BkIohStatusIohWriteError);
	  break;
	}
	else
	{
	  // figure out what buffers have been fully written
	  ioh_dequeue_byte(B, ioh, &ioh->ioh_writeq, (u_int32_t)cnt, 0);

	  if (ioh->ioh_writeq.biq_queuelen < 1)
	  {
	    ioh->ioh_writeq.biq_queuelen = 0;
	    bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdout, 0, BK_RUN_WANTWRITE, 0);
	  }

	  if (BK_FLAG_ISCLEAR(ioh->ioh_extflags, BK_IOH_WRITE_ALL))
	    break;

	  bid = biq_minimum(ioh->ioh_writeq.biq_queue);
	}
      }
    }
    if (aux == BK_RUN_READREADY)
    {						// Return the number of bytes to read
      char *data;

      if (ioh_getlastbuf(B, &ioh->ioh_readq, &size, NULL, NULL, 0) != 0 || size < 1)
      {
	size = ioh->ioh_inbuf_hint;

	if (!(data = malloc(size)))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not allocate input buffer for ioh %p of size %u\n", ioh, size);
	  BK_RETURN(B, -1);
	}

	if (ioh_queue(B, &ioh->ioh_readq, data, size, 0, 0, NULL, BID_FLAG_MESSAGE, IohDataCmdNone, NULL, BK_IOH_BYPASSQUEUEFULL) < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not insert input buffer for ioh %p input queue: %s\n",ioh,biq_error_reason(ioh->ioh_writeq.biq_queue, NULL));
	  free(data);
	  BK_RETURN(B, -1);
	}
      }
    }

    BK_RETURN(B, size);
    break;

  case IOHT_HANDLER_RMSG:
    if (aux > 0)
    {						// Determine if we have msg or are over limit
      int cnt = 0;
      bk_vptr *sendup;
      struct bk_ioh_data *bid_cache = NULL;

      // Find out how many data segments we have
      for (bid_cache = (bid = biq_minimum(ioh->ioh_readq.biq_queue));
	   bid;
	   bid = biq_successor(ioh->ioh_readq.biq_queue, bid))
      {
	if (bid->bid_data && bid->bid_inuse > 0)
	  cnt++;
      }

      if (!cnt)
	BK_RETURN(B,0);

      if (!BK_CALLOC_LEN(sendup,sizeof(*sendup)*(cnt+1)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not allocate data vectors to return data: %s\n", strerror(errno));
	BK_RETURN(B,-1);
      }

      // Optimize everything for the single bid case.
      if (cnt == 1)
      {
	sendup[0].ptr = bid_cache->bid_data+bid_cache->bid_used;
	sendup[0].len = bid_cache->bid_inuse;
      }
      else
      {
	// Actually fill out the data list
	cnt = 0;
	for (bid = biq_minimum(ioh->ioh_readq.biq_queue);
	     bid;
	     bid = biq_successor(ioh->ioh_readq.biq_queue, bid))
	{
	  if (bid->bid_data)
	  {
	    sendup[cnt].ptr = bid->bid_data+bid->bid_used;
	    sendup[cnt].len = bid->bid_inuse;
	    cnt++;
	    bid_cache = bid; // Cache this bid (used when only *one* bid involved in sendup).
	  }
	}
      }

      CALL_BACK(B, ioh, sendup, BkIohStatusReadComplete);

      // Check if the user seized the data (ie we don't have to free it)
      if (cnt == 1)
      {
	/*
	 * This is the common case where we've sent up exactly one
	 * buffer. We have optimized this case by caching the bid so that
	 * we can NULL it immediatly if we need to.
	 */
	if (!sendup[0].ptr)
	  bid_cache->bid_data = NULL;
      }
      else
      {
	// Otherwise we have to iterate through the whole list.
	cnt = 0;
	for (bid = biq_minimum(ioh->ioh_readq.biq_queue);
	     bid;
	     bid = biq_successor(ioh->ioh_readq.biq_queue, bid))
	{
	  if (bid->bid_data && !sendup[cnt++].ptr)
	    bid->bid_data=NULL;
	}
      }

      // Nuke vector list
      free(sendup);

      // Nuke everything in the input queue, we have "used" the necessary stuff
      bk_ioh_flush(B, ioh, SHUT_RD, IOH_FLAG_ALREADYLOCKED);
    }
    break;

  default:
    bk_error_printf(B, BK_ERR_ERR, "Unknown command %d/%x\n",cmd,aux);
    BK_RETURN(B,-1);
  }

  BK_RETURN(B, ret);
}



/**
 * Blocked--fixed length messages--IOH Type routines to perform I/O maintenance and activity
 *
 * THREADS: REENTRANT (ioh must already be locked)
 *
 *	@param B BAKA Thread/Global state
 *	@param ioh The IOH environment handle
 *	@param aux Auxiliary information for the command
 *	@param cmd Command we are supposed to perform
 *	@param flags Fun for the future
 *	@return <i>-1</i> Call failure, allocation failure, CLC failure, other failure
 *	@return <br><i>0</i> Success & queue empty
 *	@return <br><i>1</i> Success & queue non-empty
 */
static int ioht_block_other(bk_s B, struct bk_ioh *ioh, u_int aux, u_int cmd, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret = 0;
  struct bk_ioh_data *bid;
  u_int32_t size = 0;

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  bk_debug_printf_and(B, 1, "Block other cmd %d/%d for IOH %p\n", cmd, aux, ioh);

  switch (cmd)
  {
  case IOHT_FLUSH:
    // Clean any algorithm private data, or nuke it if ABORT
    memset(&ioh->ioh_writeq.biq.block, 0, sizeof(ioh->ioh_writeq.biq.block));
    break;

  case IOHT_HANDLER:
    if (aux == BK_RUN_WRITEREADY)
    {
      int cnt = 0;
      struct iovec *iov;

      // On shutdown-pending, just write everything out
      if (ioh->ioh_writeq.biq.block.remaining < 1 && BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_OUTPUT_PEND))
	ioh->ioh_writeq.biq.block.remaining = MIN(ioh->ioh_writeq.biq_queuelen,ioh->ioh_inbuf_hint);

      // If we are at start-of-block
      if (!ioh->ioh_writeq.biq.block.remaining)
	{
	  if (ioh->ioh_writeq.biq_queuelen >= ioh->ioh_inbuf_hint)
	    ioh->ioh_writeq.biq.block.remaining = ioh->ioh_inbuf_hint;
	  else
	  {
	    bk_error_printf(B, BK_ERR_NOTICE, "Block write handler called without a full block to output\n");
	    bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdout, 0, BK_RUN_WANTWRITE, 0);
	    break;
	  }
	}

      cnt = 0;
      size = 0;
      for (bid = biq_minimum(ioh->ioh_writeq.biq_queue); bid; bid = biq_successor(ioh->ioh_writeq.biq_queue, bid))
      {
	if (bid->bid_data)
	{
	  cnt++;
	  size += bid->bid_inuse;
	  if (size >= ioh->ioh_writeq.biq.block.remaining)
	    break;
	}
      }

      if (!bid || cnt < 1)
      {
	bk_error_printf(B, BK_ERR_ERR, "Block write handler called with inconsistent bid_inuse, biq.block.remaining, and biq_queuelen\n");
	if (cnt < 1)
	{
	  bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdout, 0, BK_RUN_WANTWRITE, 0);
	  BK_RETURN(B,-1);
	}
      }

      if (!BK_MALLOC_LEN(iov,sizeof(*iov)*(cnt)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not allocate data vectors to write data: %s\n", strerror(errno));
	BK_RETURN(B,-1);
      }

      // Fill out iovec with ptrs from user buffers
      cnt = 0;
      size = 0;
      for (bid = biq_minimum(ioh->ioh_writeq.biq_queue); bid; bid = biq_successor(ioh->ioh_writeq.biq_queue, bid))
      {
	if (bid->bid_data)
	{
	  iov[cnt].iov_base = bid->bid_data + bid->bid_used;
	  iov[cnt].iov_len = MIN(bid->bid_inuse, ioh->ioh_writeq.biq.block.remaining - size);
	  size += iov[cnt].iov_len;
	  cnt++;
	  if (size >= ioh->ioh_writeq.biq.block.remaining)
	    break;
	}
      }

#ifdef BK_USING_PTHREADS
      ioh->ioh_incallback++;
      if (BK_GENERAL_FLAG_ISTHREADON(B))
      {
	ioh->ioh_userid = pthread_self();
	if (pthread_mutex_unlock(&ioh->ioh_lock) != 0)
	  abort();
      }
#endif /* BK_USING_PTHREADS */

      cnt = compress_write(B, ioh, ioh->ioh_writefun, ioh->ioh_iofunopaque, ioh->ioh_fdout, iov, cnt, 0);
      ioh->ioh_errno = errno;

#ifdef BK_USING_PTHREADS
      if (BK_GENERAL_FLAG_ISTHREADON(B))
      {
	if (pthread_mutex_lock(&ioh->ioh_lock) != 0)
	  abort();
	BK_ZERO(&ioh->ioh_userid);
	pthread_cond_broadcast(&ioh->ioh_cond);
      }
      ioh->ioh_incallback--;
#endif /* BK_USING_PTHREADS */

      free(iov);

      bk_debug_printf_and(B, 2, "Post-write, cnt %d, size %d\n", cnt, size);

      errno = ioh->ioh_errno;
      if (cnt == 0 || (cnt < 0 && IOH_EBLOCKINGINTR))
      {
	// Not quite ready for writing yet
	cnt = 0;
	break;
      }
      else if (cnt < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Write (in block mode) failed\n");
	BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_ERROR_OUTPUT);
	ioh_flush_queue(B, ioh, &ioh->ioh_writeq, NULL, 0);
	CALL_BACK(B, ioh, NULL, BkIohStatusIohWriteError);
      }
      else
      {
	// Figure out what buffers have been fully written
	ioh_dequeue_byte(B, ioh, &ioh->ioh_writeq, (u_int32_t)cnt, 0);

	// Figure out if we have more block data (or partial block data) to write out
	ioh->ioh_writeq.biq.block.remaining -= cnt;
	if (ioh->ioh_writeq.biq.block.remaining < 1 && ioh->ioh_writeq.biq_queuelen < ioh->ioh_inbuf_hint && BK_FLAG_ISCLEAR(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_OUTPUT_PEND))
	{					// Nothing more to do
	  ioh->ioh_writeq.biq.block.remaining = 0;
	  bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdout, 0, BK_RUN_WANTWRITE, 0);
	}
	if (ioh->ioh_writeq.biq.block.remaining < 1 && ioh->ioh_writeq.biq_queuelen >= ioh->ioh_inbuf_hint)
	{					// More than a block's data left to go
	  ioh->ioh_writeq.biq.block.remaining = ioh->ioh_inbuf_hint;
	}
      }
      bk_debug_printf_and(B, 2, "Post-write, size is %d\n", size);
    }

    if (aux == BK_RUN_READREADY)
    {						// Return the number of bytes to read
      char *data;

      if (ioh_getlastbuf(B, &ioh->ioh_readq, &size, NULL, NULL, 0) != 0 || size < 1)
      {
	size = ioh->ioh_inbuf_hint;

	if (!(data = malloc(size)))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not allocate input buffer for ioh %p of size %d: %s\n",ioh,size,strerror(errno));
	  BK_RETURN(B, -1);
	}

	if (ioh_queue(B, &ioh->ioh_readq, data, size, 0, 0, NULL, BID_FLAG_MESSAGE, IohDataCmdNone, NULL, BK_IOH_BYPASSQUEUEFULL) < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not insert input buffer for ioh %p input queue: %s\n",ioh,biq_error_reason(ioh->ioh_writeq.biq_queue, NULL));
	  free(data);
	  BK_RETURN(B, -1);
	}
      }
    }
    bk_debug_printf_and(B, 2, "Block other cmd returning %d\n", size);
    BK_RETURN(B, size);
    break;

  case IOHT_HANDLER_RMSG:
    if (aux > 0)
    {						// Determine if we have msg or are over limit
      int cnt = 0;
      bk_vptr *sendup;
      struct bk_ioh_data *bid_cache = NULL;

      if (ioh->ioh_readq.biq_queuelen >= ioh->ioh_inbuf_hint)
      {						// We have enough data--get ready to send up

	size = cnt = 0;
	for (bid_cache = (bid = biq_minimum(ioh->ioh_readq.biq_queue));
	     bid && (size < ioh->ioh_inbuf_hint);
	     bid = biq_successor(ioh->ioh_readq.biq_queue, bid))
	{
	  if (bid->bid_data && bid->bid_inuse > 0)
	  {
	    cnt++;
	    size += bid->bid_inuse;
	  }
	}

	if (!cnt || size < ioh->ioh_inbuf_hint)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Inconsistency between ioh->ioh_readq.biq_queuelen (%d) and the data in the queue (%d) with hint %d\n",ioh->ioh_readq.biq_queuelen, size, ioh->ioh_inbuf_hint);
	  BK_RETURN(B,-1);
	}

	if (!BK_CALLOC_LEN(sendup,sizeof(*sendup)*(cnt+1)))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not allocate data vectors to return data: %s\n", strerror(errno));
	  BK_RETURN(B,-1);
	}

	// Actually fill out the data list
	if (cnt == 1)
	{
	  sendup[0].ptr = bid_cache->bid_data+bid_cache->bid_used;
	  sendup[0].len = MIN(bid_cache->bid_inuse,ioh->ioh_inbuf_hint);
	}
	else
	{
	  size = cnt = 0;
	  for (bid = biq_minimum(ioh->ioh_readq.biq_queue);
	       bid && (size < ioh->ioh_inbuf_hint);
	       bid = biq_successor(ioh->ioh_readq.biq_queue, bid))
	  {
	    if (bid->bid_data && bid->bid_inuse > 0)
	    {
	      sendup[cnt].ptr = bid->bid_data+bid->bid_used;
	      sendup[cnt].len = MIN(bid->bid_inuse,ioh->ioh_inbuf_hint - size);
	      size += sendup[cnt].len;
	      cnt++;
	    }
	  }
	}

	CALL_BACK(B, ioh, sendup, BkIohStatusReadComplete);

	// Check if the user seized the data (ie we don't have to free it)
	if (cnt == 1)
	{
	  /*
	   * This is the common case where we've sent up exactly one
	   * buffer. We have optimized this case by caching the bid so that
	   * we can NULL it immediatly if we need to.
	   */
	  if (!sendup[0].ptr)
	    bid_cache->bid_data = NULL;
	}
	else
	{
	  // Otherwise we have to iterate through the whole list.
	  cnt = 0;
	  for (bid = biq_minimum(ioh->ioh_readq.biq_queue);
	       bid;
	       bid = biq_successor(ioh->ioh_readq.biq_queue, bid))
	  {
	    if (bid->bid_data && !sendup[cnt++].ptr)
	      bid->bid_data=NULL;
	  }
	}

	// Nuke vector list
	free(sendup);

	// Delete buffers that have been used
	ioh_dequeue_byte(B, ioh, &ioh->ioh_readq, (u_int32_t)size, 0);
      }
    }
    break;

  default:
    bk_error_printf(B, BK_ERR_ERR, "Unknown command %d/%x\n",cmd,aux);
    BK_RETURN(B,-1);
  }

  bk_debug_printf_and(B, 2, "Block other cmd returning end %d\n", ret);
  BK_RETURN(B, ret);
}



/**
 * Vectored--length encoded messaging format--IOH Type routines to perform I/O maintenance and activity
 *
 * THREADS: REENTRANT (ioh must already be locked)
 *
 *	@param B BAKA Thread/Global state
 *	@param ioh The IOH environment handle
 *	@param aux Auxiliary information for the command
 *	@param cmd Command we are supposed to perform
 *	@param flags Fun for the future
 *	@return <i>-1</i> Call failure, allocation failure, CLC failure, other failure
 *	@return <br><i>0</i> Success & queue empty
 *	@return <br><i>1</i> Success & queue non-empty
 */
static int ioht_vector_other(bk_s B, struct bk_ioh *ioh, u_int aux, u_int cmd, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret = 0;
  struct bk_ioh_data *bid;
  u_int32_t room =0, size = 0;
  u_int32_t lengthfromwire = 0;
  int cnt = 0;
  struct bk_ioh_data *bid_cache = NULL;

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  bk_debug_printf_and(B, 1, "Vectored other cmd %d/%d for IOH %p\n", cmd, aux, ioh);


  // Subroutines
  if ((cmd == IOHT_HANDLER && aux == BK_RUN_READREADY) || (cmd == IOHT_HANDLER_RMSG))
  {						// Return the number of bytes to read
    // Determine how many bytes we have ready, and what the length of data queued up is
    for (bid = biq_minimum(ioh->ioh_readq.biq_queue); bid; bid=biq_successor(ioh->ioh_readq.biq_queue, bid))
    {
      if (!bid->bid_data || bid->bid_inuse < 1)
	continue;				// Nothing to see, move along

      if (size < sizeof(lengthfromwire))
      {
	memcpy((char *)&lengthfromwire, bid->bid_data + bid->bid_used, MIN(sizeof(lengthfromwire) - size, bid->bid_inuse));
      }

      size += bid->bid_inuse;
      cnt++;
    }
    if (size >= sizeof(lengthfromwire))
      lengthfromwire = ntohl(lengthfromwire);
    else
      lengthfromwire = 0;

    assert(size == ioh->ioh_readq.biq_queuelen);

    if (ioh_getlastbuf(B, &ioh->ioh_readq, &room, NULL, &bid, 0) != 0)
      room = 0;

    // size	        now contains the number of bytes of data in queue (including sizeof(lengthfromwire) (biq_queuelen)
    // if (size > 4)
    //   lengthfromwire contains the host-order number of data bytes in the message (not including sizeof(lengthfromwire))
    // room	        contains the number of bytes "free" in the last buffer
    // bid	        points to the last buffer where these bytes are stored
    // cnt              now contains the number of buffers that this data is stored in

    bk_debug_printf_and(B, 4, "Bytes available: %d, lengthfromwire: %d, bytes free: %d, lastbid: %p, cnt: %d\n", size, lengthfromwire, room, bid, cnt);
  }


  switch (cmd)
  {
  case IOHT_FLUSH:
    // Clean any algorithm private data, or nuke it if ABORT
    break;

  case IOHT_HANDLER:
    if (aux == BK_RUN_WRITEREADY)
    {
      struct iovec iov[IOH_VS];

      for (bid = biq_minimum(ioh->ioh_writeq.biq_queue); bid; bid = biq_successor(ioh->ioh_writeq.biq_queue, bid))
      {
	if (bid->bid_data)
	{
	  iov[cnt].iov_base = bid->bid_data + bid->bid_used;
	  iov[cnt].iov_len = bid->bid_inuse;
	  cnt++;

	  // Have we got the vector & data or at least whatever is remaining?
	  if (BK_FLAG_ISSET(bid->bid_flags, BID_FLAG_MESSAGE) || cnt >= IOH_VS)
	    break;
	}
      }

      if (bid && cnt > 0)
      {
#ifdef BK_USING_PTHREADS
	ioh->ioh_incallback++;
	if (BK_GENERAL_FLAG_ISTHREADON(B))
	{
	  ioh->ioh_userid = pthread_self();
	  if (pthread_mutex_unlock(&ioh->ioh_lock) != 0)
	    abort();
	}
#endif /* BK_USING_PTHREADS */

	cnt = compress_write(B, ioh, ioh->ioh_writefun, ioh->ioh_iofunopaque, ioh->ioh_fdout, iov, cnt, 0);
	ioh->ioh_errno = errno;

#ifdef BK_USING_PTHREADS
	if (BK_GENERAL_FLAG_ISTHREADON(B))
	{
	  if (pthread_mutex_lock(&ioh->ioh_lock) != 0)
	    abort();
	  BK_ZERO(&ioh->ioh_userid);
	  pthread_cond_broadcast(&ioh->ioh_cond);
	}
	ioh->ioh_incallback--;
#endif /* BK_USING_PTHREADS */

	errno = ioh->ioh_errno;
	if (cnt == 0 || (cnt < 0 && IOH_EBLOCKINGINTR))
	{
	  // Not quite ready for writing yet
	  cnt = 0;
	}
	else if (cnt < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Write (in vector mode) failed\n");
	  BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_ERROR_OUTPUT);
	  ioh_flush_queue(B, ioh, &ioh->ioh_writeq, NULL, 0);
	  CALL_BACK(B, ioh, NULL, BkIohStatusIohWriteError);
	}
	else
	{					// Some (cnt) data written
	  // Figure out what buffers have been fully written
	  ioh_dequeue_byte(B, ioh, &ioh->ioh_writeq, (u_int32_t)cnt, 0);

	  if (ioh->ioh_writeq.biq_queuelen < 1)
	  {					// Nothing more to do
	    ioh->ioh_writeq.biq_queuelen = 0;
	    bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdout, 0, BK_RUN_WANTWRITE, 0);
	  }
	}
      }
      BK_RETURN(B, ioh->ioh_writeq.biq_queuelen > 0);
    }

    if (aux == BK_RUN_READREADY)
    {
      char *data;

      if (size < sizeof(lengthfromwire))
      {						// We still need to read length from wire
	if (!room)
	{					// We still need to allocate storage for same
	  if (!(data = malloc(sizeof(lengthfromwire) - size)))
	  {
	    bk_error_printf(B, BK_ERR_ERR, "Could not allocate input buffer for ioh %p of size %u: %s\n",ioh,(unsigned int)sizeof(lengthfromwire) - size,strerror(errno));
	    BK_RETURN(B, -1);
	  }

	  if (ioh_queue(B, &ioh->ioh_readq, data, sizeof(lengthfromwire) - size, 0, 0, NULL, 0, IohDataCmdNone, NULL, BK_IOH_BYPASSQUEUEFULL) < 0)
	  {
	    bk_error_printf(B, BK_ERR_ERR, "Could not insert input buffer for ioh %p input queue: %s\n",ioh,biq_error_reason(ioh->ioh_writeq.biq_queue, NULL));
	    free(data);
	    BK_RETURN(B, -1);
	  }

	  if (ioh_getlastbuf(B, &ioh->ioh_readq, &room, NULL, &bid, 0) != 0 || !room)
	  {
	    bk_error_printf(B, BK_ERR_ERR, "Could not find the buffer we just inserted\n");
	    BK_RETURN(B, -1);
	  }
	}
	else
	{
	  // Should not be necessary--under normal case this will be true
	  if (size >= 4)
	    room = MIN(room,lengthfromwire+sizeof(lengthfromwire)-size);
	  else
	    room = MIN(room,sizeof(lengthfromwire)-size);
	}

	/*
	 * Returning number of bytes to fill out length from wire (maybe less if
	 * something strange is happening WRT buffers with some room
	 * already on stack)
	 */
	BK_RETURN(B, room);
      }

      // We have length from wire
      if (size >= lengthfromwire+sizeof(lengthfromwire))
      {
	/*
	 * Uh--we already have a complete message in queue.  How?
	 *
	 * Anyway, cause message to be delivered to user and return
	 * zero (skipping this read notification cycle).
	 */
	ret = 0;
	goto IOHT_HANDLER_RMSG_case;
      }

      if (!room)
      {
	room = lengthfromwire - (size - sizeof(lengthfromwire));

	if (ioh->ioh_readq.biq_queuemax && room > ioh->ioh_readq.biq_queuemax)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Incoming message is greater than the maximum allowed size (%d > %d)\n", room, ioh->ioh_readq.biq_queuemax);
	  ioh_flush_queue(B, ioh, &ioh->ioh_readq, NULL, 0);
	  CALL_BACK(B, ioh, NULL, BkIohStatusIohReadError);
	  /**
	   * @bug
	   * <BUG>This is very very "bugus", since ioh_readallowed has no
	   * effect if the ERROR_INPUT flag is set.  In general, the handling
	   * of ioh_readallowed vs. ERROR_INPUT is not clearly thought out or
	   * implemented, and there is no possibility to perform a seek after
	   * hitting EOF.</BUG>
	   */
	  BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_ERROR_INPUT);
	  // bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdout, 0, BK_RUN_WANTREAD, 0);
	  bk_ioh_readallowed(B, ioh, 0, IOH_FLAG_ALREADYLOCKED);
	  BK_RETURN(B, 0);
	}

	bk_debug_printf_and(B, 2, "Attempting to allocate vectored storage of size %d (lengthfromwire %d)\n",room,lengthfromwire);
	if (!(data = malloc(room)))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not allocate input buffer for ioh %p of size %d: %s\n",ioh,room,strerror(errno));
	  BK_RETURN(B, -1);
	}

	if (ioh_queue(B, &ioh->ioh_readq, data, room, 0, 0, NULL, BID_FLAG_MESSAGE, IohDataCmdNone, NULL, BK_IOH_BYPASSQUEUEFULL) < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not insert input buffer for ioh %p input queue: %s\n",ioh,biq_error_reason(ioh->ioh_writeq.biq_queue, NULL));
	  free(data);
	  BK_RETURN(B, -1);
	}

	if (ioh_getlastbuf(B, &ioh->ioh_readq, &room, NULL, &bid, 0) != 0 || !room)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not find the buffer we just inserted\n");
	  BK_RETURN(B, -1);
	}
      }
      BK_RETURN(B, room);
    }
    bk_error_printf(B, BK_ERR_ERR, "Should never get here\n");
    BK_RETURN(B, -1);
    break;

  IOHT_HANDLER_RMSG_case:			// Goto target
  case IOHT_HANDLER_RMSG:
    if (size >= lengthfromwire+sizeof(lengthfromwire))
    {
      bk_vptr *sendup;
      u_int32_t tmp = 0;

      // This may allocate more than we need--cnt may be (usually will be) artificially
      // high due to buffers dedicated to lengthfromwire
      if (!BK_CALLOC_LEN(sendup,sizeof(*sendup)*(cnt+1)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not allocate data vectors to return data: %s\n", strerror(errno));
	BK_RETURN(B,-1);
      }

      // Actually fill out the data list
      size = cnt = 0;

      // tmp is now the number of sizeof(lengthfromwire) bytes we have seen
      // size is now the number of data bytes we have seen
      for (bid = biq_minimum(ioh->ioh_readq.biq_queue);
	   bid && (size < lengthfromwire);
	   bid = biq_successor(ioh->ioh_readq.biq_queue, bid))
      {
	if (bid->bid_data && bid->bid_inuse > 0)
	{
	  if (tmp < sizeof(lengthfromwire))
	  {
	    if (bid->bid_inuse > (sizeof(lengthfromwire) - tmp))
	    {
	      sendup[cnt].ptr = bid->bid_data+bid->bid_used + sizeof(lengthfromwire) - tmp;
	      sendup[cnt].len = MIN(bid->bid_inuse - (sizeof(lengthfromwire) - tmp),
				    lengthfromwire);
	      size += sendup[cnt].len;
	      cnt++;
	      tmp = sizeof(lengthfromwire);
	    }
	    else
	    {
	      tmp += bid->bid_inuse;
	    }
	  }
	  else
	  {
	    sendup[cnt].ptr = bid->bid_data+bid->bid_used;
	    sendup[cnt].len = MIN(bid->bid_inuse,lengthfromwire - size);
	    size += sendup[cnt].len;
	    cnt++;
	  }
	}
      }

      CALL_BACK(B, ioh, sendup, BkIohStatusReadComplete);

      // Check if the user seized the data (ie we don't have to free it)
      if (cnt == 1)
      {
	/*
	 * This is the common case where we've sent up exactly one
	 * buffer. We have optimized this case by caching the bid so that
	 * we can NULL it immediatly if we need to.
	 */
	if (!sendup[0].ptr)
	  bid_cache->bid_data = NULL;
      }
      else
      {
	// Otherwise we have to iterate through the whole list.
	cnt = 0;
	for (bid = biq_minimum(ioh->ioh_readq.biq_queue);
	     bid;
	     bid = biq_successor(ioh->ioh_readq.biq_queue, bid))
	{
	  if (bid->bid_data && !sendup[cnt++].ptr)
	    bid->bid_data=NULL;
	}
      }

      // Nuke vector list
      free(sendup);

      size += tmp;				// Include size of lengthfromwire

      // Delete buffers that have been used
      ioh_dequeue_byte(B, ioh, &ioh->ioh_readq, (u_int32_t)size, 0);
    }
    break;

  default:
    bk_error_printf(B, BK_ERR_ERR, "Unknown command %d/%x\n",cmd,aux);
    BK_RETURN(B,-1);
  }

  BK_RETURN(B, ret);
}



/**
 * Line--"/n" terminated lines--IOH Type routines to perform I/O
 * maintenance and activity.  A mechanism should be devised to specify
 * the EOL character (or preferably sequence but that would really
 * suck).
 *
 * THREADS: REENTRANT (ioh must already be locked)
 *
 *	@param B BAKA Thread/Global state
 *	@param ioh The IOH environment handle
 *	@param aux Auxiliary information for the command
 *	@param cmd Command we are supposed to perform
 *	@param flags Fun for the future
 *	@return <i>-1</i> Call failure, allocation failure, CLC failure, other failure
 *	@return <br><i>0</i> Success & queue empty
 *	@return <br><i>1</i> Success & queue non-empty
 */
static int ioht_line_other(bk_s B, struct bk_ioh *ioh, u_int aux, u_int cmd, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret = 0;
  struct bk_ioh_data *bid;
  u_int32_t needed, size = 0;
  int cnt = 0;

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  bk_debug_printf_and(B, 1, "Line other cmd %d/%d for IOH %p\n", cmd, aux, ioh);

  switch (cmd)
  {
  case IOHT_FLUSH:
    // Clean any algorithm private data, or nuke it if ABORT
    break;

  case IOHT_HANDLER:
    if (aux == BK_RUN_WRITEREADY)
    {
      /*
       * We are not enforcing line oriented output, thus this is no different
       * that raw, so we will trivially call raw.
      */
      BK_RETURN(B, ioht_raw_other(B, ioh, aux, cmd, flags));
    }

    if (aux == BK_RUN_READREADY)
    {
      /*
       * Determining the number of bytes to read is an impossible
       * science in line oriented input modes.  No one policy will
       * ever be correct, and the one used by raw seems just as good
       * as any other, so let's use raw.
       *
       * I suppose one possible modification would be to ensure
       * that everything is NUL terminated, but since the stuff
       * is not guaranteed contiguous, this is not too interesting.
       */
      BK_RETURN(B, ioht_raw_other(B, ioh, aux, cmd, flags));
    }
    break;

  case IOHT_HANDLER_RMSG:
    /*
     * Here is the interesting bit.  Determining if we have at least
     * one full line, making a buffer list for it, and sending it up.
     *
     * Since we may have many lines in the queue, we loop over the data
     * until the data is exhausted or we don't find a complete line.
     */
    while (ioh->ioh_readq.biq_queuelen > 0 && !ioh->ioh_throttle_cnt)
    {
      bk_vptr *sendup;
      u_int32_t tmp = 0;

      // Find number of segments
      size = cnt = 0;
      for (bid = biq_minimum(ioh->ioh_readq.biq_queue);
	   bid;
	   bid = biq_successor(ioh->ioh_readq.biq_queue, bid))
      {
	if (bid->bid_data && bid->bid_inuse > 0)
	{
	  cnt++;
	  for (tmp=0; tmp<bid->bid_inuse; tmp++)
	  {
	    size++;
	    if (*(bid->bid_data + tmp + bid->bid_used) == ioh->ioh_eolchar)
	    {
	      // Hurrah--we have found E-O-L
	      goto outloop;
	    }
	  }
	}
      }
    outloop:
      needed = size;

      if (!bid || cnt < 1)
      {						// No bid, no line (at least not this time)
	BK_RETURN(B, 0);
      }

      // Allocate send-up buffers
      if (!BK_CALLOC_LEN(sendup,sizeof(*sendup)*(cnt+1)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not allocate data vectors to return data: %s\n", strerror(errno));
	BK_RETURN(B,-1);
      }

      // Actually fill out the data list
      size = cnt = 0;
      for (bid = biq_minimum(ioh->ioh_readq.biq_queue);
	   bid && (size < needed);
	   bid = biq_successor(ioh->ioh_readq.biq_queue, bid))
      {
	if (bid->bid_data && bid->bid_inuse > 0)
	{
	  sendup[cnt].ptr = bid->bid_data + bid->bid_used;
	  sendup[cnt].len = MIN(bid->bid_inuse, needed-size);
	  size += sendup[cnt].len;
	  cnt++;
	}
      }

      CALL_BACK(B, ioh, sendup, BkIohStatusReadComplete);

      // Nuke vector list
      free(sendup);

      // Delete buffers that have been used
      ioh_dequeue_byte(B, ioh, &ioh->ioh_readq, (u_int32_t)size, 0);
    }

    // Check for input overflow
    if (ioh->ioh_readq.biq_queuemax && (ioh->ioh_readq.biq_queuelen > ioh->ioh_readq.biq_queuemax))
    {
      // We are over the maximum size of queued data.  Send current data up as incomplete.
      ioh_sendincomplete_up(B, ioh, 0, 0);
    }

    break;

  default:
    bk_error_printf(B, BK_ERR_ERR, "Unknown command %d/%x\n",cmd,aux);
    BK_RETURN(B,-1);
  }

  BK_RETURN(B, ret);
}



/**
 * Flush a input or output queue completely
 *
 * THREADS: REENTRANT (ioh must already have been locked)
 *
 *	@param B BAKA Thread/global state
 *	@param ioh IOH state handle
 *	@param queue I/O data queue
 *	@param cmd Copy-out pending commands on this queue
 *	@param flags Flags
 */
static void ioh_flush_queue(bk_s B, struct bk_ioh *ioh, struct bk_ioh_queue *queue, dict_h *cmdsp, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ioh_data *data;
  dict_h cmds = NULL;
  struct ioh_data_cmd *idc;

  if (!ioh || !queue)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  bk_debug_printf_and(B, 1, "Internal flush of IOH %p queue %p\n", ioh, queue);

  if (cmdsp) *cmdsp = NULL;

  while ((data = biq_minimum(queue->biq_queue)))
  {
    if (data->bid_idc.idc_type != IohDataCmdNone && cmdsp)
    {
      // Copy command and insert it cmds list (creating same if required
      if (!cmds)
      {
	if (!(cmds = cmd_list_create(NULL, NULL, DICT_UNORDERED)))
	{
	  bk_error_printf(B, BK_ERR_ERR, "could not create command list: %s\n", cmd_list_error_reason(cmds,NULL));
	}
	*cmdsp = cmds;
      }
      else
      {
	if (!(idc = idc_create(B)))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not allocate idc (cmd lost)\n");
	  /*
	   * Do *not* destroy cmds. There might be other commands and if
	   * not, the empty list will be passed to execute_commands where
	   * it will be harmless processed and destroyed
	   */
	}
	else
	{
	  // <WARNING> Structure copy!! If idc changes substantially this might need fixing </WARNING>
	  *idc = data->bid_idc;
	  if (cmd_list_append(cmds, idc) != DICT_OK)
	  {
	    bk_error_printf(B, BK_ERR_ERR, "Could not insert idc in list: %s\n", cmd_list_error_reason(cmds, NULL));
	    idc_destroy(B,idc);
	  }
	}
      }
    }

    /*
     * <WARNING>There was a NULL in place of the ioh here, but the
     * user callback is not called when the ioh is not present.  This
     * means we leak memory (the bounding vptr and other associated
     * data--and perhaps other bad things) when the data queue is
     * freed.  But it seems unlikely that this was done without a good
     * reason, so if you encounter problems...</WARNING>
     */
    ioh_dequeue(B, ioh, queue, data, IOH_DEQUEUE_ABORT);
  }

  ioh->ioh_readq.biq_queuelen = 0;

  // Nuke any algorithm-private data
  if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_RAW))
  {
    ioht_raw_other(B, ioh, 0, IOHT_FLUSH, flags);
  }
  else if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_BLOCKED))
  {
    ioht_block_other(B, ioh, 0, IOHT_FLUSH, flags);
  }
  else if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_VECTORED))
  {
    ioht_vector_other(B, ioh, 0, IOHT_FLUSH, flags);
  }
  else if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_LINE))
  {
    ioht_line_other(B, ioh, 0, IOHT_FLUSH, flags);
  }
  else
  {
    bk_error_printf(B, BK_ERR_ERR, "Unknown message format type %x\n",ioh->ioh_extflags);
    BK_VRETURN(B);
  }

  if (BK_FLAG_ISSET(flags, IOH_FLUSH_DESTROY))
  {
    biq_destroy(queue->biq_queue);
  }

  BK_VRETURN(B);

}



/**
 * Determine the last buffer on the input stack and free space for writes.
 *
 * THREADS: REENTRANT (ioh must already be locked)
 *
 *	@param B BAKA Thread/global state
 *	@param queue Input data queue
 *	@param size Size remaining in the last buffer
 *	@param data Pointer to start of last free buffer
 *	@param flags Flags
 *	@return <i>-1</i> on call failure
 *	@return <br><i>0</i> on success (including no bytes available)
 *	@return <br><i>1</i> if no data could be found
 */
static int ioh_getlastbuf(bk_s B, struct bk_ioh_queue *queue, u_int32_t *size, char **data, struct bk_ioh_data **bidp, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ioh_data *bid;

  if (!queue || !size)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  bk_debug_printf_and(B, 1, "Getting last buffer information for IOH queue %p\n", queue);

  for (bid = biq_maximum(queue->biq_queue); bid; bid = biq_predecessor(queue->biq_queue, bid))
  {
    if (bid->bid_data)
    {
      if (data) *data = bid->bid_data + bid->bid_inuse + bid->bid_used;
      *size = bid->bid_allocated - bid->bid_inuse - bid->bid_used;
      if (bidp) *bidp = bid;
      BK_RETURN(B,0);
    }
  }

  *size = 0;
  if (data) *data = NULL;
  if (bidp) *bidp = NULL;
  BK_RETURN(B,1);
}



/**
 * Read some data from the file represented by this IOH
 *
 * THREADS: REENTRANT (ioh must already be locked)
 *
 *	@param B BAKA Thread/global state
 *	@param ioh IOH state handle
 *	@param fd File descriptor to read from
 *	@param data Where to place the data
 *	@param len Length to read
 *	@param flags Flags
 *	@return <i>-1</i> on call failure or read failure
 *	@return <br><i>0</i> on EOF
 *	@return <br><i>positive</i> indicating number of bytes read
 */
static int ioh_internal_read(bk_s B, struct bk_ioh *ioh, int fd, char *data, size_t len, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret;

  if (!ioh || !data || fd < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    errno = EINVAL;
    BK_RETURN(B,-1);
  }

  bk_debug_printf_and(B, 1, "Internal read IOH %p (filedes: %d) of %u bytes\n", ioh, fd, (unsigned int)len);

  if (bk_run_fd_is_closed(B, ioh->ioh_run, ioh->ioh_fdin))
  {
    bk_debug_printf_and(B,1,"IOH is adminstratively closed\n");
    BK_RETURN(B,0);
  }

#ifdef BK_USING_PTHREADS
  ioh->ioh_incallback++;
  if (BK_GENERAL_FLAG_ISTHREADON(B))
  {
    ioh->ioh_userid = pthread_self();
    if (pthread_mutex_unlock(&ioh->ioh_lock) != 0)
      abort();
  }
#endif /* BK_USING_PTHREADS */

  ret = (*ioh->ioh_readfun)(B, ioh, ioh->ioh_iofunopaque, fd, data, len, flags);
  ioh->ioh_errno = errno;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B))
  {
    if (pthread_mutex_lock(&ioh->ioh_lock) != 0)
      abort();
    BK_ZERO(&ioh->ioh_userid);
    pthread_cond_broadcast(&ioh->ioh_cond);
  }
  ioh->ioh_incallback--;
#endif /* BK_USING_PTHREADS */

  if (ret > 0)
    ioh->ioh_tell += ret;

  BK_RETURN(B,ret);
}



/**
 * Send all pending data on the input queue up to the user
 *
 * THREADS: REENTRANT (ioh must already be locked)
 *
 *	@param B BAKA Thread/global state
 *	@param ioh IOH state handle
 *	@param filter Only send messages with this mark up
 *	@param flags Flags
 */
static void ioh_sendincomplete_up(bk_s B, struct bk_ioh *ioh, u_int32_t filter, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ioh_data *bid;
  int cnt = 0;
  bk_vptr *sendup;

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  bk_debug_printf_and(B, 1, "Send incomplete reads up for IOH %p\n", ioh);

  // Find out how many data segments we have
  for (bid = biq_minimum(ioh->ioh_readq.biq_queue);
       bid;
       bid = biq_successor(ioh->ioh_readq.biq_queue, bid))
  {
    bk_debug_printf_and(B, 1, "Checking %p for incomplete %d (%p,%d) -- cnt %d\n", bid, bid->bid_flags, bid->bid_data, bid->bid_inuse, cnt);
    if (filter && BK_FLAG_ISCLEAR(bid->bid_flags, filter))
      continue;
    if (bid->bid_data && bid->bid_inuse)
      cnt++;
  }

  if (!cnt)
  {
    // Only things left might be allocated but unused buffers.
    // No cmds can exist on the read queue.
    bk_ioh_flush(B, ioh, SHUT_RD, IOH_FLAG_ALREADYLOCKED);
    BK_VRETURN(B);
  }

  if (!BK_CALLOC_LEN(sendup,sizeof(*sendup)*(cnt+1)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate data vectors to return data: %s\n", strerror(errno));
    BK_VRETURN(B);
  }

  // Actually fill out the data list
  cnt = 0;
  for (bid = biq_minimum(ioh->ioh_readq.biq_queue);
       bid;
       bid = biq_successor(ioh->ioh_readq.biq_queue, bid))
  {
    if (filter && BK_FLAG_ISCLEAR(bid->bid_flags, filter))
      continue;
    if (bid->bid_data && bid->bid_inuse)
    {
      sendup[cnt].ptr = bid->bid_data+bid->bid_used;
      sendup[cnt].len = bid->bid_inuse;
      cnt++;
    }
  }

  bk_debug_printf_and(B, 1, "We have %d incomplete reads we are sending up\n", cnt);
  CALL_BACK(B, ioh, sendup, BkIohStatusIncompleteRead);

  // Nuke vector list
  free(sendup);

  // Nuke everything in the input queue, we have "used" the necessary stuff
  bk_ioh_flush(B, ioh, SHUT_RD, IOH_FLAG_ALREADYLOCKED);

  BK_VRETURN(B);
}



/**
 * Execute all special elements on the front of the stack, until we see the first
 * non-special (note the first non-special may be the first).
 *
 * THREADS: REENTRANT (ioh must already be locked)
 *
 *	@param B BAKA Thread/global state
 *	@param ioh IOH state handle
 *	@param queue I/O queue to look at
 *	@param flags Flags
 *	@return <i>-1</i> on call failure
 *	@return <br><i>0</i> on success
 *	@return <br><i>0</i> on success if there is no data pending (queue empty)
 *	@return <br><i>1</i> on success if there is data pending on the queue
 *	@return <br><i>2</i> on success if the IOH was shut down
 */
static int ioh_execute_ifspecial(bk_s B, struct bk_ioh *ioh, struct bk_ioh_queue *queue, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ioh_data *bid;
  int ret = 0;
  dict_h cmds = NULL;
  struct ioh_data_cmd *idc;

  if (!ioh || !queue)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  bk_debug_printf_and(B, 1, "Execute first items on stack if they are special for IOH %p queue %p\n", ioh, queue);

  while ((bid = biq_minimum(queue->biq_queue)))
  {
    bk_debug_printf_and(B, 2, "Checking bid %p (data-%p/%d/%d, flags-%x)\n", bid, bid->bid_data, bid->bid_inuse, bid->bid_used, bid->bid_flags);
    if (bid->bid_data)
      break;

    if (bid->bid_idc.idc_type != IohDataCmdNone)
    {
      // Copy command and insert it cmds list (creating same if required
      if (!cmds && !(cmds = cmd_list_create(NULL, NULL, DICT_UNORDERED)))
	{
	  bk_error_printf(B, BK_ERR_ERR, "could not create command list: %s\n", cmd_list_error_reason(cmds,NULL));
	}
      else
      {
	if (!(idc = idc_create(B)))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not allocate idc (cmd lost)\n");
	  /*
	   * Do *not* destroy cmd. There might be other commands and if
	   * not, the empty list will be passed to execute_commands where
	   * it will be harmless processed and destroyed
	   */
	}
	else
	{
	  // <WARNING> Structure copy!! If idc changes substantially this might need fixing </WARNING>
	  *idc = bid->bid_idc;
	  if (cmd_list_append(cmds, idc) != DICT_OK)
	  {
	    bk_error_printf(B, BK_ERR_ERR, "Could not insert idc in list: %s\n", cmd_list_error_reason(cmds, NULL));
	    idc_destroy(B,idc);
	  }
	}
      }
    }

    ioh_dequeue(B, NULL, queue, bid, 0);
  }

  if (cmds)
  {
    if ((ret = ioh_execute_cmds(B, ioh, cmds, 0)) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not execute commands stored at front of list\n");
      BK_RETURN(B,-1);
    }
    if (ret == 2)
      BK_RETURN(B, 2);
  }

  if (bid && bid->bid_data)
    BK_RETURN(B,1);

  BK_RETURN(B,0);
}



/**
 * Execute the commands on the cmds list.
 *
 * THREADS: REENTRANT (ioh must already be locked)
 *
 *	@param B BAKA Thread/global state
 *	@param ioh IOH state handle
 *	@param cmds list of _CLOSE/_SHUTDOWN/etc. commands
 *	@param flags Flags
 *	@return <i>-1</i> on call failure
 *	@return <br><i>0</i> on success
 *	@return <br><i>2</i> destroyed IOH
 */
static int ioh_execute_cmds(bk_s B, struct bk_ioh *ioh, dict_h cmds, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct ioh_data_cmd *idc;
  int ret = 0;
  struct ioh_seek_args *isa = NULL;


  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  while((idc = cmd_list_minimum(cmds)))
  {
    bk_debug_printf_and(B, 1, "Execute cmds %d for IOH %p\n", idc->idc_type, ioh);

    // Process commands until (and if) we see a CmdClose (but dequeue and destroy everything).
    if (ret != 2)
    {
      switch (idc->idc_type)
      {
      case IohDataCmdShutdown:
	bk_debug_printf_and(B,2,"Shutingdown writes on descriptor: %d\n", ioh->ioh_fdout);
	BK_IOH_SHUTDOWN(ioh,SHUT_WR);
	BK_FLAG_CLEAR(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_OUTPUT_PEND);
	BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_OUTPUT);
	break;

      case IohDataCmdClose:
	bk_ioh_destroy(B, ioh);
	ret = 2;
	break;

      case IohDataCmdSeek:
	ret = 0;
	if (!idc->idc_args)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Seek command contained no args! Seek failed\n");
	  ret = -1;
	}
	else
	{
	  isa = idc->idc_args;
	  if (lseek(ioh->ioh_fdin, isa->isa_offset, isa->isa_whence) < 0)
	  {
	    bk_error_printf(B, BK_ERR_ERR, "Could lseek: %s\n", strerror(errno));
	  }
	  else
	  {
	    ioh_flush_queue(B, ioh, &ioh->ioh_readq, NULL, 0);
	  }
	}

	bk_ioh_readallowed(B, ioh, 1, IOH_FLAG_ALREADYLOCKED);
	free(isa);

	CALL_BACK(B, ioh, NULL, ret==0?BkIohStatusIohSeekSuccess:BkIohStatusIohSeekFailed);

	break;

      default:
	bk_error_printf(B, BK_ERR_ERR, "Unknown bid command: %d\n", idc->idc_type);
	break;
      }
    }

    if (cmd_list_delete(cmds, idc) != DICT_OK)
    {
      bk_error_printf(B, BK_ERR_ERR, "could not delete idc from cmds list: %s\n", cmd_list_error_reason(cmds, NULL));
      break;
    }
    idc_destroy(B, idc);
  }
  cmd_list_destroy(cmds);

  BK_RETURN(B,ret);
}



/**
 * Standard read() functionality in IOH API
 *
 * THREADS: MT-SAFE
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
int bk_ioh_stdrdfun(bk_s B, struct bk_ioh *ioh, void *opaque, int fd, caddr_t buf, __SIZE_TYPE__ size, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret, erno = 0;

  errno = 0;
  if ((ret = read(fd, buf, size)) < 0)
  {
    erno = errno;
    if (!IOH_EBLOCKINGINTR && errno != EIO)
      bk_error_printf(B, BK_ERR_ERR, "read syscall failed on fd %d of size %zu: %s\n", fd, size, strerror(errno));
  }

  bk_debug_printf_and(B, 1, "System read returns %d with errno %d\n", ret, errno);

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
 * Standard write() functionality in IOH API
 *
 * THREADS: MT-SAFE
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
int bk_ioh_stdwrfun(bk_s B, struct bk_ioh *ioh, void *opaque, int fd, struct iovec *buf, __SIZE_TYPE__ size, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret = 0, erno = 0;
  int cursize = size;
  int offset = 0;
  ssize_t bytes_to_write = 0;
  ssize_t bytes_written = 0;
  __SIZE_TYPE__ i;

  errno = 0;

  for (i=0;i<size;i++)
  {
    if (bytes_to_write + (ssize_t)buf[i].iov_len < bytes_to_write)
    {
      // iov is too big--complete vector write would overwhelm return code size
      size = i;
      break;
    }
    bytes_to_write += buf[i].iov_len;
  }

  /*
   * Spin on this FD as long as possible.  With pipe fds
   * the 4K maximum write size means that the overhead of going
   * out of this function and through select before we get to
   * write another 4K chunk will be overwhelming.
   */
  while (bytes_to_write > 0)
  {
    // avoid EINVAL errors from exceeding maxiov limit (if any)
    if (ioh->ioh_maxiov!= 0 && (size - offset) > ioh->ioh_maxiov)
      cursize = ioh->ioh_maxiov;
    else
      cursize = size - offset;

    if ((ret = writev(fd, buf+offset, cursize)) < 0)
    {
      erno = errno;
      if (!IOH_EBLOCKINGINTR)
	bk_error_printf(B, BK_ERR_ERR, "write syscall failed on fd %d of size %zu: %s\n", fd, size, strerror(errno));
    }

    bk_debug_printf_and(B, 1, "System writev returns %d with errno %d\n",ret,errno);
    if (ret < 1)
    {
      if (!bytes_written)
	bytes_written = ret;
      break;
    }

    bytes_written += ret;
    bytes_to_write -= ret;

    if (bytes_to_write)
    {
      // Figure out where the next write should start in the iov
      for (i=offset;i<size && ret > 0;i++)
      {
	if (ret >= (ssize_t)buf[i].iov_len)
	{
	  offset++;
	  ret -= buf[i].iov_len;
	}
	else
	{
	  buf[i].iov_base = ((char *)buf[i].iov_base) + ret;
	  buf[i].iov_len -= ret;
	  break;
	}
      }
    }
  }

  if (bk_debug_and(B, 0x20) && ret > 0)
  {
    u_int x;
    int myret = ret;
    int len = 0;
    for (x = 0; x < size && myret > 0; x++)
    {
      bk_vptr dbuf;

      dbuf.ptr = buf[x].iov_base;
      dbuf.len = MIN(MIN((u_int)buf[x].iov_len, 128U), (u_int)myret);

      len += buf[x].iov_len;
      bk_debug_printbuf_and(B, 0x20, "Buffer just wrote:", "\t", &dbuf);
      myret -= buf[x].iov_len;
    }
    bk_debug_printf_and(B, 1, "Returns %d, but submitted %d\n",ret,len);
  }

  errno = erno;
  BK_RETURN(B, bytes_written);
}



#define UNLINK_AF_LOCAL_FILE(fdin)								\
{												\
  struct sockaddr_un __sun;									\
  socklen_t __len = sizeof(__sun);								\
												\
  /* If this is an AF_LOCAL socket, then remove the associated file */				\
  if (getsockname((fdin), (struct sockaddr *)(&__sun), &__len) > 0)				\
  {												\
    if (__sun.sun_family == AF_LOCAL && __sun.sun_path &&					\
	(!BK_STREQ(__sun.sun_path, "")) && (unlink(__sun.sun_path) < 0))			\
    {												\
      if (errno != ENOENT)									\
	bk_error_printf(B, BK_ERR_ERR, "Could not unlink AF_LOCAL file: %s\n", __sun.sun_path);	\
    }												\
  }												\
}



/**
 * Standard close() functionality in IOH API.
 *
 * It's totally bogus to check for the AF_LOCAL file here, but since it
 * gets *created* automatically (by the kernel), it would be nice to have
 * it disappear automatically too and this is sort of the place. NB: this
 * function may be overridden by the user in which case he is on his own.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state
 *	@param ioh The ioh to use (may be NULL for those not including libbk_internal.h)
 *	@param opaque Common opaque data for read and write funs
 *	@param fdin File descriptor
 *	@param fdout File descriptor
 *	@param flags Fun for the future
 *	@return Standard @a writev() return codes
 */
void bk_ioh_stdclosefun(bk_s B, struct bk_ioh *ioh, void *opaque, int fdin, int fdout, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (fdin >= 0)
  {
    UNLINK_AF_LOCAL_FILE(fdin);
    close(fdin);
  }

  if ((fdout >= 0) && (fdout != fdin))
  {
    UNLINK_AF_LOCAL_FILE(ioh->ioh_fdout);
    close(ioh->ioh_fdout);
  }

  BK_VRETURN(B);
}



/**
 * Compress data (if compression enabled) and call write function for this ioh.
 *
 *	@param B BAKA Thread/global state
 *	@param ioh The ioh to use (may be NULL for those not including libbk_internal.h)
 *	@param writefun Underlying write function to use
 *	@param opaque Common opaque data for read and write funs
 *	@param fd File descriptor
 *	@param iovec Data to write
 *	@param size Number of iovec buffers
 *	@param flags Fun for the future
 *	@return Standard @a writev() return codes
 *
 *	@return <i>the number of bytes written</i> on success
 *	@return <i>-1</i> on error
 */
static int
compress_write(bk_s B, struct bk_ioh *ioh, bk_iowfunc_f writefun, void *opaque, int fd,
	       struct iovec *buf, __SIZE_TYPE__ size, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret;

  errno = 0;

  if (ioh && ioh->ioh_compress_level)
  {
#ifdef IOH_COMPRESS_SUPPORT
    char *src = NULL;
    char *dst = NULL;
    u_int cnt;
    char *q;
    int len = 0;
    int new_len;
    struct iovec vector;			// singleton vector for simple writes

    ret =-1;					// Assume failure
    for (cnt = 0; cnt < size; cnt++)
    {
      if (!(q = realloc(src, len + buf[cnt].iov_len)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not allocat compress source");
	free(src);
	src = NULL;
	break;
      }
      src = q;
      memcpy(src+len, buf[cnt].iov_base, buf[cnt].iov_len);
      len += buf[cnt].iov_len;
    }
    if (src)
    {
      new_len = BK_COMPRESS_SWELL(len);
      if (!(BK_MALLOC_LEN(dst, new_len)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not allocate compress dest");
      }
      else
      {
	int comp_ret;
	if ((comp_ret = compress2(dst, (uLongf *)&new_len, src, len, ioh->ioh_compress_level)) != Z_OK)
	{
	  // <TODO> Need compression perror message here </TODO>
	  bk_error_printf(B, BK_ERR_ERR, "Could not compress buffer\n");
	}
	else
	{
	  // Sigh I think we need to loop here until we write out everything or get -1.
	  vector.iov_base = dst;
	  vector.iov_len = new_len;
	  ret = (*writefun)(B, ioh, opaque, fd, &vector, 1, 0);
	}
      }
      free(src);
      if (dst)
	free(dst);
    }
#else
    ret = -1;
#endif
  }
  else
  {
    ret = (*writefun)(B, ioh, opaque, fd, buf, size, 0);
  }

  BK_RETURN(B, ret);
}



/**
 * Flush an ioh read queue.
 *
 * THREADS: MT-SAFE (assuming different ioh)
 * THREADS: THREAD-REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param ioh The @a bk_ioh to use.
 *	@param flags Flags to pass to internal flush routines.
 */
void
bk_ioh_flush_read(bk_s B, struct bk_ioh *ioh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  ioh_flush_queue(B, ioh, &ioh->ioh_readq, NULL, flags);

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_VRETURN(B);
}



/**
 * Flush an ioh write queue.
 *
 * THREADS: MT-SAFE (assuming different ioh)
 * THREADS: THREAD-REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param ioh The @a bk_ioh to use.
 *	@param flags Flags to pass to internal flush routines.
 */
void
bk_ioh_flush_write(bk_s B, struct bk_ioh *ioh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  ioh_flush_queue(B, ioh, &ioh->ioh_writeq, NULL, flags);

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_VRETURN(B);
}




/**
 * Create an @a ioh_data_cmd.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a ioh_data_cmd on success.
 */
static struct ioh_data_cmd *
idc_create(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct ioh_data_cmd *idc;

  if (!(BK_CALLOC(idc)))
  {
    bk_error_printf(B, BK_ERR_ERR, "could not allocate idc: %s\n", strerror(errno));
    BK_RETURN(B,NULL);
  }
  BK_RETURN(B,idc);
}



/**
 * Destroy a idc.
 *
 * THREADS: MT-SAFE (assuming different idc)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param idc The @a ioh_cmd_data to destroy.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static void
idc_destroy(bk_s B, struct ioh_data_cmd *idc)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!idc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  free(idc);
  BK_VRETURN(B);
}




/**
 * Seek on an ioh. This is trickier then it sounds. We have to pause reads,
 * place a cmd on the write queue and wait for that to flush, *then* run
 * the seek. If the seek succeeds we then have to flush the current input
 * queue and then notify the user. If the seek fails we have to notify the
 * user, but we don't flush the input queue. The rest of the function is in
 * ioh_execute_cmds()
 *
 * THREADS: MT-SAFE (assuming different ioh)
 * THREADS: THREAD-REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param ioh The @a bk_ioh to use.
 *	@param offset As described in @a lseek(2)
 *	@param whence As described in @a lseek(2)
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 *	@return <i>2</i> if ioh destroyed.
 */
int
bk_ioh_seek(bk_s B, struct bk_ioh *ioh, off_t offset, int whence)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct ioh_seek_args *isa = NULL;

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  bk_ioh_readallowed(B, ioh, 0, IOH_FLAG_ALREADYLOCKED);

  if (!(BK_CALLOC(isa)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate isa: %s\n", strerror(errno));
    goto error;
  }

  isa->isa_offset = offset;
  isa->isa_whence = whence;

  if (ioh_queue(B, &ioh->ioh_writeq, NULL, 0, 0, 0, NULL, 0, IohDataCmdSeek, isa, BK_IOH_BYPASSQUEUEFULL) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not enqueue message\n");
    goto error;
  }

  // While this appears to do nothing, it someone adds code later, it guards against double free.
  isa = NULL;

  // Execute seek immediately if queue was empty
  if (ioh_execute_ifspecial(B, ioh, &ioh->ioh_writeq, 0) == 2)
    BK_RETURN(B,2);				// IOH was destroyed

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B,0);

 error:
  if (isa) free(isa);
  bk_ioh_readallowed(B, ioh, 1, IOH_FLAG_ALREADYLOCKED);
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */
  BK_RETURN(B,-1);
}



/**
 * IOH coalescing routine for external users who need unified buffers
 * w/optimizations for simple cases. NB @a data is <em>not</em> freed. This
 * routine is written to the ioh read API and according to that API the ioh
 * system frees the data it has read.
 *
 * Update: this routine now supports the flag
 * BK_IOH_COALESCE_FLAG_SEIZE_DATA (which is mutually exclussive of
 * BK_IOH_COALESCE_FLAG_MUST_COPY) which seizes control of the data just as
 * if it had copied it but tells the IOH layer not to free it. NB this can
 * only happen when @a data is one buffer and curvptr is NULL (this is the
 * most common case though). NB(2): It is the resonsiblity of the caller to
 * verify that data seizing is permitted for this data (see
 * BK_IOH_DATA_SEIZE_PERMITTED())
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA global/thread state
 *	@param data NULL terminated array of vectored pointers
 *	@param curvptr Optional remaining data from previous call--user must free
 *	@param flags MUST_COPY and TRAILING_NULL
 *	@return <i>NULL</i> on call failure, allocation failure
 *	@return <br><i>new vptr</i> on success
 */
bk_vptr *bk_ioh_coalesce(bk_s B, bk_vptr *data, bk_vptr *curvptr, bk_flags in_flags, bk_flags *out_flagsp)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  bk_vptr *new = NULL;
  bk_vptr *cur;
  int cbuf = 0;
  int nulldata = 0;
  int cdata = 0;
  char *optr;

  if (!data)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  if (out_flagsp)
    *out_flagsp = 0;

  if (curvptr && curvptr->ptr && curvptr->len > 0)
  {
    cbuf++;
    cdata += curvptr->len;
  }

  for (cur=data;cur->ptr;cur++)
  {
    cbuf++;
    cdata += cur->len;
  }

  if (BK_FLAG_ISSET(in_flags, BK_IOH_COALESCE_FLAG_TRAILING_NULL))
  {
    BK_FLAG_SET(in_flags, BK_IOH_COALESCE_FLAG_MUST_COPY);
    nulldata = 1;
  }

  if (cdata < 0 || nulldata < 0 || cdata+nulldata < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Overflow condition!  More than 2^31 bytes in queue...\n");
    goto error;
  }

  if (cbuf > 1 || (curvptr && curvptr->ptr && curvptr->len > 0) ||
      BK_FLAG_ISSET(in_flags, BK_IOH_COALESCE_FLAG_MUST_COPY))
  {
    if (!BK_MALLOC(new) || !BK_MALLOC_LEN(new->ptr, cdata + nulldata))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate data during coalescing of %d bytes: %s\n", cdata, strerror(errno));
      goto error;
    }
    new->len = cdata + nulldata;
    optr = new->ptr;

    if (curvptr && curvptr->ptr && curvptr->len > 0)
    {
      memcpy(optr, curvptr->ptr, curvptr->len);
      optr += curvptr->len;
    }

    for (cur=data;cur->ptr;cur++)
    {
      memcpy(optr, cur->ptr, cur->len);
      optr += cur->len;
      // NO FREE of cur->ptr (see above).
    }
    // NO FREE off data (see above).

    if (BK_FLAG_ISSET(in_flags, BK_IOH_COALESCE_FLAG_TRAILING_NULL))
      ((char *)new->ptr)[cdata] = 0;		// Ensure null termination
  }
  else
  {
    if (BK_FLAG_ISSET(in_flags, BK_IOH_COALESCE_FLAG_SEIZE_DATA))
    {
      if (!(BK_MALLOC(new)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not allocate data vptr: %s\n", strerror(errno));
	goto error;
      }
      *new = *data;
      data->ptr = NULL;
    }
    else
      new = data;

    if (out_flagsp)
      BK_FLAG_SET(*out_flagsp, BK_IOH_COALESCE_OUT_FLAG_NO_COPY);
  }

  BK_RETURN(B, new);

 error:
  if (new && new != data)
  {
    if (new->ptr && BK_FLAG_ISCLEAR(in_flags, BK_IOH_COALESCE_FLAG_SEIZE_DATA))
      free(new->ptr);
    free(new);
  }

  BK_RETURN(B, NULL);
}



/*
 * Copies string for output to IOH
 * This function is only safe for C99 compliant (glibc 2.1+) compilers
 *
 * THREADS: MT-SAFE (assuming different ioh)
 * THREADS: THREAD-REENTRANT (otherwise)
 *
 * @param B BAKA Thread/global state
 * @param ioh IOH for output
 * @param str String to output
 * @return <i>0</i> on success<br>
 * @return <i>negative</i> on failure (including queue too full)
 */
extern int bk_ioh_print(bk_s B, struct bk_ioh *ioh, const char *str)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  bk_vptr *data = NULL;                         // to pass to bk_ioh_write

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "invalid arguments\n");
    BK_RETURN(B, -1);
  }

  BK_CALLOC(data);
  if (!data)
  {
    bk_error_printf(B, BK_ERR_ERR, "Calloc failed\n");
    goto error;
  }
  data->len = strlen(str);
  data->ptr = malloc(data->len);
  if (!data->ptr)
  {
    bk_error_printf(B, BK_ERR_ERR, "Malloc failed\n");
    goto error;
  }

  memcpy(data->ptr, str, data->len);

  if (bk_ioh_write(B, ioh, data, 0) != 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "IOH write failed\n");
    goto error;
  }
  data = NULL;

  BK_RETURN(B, 0);

 error:
  if (data)
  {
    if (data->ptr)
    {
      free(data->ptr);
    }
    free(data);
  }
  BK_RETURN(B, -1);
}



/*
 * Formatted output for IOH.
 * This function is only safe for C99 compliant (glibc 2.1+) compilers
 *
 * THREADS: MT-SAFE (assuming different ioh)
 * THREADS: THREAD-REENTRANT (otherwise)
 *
 * @param B BAKA Thread/global state
 * @param ioh IOH for output
 * @param format The format string to interpret in printf style
 * @param ... printf style arguments
 * @return <i>0</i> on success<br>
 * @return <i>negative</i> on failure (including queue too full)
 */
extern int bk_ioh_printf(bk_s B, struct bk_ioh *ioh, const char *format, ...)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  va_list args;
  bk_vptr *data = NULL;                        // local buffer
  bk_vptr *send_data = NULL;                   // to pass to bk_ioh_write
  int32_t ret;                                 // temp storage for return vals

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "invalid arguments\n");
    BK_RETURN(B, -1);
  }

  if (!BK_CALLOC(data))
  {
    bk_error_printf(B, BK_ERR_ERR, "Calloc failed\n");
    goto error;
  }

  // allocate space
  data->len = MinBufSize;
  data->ptr = malloc(MinBufSize);
  if (!data->ptr)
  {
    bk_error_printf(B, BK_ERR_ERR, "Malloc failed\n");
    goto error;
  }

  // try to print
  va_start(args, format);
  ret = vsnprintf(data->ptr, 0, format,args);
  va_end(args);

  if (ret < 0)
  {
    // unrecoverable error
    bk_error_printf(B, BK_ERR_ERR, "vsnprintf failed\n");
    goto error;
  }
  else if (ret > 0)
  {
    // we just didn't allocate enough space
    // re-allocate space
    free(data->ptr);
    data->ptr = malloc(ret + 1);               // leave space for NULL
    if (!data->ptr)
    {
      bk_error_printf(B, BK_ERR_ERR, "Malloc failed\n");
      goto error;
    }

    // write the buffer again
    va_start(args, format);
    vsnprintf(data->ptr, ret + 1, format, args);
    va_end(args);
  }

  if (!BK_CALLOC(send_data) || !BK_MALLOC_LEN(send_data->ptr, ret))
  {
    bk_error_printf(B, BK_ERR_ERR, "memory allocation failed\n");
    goto error;
  }

  memcpy(send_data->ptr, data->ptr, ret);
  send_data->len = ret;

  free(data->ptr);
  free(data);
  data = NULL;

  if (bk_ioh_write(B, ioh, send_data, 0) != 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "IOH write failed\n");
    goto error;
  }
  send_data = NULL;

  BK_RETURN(B, 0);

 error:
  if (data)
  {
    if (data->ptr)
    {
      free(data->ptr);
    }
    free(data);
  }

  if (send_data)
  {
    if (send_data->ptr)
    {
      free(send_data->ptr);
    }
    free(send_data);
  }

  BK_RETURN(B, -1);
}



/**
 * Event queue job to ask for the user IOH queue to be drained of any
 * pending input
 *
 * THREADS: MT-SAFE (assuming different ioh)
 * THREADS: THREAD-REENTRANT (otherwise)
 *
 * @param B BAKA Thread/global environment
 * @param run Run environment
 * @param opaque Private data
 * @param starttime When this event queue run started
 * @param flags Fun for the future
 */
static void bk_ioh_userdrainevent(bk_s B, struct bk_run *run, void *opaque, const struct timeval starttime, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ioh *ioh = opaque;

  if (!run || !ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  ioh->ioh_readallowedevent = NULL;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  ioh_runhandler(B, run, BK_RUN_USERFLAG1, ioh->ioh_fdin, ioh, &starttime);

  BK_VRETURN(B);
}




/**
 * Set various and sundry "extras" available to the ioh.
 *
 * NB Not everything may be fully supported
 *
 * THREADS: MT-SAFE (assuming different ioh)
 * THREADS: THREAD-REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param ioh The @a bk_ioh to use.
 *	@param int compression_level The compression level to use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_ioh_stdio_init(bk_s B, struct bk_ioh *ioh, int compression_level, int auth_alg, bk_vptr auth_key, char *auth_name , int encrypt_alg, bk_vptr encrypt_key, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (compression_level)
  {
#ifdef IOH_COMPRESS_SUPPORT
    if (ioh->ioh_compress_level && ioh->ioh_compress_level != compression_level)
    {
      bk_error_printf(B, BK_ERR_ERR, "Compression cannot be changed once set\n");
      goto error;
    }
    ioh->ioh_compress_level = compression_level;
    if (BK_FLAG_ISCLEAR(ioh->ioh_extflags, BK_IOH_BLOCKED))
    {
      BK_FLAG_CLEAR(ioh->ioh_extflags, BK_IOH_RAW | BK_IOH_VECTORED | BK_IOH_LINE);
      BK_FLAG_SET(ioh->ioh_extflags, BK_IOH_BLOCKED);
    }
    if (!ioh->ioh_inbuf_hint)
      ioh->ioh_inbuf_hint = IOH_COMPRESS_BLOCK_SIZE;
#else
    bk_error_printf(B, BK_ERR_ERR, "Compression is not enabled - recompile\n");
    goto error;
#endif
  }
  else if (ioh->ioh_compress_level)
  {
    bk_error_printf(B, BK_ERR_ERR, "Compression cannot be changed once set\n");
    goto error;
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B,0);

 error:
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B,-1);
}




/**
 * Register an ioh for cancellation. NB: this only does input.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param ioh The ioh to register.
 *	@param flags Flags passed though.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_ioh_cancel_register(bk_s B, struct bk_ioh *ioh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  BK_RETURN(B,bk_run_fd_cancel_register(B, ioh->ioh_run, ioh->ioh_fdin, flags));
}




/**
 * Unregister an ioh for cancellation. NB: this only does input.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param ioh The ioh to unregister.
 *	@param flags Flags passed through.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_ioh_cancel_unregister(bk_s B, struct bk_ioh *ioh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  BK_RETURN(B,bk_run_fd_cancel_unregister(B, ioh->ioh_run, ioh->ioh_fdin, flags));
}




/**
 * Check to see if an ioh has been canceled.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param ioh The @a bk_ioh to use.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success and <b>not</b> registered.
 *	@return <i>1</i> on success and registered.
 */
int
bk_ioh_is_canceled(bk_s B, struct bk_ioh *ioh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  BK_RETURN(B,bk_run_fd_is_canceled(B, ioh->ioh_run, ioh->ioh_fdin) || bk_run_fd_is_closed(B, ioh->ioh_run, ioh->ioh_fdin));
}



/**
 * Cancel an ioh
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param ioh The @a bk_ioh to cancel.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_ioh_cancel(bk_s B, struct bk_ioh *ioh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  BK_RETURN(B,bk_run_fd_cancel(B, ioh->ioh_run, ioh->ioh_fdin, flags));
}



/**
 * Get the last errno from this ioh.  This would be a macro
 * excpet that the ioh internals are private to libbk.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param ioh The @a bk_ioh to check.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>errno</i> on success.
 */
int
bk_ioh_last_error(bk_s B, struct bk_ioh *ioh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Internal Error!\n");
    BK_RETURN(B, -1);
  }

  BK_RETURN(B, ioh->ioh_errno);
}




/**
 * Check if an ioh in follow mode is at the end of file. If so, remove it from
 * the read set, and enqueue an event to check again after a short pause. If
 * it's not at the end of the file insert it in the read set (yikes!) and
 * update the ioh stat info.
 *
 * THREADS: REENTRANT (ioh must already be locked)
 *
 *	@param B BAKA thread/global state.
 *	@param ioh The ioh to check.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static void
check_follow(bk_s B, struct bk_ioh *ioh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct stat st;
  int iscancelled;

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  // <WARNING> make sure the follow flag is set for this ioh before calling check_follow()</WARNING>
  /*
   * If the former size of the file is equal to the our location int the
   * stream look for file growth.
   */
  if (ioh->ioh_size == ioh->ioh_tell)
  {
    if (fstat(ioh->ioh_fdin, &st) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not stat input file descriptor: %s\n", strerror(errno));
      goto error;
    }

    ioh->ioh_size = st.st_size;
  }

  iscancelled = bk_ioh_is_canceled(B, ioh, 0) || bk_run_fd_is_closed(B, ioh->ioh_run, ioh->ioh_fdin);
  if (ioh->ioh_size == ioh->ioh_tell && !iscancelled)
  {
    // Wtihdraw from read set
    bk_debug_printf_and(B,1,"Checking if read allowed for follow...NO (cancelled %d/%p)\n", iscancelled, ioh);

    if (bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdin, 0, BK_RUN_WANTREAD, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not withdraw fdin from read set\n");
      goto error;
    }

    // Enqueue event to recheck after a short delay.
    if (bk_run_enqueue_delta(B, ioh->ioh_run, BK_SECS_TO_EVENT(ioh->ioh_follow_pause), recheck_follow, ioh, &ioh->ioh_recheck_event, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not enqueue event to recheck the file size in follow mode\n");
      goto error;
    }
  }
  else
  {
    // Allow reads again.
    bk_debug_printf_and(B,1,"Checking if read allowed for follow...YES (cacnelled %d)\n", iscancelled);

    if (bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdin, 1, BK_RUN_WANTREAD, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not insert fdin into read set\n");
      goto error;
    }
  }

 error:
  BK_VRETURN(B);
}





/**
 * Event handler to recheck the size of the follow in follow mode.
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static void
recheck_follow(bk_s B, struct bk_run *run, void *opaque, const struct timeval starttime, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ioh *ioh = (struct bk_ioh *)opaque;

  if (!run || !ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  ioh->ioh_recheck_event = NULL;

  check_follow(B, ioh, 0);

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&ioh->ioh_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_VRETURN(B);
}




/**
 * Check whether data seizing (typically to avoid read side copies by the
 * consumer) is allowed on this ioh.
 *
 *	@param B BAKA thread/global state.
 *	@param ioh. The @a bk_ioh to check
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success and data may not be seized.
 *	@return <i>1</i> on success and data may be seized.
 */
int
bk_ioh_data_seize_permitted(bk_s B, struct bk_ioh *ioh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  BK_RETURN(B,BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_RAW | BK_IOH_VECTORED | BK_IOH_BLOCKED));
}
