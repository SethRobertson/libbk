#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_ioh.c,v 1.31 2001/12/12 00:57:34 jtt Exp $";
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
 * Association of file descriptor/handle to callback.  Queue data for
 * output, translate input stream into messages.
 */

#include <libbk.h>
#include "libbk_internal.h"



#if defined(EWOULDBLOCK) && defined(EAGAIN)
#define IOH_EBLOCKING (errno == EWOULDBLOCK || errno == EAGAIN) ///< real error or just normal behavior?
#else
#if defined(EWOULDBLOCK)
#define IOH_EBLOCKING (errno == EWOULDBLOCK)	///< real error or just normal behavior?
#else
#if defined(EAGAIN)
#define IOH_EBLOCKING (errno == EAGAIN)		///< real error or just normal behavior?
#else
#define IOH_EBLOCKING 0				///< real error or just normal behavior?
#endif
#endif
#endif


#define CALLBACK(B, ioh, data, state)								\
 do												\
 {												\
   BK_FLAG_SET((ioh)->ioh_intflags, IOH_FLAGS_IN_CALLBACK);					\
   bk_debug_printf_and(B, 2, "Calling user callback for ioh %p with state %d\n",(ioh),(state));	\
   ((*((ioh)->ioh_handler))((B),(data), (ioh)->ioh_opaque, (ioh), (state)));			\
   BK_FLAG_CLEAR((ioh)->ioh_intflags, IOH_FLAGS_IN_CALLBACK);					\
 } while (0)					///< Function to evaluate user callback with new data/state information

#define IOH_DEFAULT_DATA_SIZE	128		///< Default read size (optimized for user and protocol traffic, not bulk data transfer)
#define IOH_VS			2		///< Number of vectors to hold length and msg
#define IOH_EOLCHAR		'\n'		///< End of line character (for line oriented mode--change to m



/**
 * Information about a chunk of user data in use by the ioh subsystem
 */
struct bk_ioh_data
{
  char		       *bid_data;		///< Actual data
  u_int32_t		bid_allocated;		///< Allocated size of data
  u_int32_t		bid_inuse;		///< Amount actually used (!including consumed)
  u_int32_t		bid_used;		///< Amount virtually consumed (written or sent to user)
  bk_vptr	       *bid_vptr;		///< Stored vptr to return in callback
  bk_flags		bid_flags;		///< Additional information about this dataa
#define BID_FLAG_SHUTDOWN	0x01		///< When this mark is reached, shut everything down
#define BID_FLAG_CLOSE		0x02		///< When this mark is reached, close everything down
#define BID_FLAG_MESSAGE	0x04		///< This is a message boundry
};



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
  bk_iorfunc 		ioh_readfun;		///< Function to read data
  bk_iowfunc		ioh_writefun;		///< Function to write data
  void		       *ioh_iofunopaque;	///< Opaque data for iofuns
  bk_iohhandler		ioh_handler;		///< Callback function for event handling
  void		       *ioh_opaque;		///< Opaque data for handler
  u_int32_t		ioh_inbuf_hint;		///< Hint for input buffer sizes
  struct bk_run	       *ioh_run;		///< BK_RUN environment
  struct bk_ioh_queue	ioh_readq;		///< Data queued for reading
  struct bk_ioh_queue	ioh_writeq;		///< Data queued for writing
  char			ioh_eolchar;		///< End of line character
  bk_flags		ioh_deferredclosearg;	///< Flags argument to deferred close
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
#define IOH_FLAGS_CLOSE_PENDING		0x200	///< We want to close, but are in a user callback
#define IOH_FLAGS_IN_CALLBACK		0x400	///< We are in a user callback
};



static int ioh_dequeue_byte(bk_s B, struct bk_ioh *ioh, struct bk_ioh_queue *iohq, u_int32_t bytes, bk_flags flags);
static int ioh_dequeue(bk_s B, struct bk_ioh *ioh, struct bk_ioh_queue *iohq, struct bk_ioh_data *bid, bk_flags flags);
#define IOH_DEQUEUE_ABORT		0x01	///< Tell user data is aborted
static int ioh_getlastbuf(bk_s B, struct bk_ioh_queue *queue, u_int32_t *size, char **data, struct bk_ioh_data **bid, bk_flags flags);
static int ioh_internal_read(bk_s B, struct bk_ioh *ioh, int fd, char *data, size_t len, bk_flags flags);
static void ioh_sendincomplete_up(bk_s B, struct bk_ioh *ioh, u_int32_t filter, bk_flags flags);
static int ioh_execute_ifspecial(bk_s B, struct bk_ioh *ioh, struct bk_ioh_queue *queue, bk_flags flags);
static int ioh_execute_cmds(bk_s B, struct bk_ioh *ioh, u_int32_t cmds, bk_flags flags);
static void ioh_flush_queue(bk_s B, struct bk_ioh *ioh, struct bk_ioh_queue *queue, u_int32_t *cmd, bk_flags flags);
#define IOH_FLUSH_DESTROY	1		///< Notify that queue is being destroyed
static int ioh_queue(bk_s B, struct bk_ioh_queue *iohq, char *data, u_int32_t allocated, u_int32_t inuse, u_int32_t used, bk_vptr *vptr, bk_flags msgflags, bk_flags flags);
static void ioh_runhandler(bk_s B, struct bk_run *run, int fd, u_int gottypes, void *opaque, const struct timeval *starttime);
static int bk_ioh_fdctl(bk_s B, int fd, u_int32_t *savestate, bk_flags flags);
#define IOH_FDCTL_SET		1		///< Set the fd set to the ioh normal version
#define IOH_FDCTL_RESET		1		///< Set the fd set to the original defaults
static void bk_ioh_destroy(bk_s B, struct bk_ioh *ioh);



/**
 * @name IOH message type queueing functions
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
#define biq_create(o,k,f,a)		dll_create(o,k,f)
#define biq_destroy(h)			dll_destroy(h)
#define biq_insert(h,o)			dll_insert(h,o)
#define biq_insert_uniq(h,n,o)		dll_insert_uniq(h,n,o)
#define biq_append(h,o)			dll_append(h,o)
#define biq_append_uniq(h,n,o)		dll_append_uniq(h,n,o)
#define biq_search(h,k)			dll_search(h,k)
#define biq_delete(h,o)			dll_delete(h,o)
#define biq_minimum(h)			dll_minimum(h)
#define biq_maximum(h)			dll_maximum(h)
#define biq_successor(h,o)		dll_successor(h,o)
#define biq_predecessor(h,o)		dll_predecessor(h,o)
#define biq_iterate(h,d)		dll_iterate(h,d)
#define biq_nextobj(h,i)		dll_nextobj(h,i)
#define biq_iterate_done(h,i)		dll_iterate_done(h,i)
#define biq_error_reason(h,i)		dll_error_reason(h,i)
// @}



/**
 * Create and initialize the ioh environment.
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
struct bk_ioh *bk_ioh_init(bk_s B, int fdin, int fdout, bk_iohhandler handler, void *opaque, u_int32_t inbufhint, u_int32_t inbufmax, u_int32_t outbufmax, struct bk_run *run, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ioh *curioh = NULL;
  int tmp;
  bk_iorfunc readfun = bk_ioh_stdrdfun;
  bk_iowfunc writefun = bk_ioh_stdwrfun;

  if ((fdin < 0 && fdout < 0) || !run)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!handler)
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


  // Check for invalid flags combinations
  tmp = 0;
  if (BK_FLAG_ISSET(flags, BK_IOH_STREAM)) tmp++;

  if (tmp != 1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Must have exactly one input type in flags (STREAM)\n");
    BK_RETURN(B, NULL);
  }

  if (!BK_CALLOC(curioh))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate ioh structure: %s\n",strerror(errno));
    BK_RETURN(B, NULL);
  }

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
  curioh->ioh_handler = handler;
  curioh->ioh_opaque = opaque;
  curioh->ioh_inbuf_hint = inbufhint?inbufhint:IOH_DEFAULT_DATA_SIZE;
  curioh->ioh_readq.biq_queuemax = inbufmax;
  curioh->ioh_writeq.biq_queuemax = outbufmax;
  curioh->ioh_run = run;
  curioh->ioh_extflags = flags;
  curioh->ioh_eolchar = IOH_EOLCHAR;

  curioh->ioh_fdin = fdin;
  if (curioh->ioh_fdin >= 0)
  {
    if (bk_run_handle(B, curioh->ioh_run, curioh->ioh_fdin, ioh_runhandler, curioh, BK_RUN_WANTREAD, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not put this new read ioh into the bk_run environment\n");
      goto error;
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
      if (bk_run_handle(B, curioh->ioh_run, curioh->ioh_fdout, ioh_runhandler, curioh, 0, 0) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not put this new write ioh into the bk_run environment\n");
	goto error;
      }

      // Examine file descriptor for proper file and socket options
      bk_ioh_fdctl(B, curioh->ioh_fdout, &curioh->ioh_fdout_savestate, IOH_FDCTL_SET);
    }
  }
  else
  {
    BK_FLAG_SET(curioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_OUTPUT);
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
    free(curioh);
  }
  BK_RETURN(B, NULL);
}



/**
 * Update various configuration parameters of a IOH instance
 *
 *	@param B BAKA thread/global state 
 *	@param ioh The IOH environment to update
 *	@param readfun The function to use to read data.
 *	@param writefun The function to use to write data.
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
 *	@return <br><i>0</i> on success.
 */
int bk_ioh_update(bk_s B, struct bk_ioh *ioh, bk_iorfunc readfun, bk_iowfunc writefun, void *iofunopaque, bk_iohhandler handler, void *opaque, u_int32_t inbufhint, u_int32_t inbufmax, u_int32_t outbufmax, bk_flags flags, bk_flags updateflags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
 
  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  bk_debug_printf_and(B, 1, "Updating IOH parameters %p\n",ioh);

  if (BK_FLAG_ISSET(updateflags, BK_IOH_UPDATE_READFUN))
    ioh->ioh_readfun = readfun;
  if (BK_FLAG_ISSET(updateflags, BK_IOH_UPDATE_WRITEFUN))
    ioh->ioh_writefun = writefun;
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

  BK_RETURN(B, 0);
}



/**
 * Request various configuration parameters of a IOH instance.
 *
 *	@param B BAKA thread/global state 
 *	@param ioh The IOH environment to update
 *	@param fdin The file descriptor to read from.  -1 if no input is desired.
 *	@param fdout The file descriptor to write to.  -1 if no output is desired.
 *	@param readfun The function to use to read data.
 *	@param writefun The function to use to write data.
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
int bk_ioh_get(bk_s B, struct bk_ioh *ioh, int *fdin, int *fdout, bk_iorfunc *readfun, bk_iowfunc *writefun, void **iofunopaque, bk_iohhandler *handler, void **opaque, u_int32_t *inbufhint, u_int32_t *inbufmax, u_int32_t *outbufmax, struct bk_run **run, bk_flags *flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
 
  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  bk_debug_printf_and(B, 1, "Obtaining IOH parameters %p\n",ioh);

  if (fdin) *fdin = ioh->ioh_fdin;
  if (fdout) *fdout = ioh->ioh_fdout;
  if (readfun) *readfun = ioh->ioh_readfun;
  if (writefun) *writefun = ioh->ioh_writefun;
  if (iofunopaque) *iofunopaque = ioh->ioh_iofunopaque;
  if (handler) *handler = ioh->ioh_handler;
  if (opaque) *opaque = ioh->ioh_opaque;
  if (inbufhint) *inbufhint = ioh->ioh_inbuf_hint;
  if (inbufmax) *inbufmax = ioh->ioh_readq.biq_queuemax;
  if (outbufmax) *outbufmax = ioh->ioh_readq.biq_queuemax;
  if (run) *run = ioh->ioh_run;
  if (flags) *flags = ioh->ioh_extflags;

  BK_RETURN(B, 0);
}



/**
 * Enqueue data for output, if allowed.
 *
 *	@param B BAKA thread/global state 
 *	@param ioh The IOH environment to update
 *	@param data The data to be sent
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
    CALLBACK(B, ioh, data, BK_IOH_STATUS_WRITEABORTED);
    BK_RETURN(B, -1);
  }

  bk_debug_printf_and(B, 1, "Writing %d bytes to IOH %p\n",data->len, ioh);

  if (BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_CLOSING | IOH_FLAGS_SHUTDOWN_DESTROYING | IOH_FLAGS_ERROR_OUTPUT | IOH_FLAGS_SHUTDOWN_OUTPUT | IOH_FLAGS_SHUTDOWN_OUTPUT_PEND))
  {
    bk_error_printf(B, BK_ERR_ERR, "Cannot write after shutdown/close\n");
    BK_RETURN(B, -1);
  }

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
    CALLBACK(B, ioh, data, BK_IOH_STATUS_WRITEABORTED);
    BK_RETURN(B, -1);
  }

  if (ret < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not append user data to outgoing message queue\n");
    CALLBACK(B, ioh, data, BK_IOH_STATUS_WRITEABORTED);
    BK_RETURN(B, -1);
  }

  // <TODO>Consider trying a spontaneous write here, if nbio is set, especially if writes are not pending </TODO>

  BK_RETURN(B, ret);
}



/**
 * Tell the system that no futher system I/O will be permitted.
 *
 * If data is currently queued for input, this data will sent to the user as incomplete.
 * If data is currented queued for output, this data will be drained before the shutdown takes effect.
 * See @a bk_ioh_flush for a way to avoid this.
 *
 *	@param B BAKA thread/global state 
 *	@param ioh The IOH environment to update
 *	@param how -- SHUT_RD to shut down reads, SHUT_WR to shut down writes, SHUT_RDWR for both.
 *	@param flags Future expansion
 *	@see bk_ioh_flush
 *	@return <i>-1</i> on call failure or subsystem refusal
 *	@return <br><i>0</i> on success
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

  bk_debug_printf_and(B, 1, "Starting shutdown %d of IOH %p\n", how, ioh);

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

  if (how == SHUT_RD || how == SHUT_RDWR)
  {
    ioh_sendincomplete_up(B, ioh, BID_FLAG_MESSAGE, 0);
    shutdown(ioh->ioh_fdin, SHUT_RD);
  }

  if (how == SHUT_WR || how == SHUT_RDWR)
  {
    if ((ret = ioh_queue(B, &ioh->ioh_writeq, NULL, 0, 0, 0, NULL, BID_FLAG_SHUTDOWN, 0)) < 0)
      shutdown(ioh->ioh_fdin, SHUT_WR);
  }

  ret = ioh_execute_ifspecial(B, ioh, &ioh->ioh_writeq, 0); // Execute shutdown immediately if queue was empty

  if (ret == 2)
    BK_VRETURN(B);				// IOH was destroyed

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

  if (ioh->ioh_fdin >= 0 && BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_INPUT|IOH_FLAGS_ERROR_INPUT))
    bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdin, 0, BK_RUN_WANTREAD, 0); // Clear read
  if (ioh->ioh_fdout >= 0 && BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_OUTPUT|IOH_FLAGS_ERROR_OUTPUT))
    bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdout, 0, BK_RUN_WANTWRITE, 0); // Clear write

  if (BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_CLOSE_PENDING))
  {
    BK_FLAG_CLEAR(ioh->ioh_intflags, IOH_FLAGS_CLOSE_PENDING);
    bk_ioh_close(B, ioh, ioh->ioh_deferredclosearg);
    BK_VRETURN(B);
  }

  BK_VRETURN(B);
}



/**
 * Flush data queued in the ioh.
 *
 * Data may be queued for input or output.
 *
 *	@param B BAKA thread/global state 
 *	@param ioh The IOH environment to update
 *	@param how -- SHUT_RD to flush input, SHUT_WR to flush output, SHUT_RDWR for both.
 *	@param flags Future expansion
 *	@return <i>-1</i> on call failure or subsystem refusal
 *	@return <br><i>0</i> on success
 */
void bk_ioh_flush(bk_s B, struct bk_ioh *ioh, int how, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int cmds = 0;
 
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

  if (how == SHUT_RD || how == SHUT_RDWR)
  {
    ioh_flush_queue(B, ioh, &ioh->ioh_readq, NULL, 0);
  }
  if (how == SHUT_WR || how == SHUT_RDWR)
  {
    ioh_flush_queue(B, ioh, &ioh->ioh_writeq, &cmds, 0);
  }

  if (cmds)
    ioh_execute_cmds(B, ioh, cmds, 0);

  BK_VRETURN(B);
}



/**
 * Control whether or not new input will be accepted from the system.
 *
 * Note there may be incomplete data on the input queue which will remain pending.
 *
 *	@param B BAKA thread/global state 
 *	@param ioh The IOH environment to update
 *	@param isallowed Zero if no reads desired, non-zero if reads allowed
 *	@param flags Future expansion
 *	@return <i>-1</i> on call failure or subsystem refusal
 *	@return <br><i>0</i> on success
 */
void bk_ioh_readallowed(bk_s B, struct bk_ioh *ioh, int isallowed, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
 
  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  bk_debug_printf_and(B, 1, "Setting read state to %d of IOH %p\n", isallowed, ioh);

  if (ioh->ioh_fdin < 0 || BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_INPUT | IOH_FLAGS_ERROR_INPUT | IOH_FLAGS_SHUTDOWN_DESTROYING))
  {
    bk_error_printf(B, BK_ERR_WARN, "You cannot manipulate the read desireability if input is technically impossible\n");
    BK_VRETURN(B);
  }

  // Set new preference
  if (bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdin, isallowed?BK_RUN_WANTREAD:0, BK_RUN_WANTREAD, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Cannot set I/O preferences\n");
    BK_VRETURN(B);
  }

  BK_VRETURN(B);
}



/**
 * Indicate that no further activity is desired on this ioh.
 *
 * The ioh may linger if data requires draining, unless abort.
 * Incomplete data pending on the input queue will be sent to user as incomplete unless abort.
 * Additional callbacks may occur--WRITECOMPLETEs or IOHWRITEERRORs
 *  (if no abort), WRITEABORTEDs (if abort), IOHCLOSING (if
 *  NOTIFYANYWAY)
 *
 *	@param B BAKA thread/global state 
 *	@param ioh The IOH environment to update
 *	@param flags BK_IOH_(DONTCLOSEFDS to prevent close() from being executed on the fds,
 *			ABORT to cause automatic flush of input and output queues (to prevent
 *			indefinite wait for output to drain), NOTIFYANYWAY to cause user to be
 *			notified of this close()).
 */
void bk_ioh_close(bk_s B, struct bk_ioh *ioh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret = -1;
  int cmds = 0;
 
  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  bk_debug_printf_and(B, 1, "Closing with flags %x ioh IOH %p/%x\n", flags, ioh, ioh->ioh_intflags);
  if (bk_debug_and(B, 0x10))
  {
    bk_debug_print(B, "Printing stack trace\n");
    bk_fun_trace(B, stderr, BK_ERR_NONE, 0);
  }

  if (BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_IN_CALLBACK))
  {
    BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_CLOSE_PENDING);
    ioh->ioh_deferredclosearg = flags;
    BK_VRETURN(B);
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
    BK_VRETURN(B);
  }

  BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_CLOSING);

  if (BK_FLAG_ISSET(flags, BK_IOH_ABORT))
    bk_ioh_flush(B, ioh, SHUT_RDWR, 0);

  ioh_sendincomplete_up(B, ioh, BID_FLAG_MESSAGE, 0);

  BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_INPUT|IOH_FLAGS_SHUTDOWN_OUTPUT_PEND);

  // Queue up a close command after any pending data commands
  if (ioh_queue(B, &ioh->ioh_writeq, NULL, 0, 0, 0, NULL, BID_FLAG_CLOSE, 0) < 0)
  {
    ioh_flush_queue(B, ioh, &ioh->ioh_writeq, NULL, 0);
    cmds = BID_FLAG_CLOSE;
  }

  // Execute close if it is the only thing on the output queue
  if (cmds)
    ret = ioh_execute_cmds(B, ioh, cmds, 0);
  else
    ret = ioh_execute_ifspecial(B, ioh, &ioh->ioh_writeq, 0);

  if (ret == 2)
  {
    // IOH has been destroyed
    BK_VRETURN(B);
  }

  // Propagate close to RUN level
  if (ioh->ioh_fdin >= 0 && BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_INPUT|IOH_FLAGS_ERROR_INPUT))
    bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdin, 0, BK_RUN_WANTREAD, 0); // Clear read
  if (ioh->ioh_fdout >= 0 && BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_OUTPUT|IOH_FLAGS_ERROR_OUTPUT))
    bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdout, 0, BK_RUN_WANTWRITE, 0); // Clear write

  if (BK_FLAG_ISSET(flags, BK_IOH_ABORT) || ret < 1)
    bk_ioh_destroy(B, ioh);

  BK_VRETURN(B);
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
 * jtt made this static since noone outside this file was using it and
 * you're really not supposed to. Seth is dubious; he feels that it just
 * <em>might</em> happen that someone will get in a weird state where they
 * <em>really</em> need to get rid of their ioh without getting called back
 * or anything. So you may make this unstatic if you like, but it might
 * nice to document the reason why.
 * </WARNING>
 *
 *	@param B BAKA thread/global state 
 *	@param ioh The IOH environment to update
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

  // Remove from RUN level
  if (ioh->ioh_fdin >= 0)
    bk_run_close(B, ioh->ioh_run, ioh->ioh_fdin, 0);
  if (ioh->ioh_fdout >= 0 && ioh->ioh_fdout != ioh->ioh_fdin)
    bk_run_close(B, ioh->ioh_run, ioh->ioh_fdout, 0);

  if (BK_FLAG_ISCLEAR(ioh->ioh_intflags, IOH_FLAGS_DONTCLOSEFDS))
  {						// Close FDs if we can
    if (ioh->ioh_fdin >= 0)
      close(ioh->ioh_fdin);

    if (ioh->ioh_fdout >= 0 && ioh->ioh_fdout != ioh->ioh_fdin)
      close(ioh->ioh_fdout);
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
  CALLBACK(B, ioh, NULL, BK_IOH_STATUS_IOHCLOSING);

  free(ioh);

  bk_debug_printf_and(B, 1, "IOH %p is now gone\n", ioh);

  BK_VRETURN(B);
}



/**
 * Get length of IOH queues.  Note that this may only have a passing
 * resemblence to the amount of user data that can still be placed in
 * the queue, the amount that was placed in the queue, or any other
 * version thereof.  The message types may compress or expand the
 * number of bytes which get enqueued for transmission.
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

  if (inqueue)
    *inqueue = ioh->ioh_readq.biq_queuelen;
  if (outqueue)
    *outqueue = ioh->ioh_writeq.biq_queuelen;

  BK_RETURN(B, 0);
}



/**
 * Run's interface into the IOH.  The callback which it calls when activity was referenced.
 *
 *	@param B BAKA Thread/global state
 *	@param run Handle into run environment
 *	@param fd File descriptor which had the activity
 *	@param gottypes Description of activity seen
 *	@param opaque The ioh we passed in previosly
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
  size_t room = 0;
  int ret = 0;
  struct bk_ioh_data *bid;
 
  if (!opaque)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  bk_debug_printf_and(B, 1, "Received run notificaiton of %x ready for IOH %p/%x\n", gottypes, ioh, ioh->ioh_intflags);

  // Check for error or exceptional conditions
  if (BK_FLAG_ISSET(gottypes, BK_RUN_DESTROY) || BK_FLAG_ISSET(gottypes, BK_RUN_CLOSE))
  {
    bk_ioh_close(B, ioh, BK_IOH_ABORT);
    BK_VRETURN(B);
  }

  // Don't do anything if we are shut down. This is weird and probaby shouldn't have happened but whatever...
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

  // Write first to hopefully free memory for read if necessary
  if (ret >= 0 && BK_FLAG_ISSET(gottypes, BK_RUN_WRITEREADY))
  {
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
  }

  // Time for reading
  if (ret >= 0 && BK_FLAG_ISSET(gottypes, BK_RUN_READREADY))
  {
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

      if (ret < 0 && IOH_EBLOCKING)
      {
	// Not ready after all.  Do nothing.
	ret = 0;				// Don't claim we failed
      }
      else if (ret < 0)
      {
	// Error
	ioh_sendincomplete_up(B, ioh, BID_FLAG_MESSAGE, 0);
	CALLBACK(B, ioh, NULL, BK_IOH_STATUS_IOHREADERROR);
	BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_ERROR_INPUT);
	bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdout, 0, BK_RUN_WANTREAD, 0); // Clear read from select
      }
      else if (ret == 0)
      {
	// EOF
	ioh_sendincomplete_up(B, ioh, BID_FLAG_MESSAGE, 0);
	CALLBACK(B, ioh, NULL, BK_IOH_STATUS_IOHREADEOF);
	BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_ERROR_INPUT); // This is a little bogus.....
	bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdout, 0, BK_RUN_WANTREAD, 0); // Clear read from select
      }
      else
      {
	// Got ret bytes
	bid->bid_inuse += ret;
	ioh->ioh_readq.biq_queuelen += ret;

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
      }
    }
  }

  if (BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_CLOSE_PENDING))
  {
    BK_FLAG_CLEAR(ioh->ioh_intflags, IOH_FLAGS_CLOSE_PENDING);
    bk_ioh_close(B, ioh, ioh->ioh_deferredclosearg);
    BK_VRETURN(B);
  }

  if (ret >= 0)
    ret = ioh_execute_ifspecial(B, ioh, &ioh->ioh_writeq, 0);
      

  if (ret < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Message type handler failed severely\n");
    bk_ioh_close(B, ioh, BK_IOH_ABORT);
    BK_VRETURN(B);
  }

  BK_VRETURN(B);
}



/**
 * Get or set standard IOH file/network options.  Normally sets
 * O_NONBLOCK (nonblocking), OOBINLINE (urgent data is treated
 * normally), -SOLINGER (shutdown/close do not block).
 *
 *	@param B BAKA Thread/global state
 *	@param fd File descriptor to set or reset
 *	@param savestate Copy-in or copy-out (depending on mode) of
 *			 what options need reset, or were set
 *	@param flags IOH_FDCTL_SET -- set standard fd options.
 *		     IOH_FDCTL_RESET -- reset to original defaults.
 *	@return <i>-1</i> on call failure
 *	@return <br><i>0</i> no changes made
 *	@return <br><i>> 0</i> some changes made
 */
static int bk_ioh_fdctl(bk_s B, int fd, u_int32_t *savestate, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret = 0;
  int fdflags, oobinline, linger, size;

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

  // Examine file descriptor for proper file and socket options
  if ((fdflags = fcntl(fd, F_GETFL, NULL)) < 0)
    fdflags = -1;
  size = sizeof(oobinline);
  if (getsockopt(fd, SOL_SOCKET, SO_OOBINLINE, &oobinline, &size) < 0)
    oobinline = -1;
  size = sizeof(linger);
  if (getsockopt(fd, SOL_SOCKET, SO_LINGER, &linger, &size) < 0)
    linger = -1;


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
      // We need to save the linger value--some OSs overload this information.
      // Bound it to 2^16-1, store in upper 16 bits of savestate;
      if (linger > 65535) linger = 65535;
      *savestate |= linger<<16;
      linger = 0;
      *savestate |= IOH_NUKED_LINGER;
    }
  }

  if (BK_FLAG_ISSET(flags,IOH_FDCTL_RESET))
  {
    if (BK_FLAG_ISSET(*savestate, IOH_ADDED_NONBLOCK) && fdflags >= 0 && BK_FLAG_ISSET(fdflags, O_NONBLOCK))
    {
      BK_FLAG_CLEAR(fdflags, O_NONBLOCK);
    }
    if (BK_FLAG_ISSET(*savestate, IOH_ADDED_OOBINLINE) && oobinline > 0)
    {
      oobinline = 0;
    }
    if (BK_FLAG_ISSET(*savestate, IOH_NUKED_LINGER) && linger == 0)
    {
      linger = (*savestate >> 16)&0xffff;
    }
  }

  // Examine file descriptor for proper file and socket options
  if (fdflags >= 0 && fcntl(fd, F_SETFL, &fdflags) >= 0)
    ret++;
  if (oobinline >= 0 && setsockopt(fd, SOL_SOCKET, SO_OOBINLINE, &oobinline, sizeof(oobinline)) >= 0)
    ret++;
  if (linger >= 0 && setsockopt(fd, SOL_SOCKET, SO_LINGER, &linger, sizeof(linger)) >= 0)
    ret++;

  BK_RETURN(B, ret);
}



/**
 * Remove a certain number of bytes from the queue in question
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
  dict_iter iter;
  struct bk_ioh_data *bid;

  if (!iohq)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_RETURN(B, -1);
  }

  bk_debug_printf_and(B, 1, "Dequeuing %d bytes for IOH queue %p\n", bytes, iohq);

  // Figure out what buffers have been fully written
  iter = biq_iterate(iohq->biq_queue, DICT_FROM_START);
  while ((bid = biq_nextobj(iohq->biq_queue, iter)) && bytes > 0)
  {	
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
  biq_iterate_done(iohq->biq_queue, iter);

  BK_RETURN(B, 0);
}



/**
 * Remove and destroy a bid from a queue
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
    CALLBACK(B, ioh, bid->bid_vptr, BK_FLAG_ISSET(flags, IOH_DEQUEUE_ABORT)?BK_IOH_STATUS_WRITEABORTED:BK_IOH_STATUS_WRITECOMPLETE);
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
static int ioh_queue(bk_s B, struct bk_ioh_queue *iohq, char *data, u_int32_t allocated, u_int32_t inuse, u_int32_t used, bk_vptr *vptr, bk_flags msgflags, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ioh_data *bid;

  if (!iohq)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  bk_debug_printf_and(B, 1, "Enqueuing data %p/%d/%d or flags %x for IOH queue %p(%d)\n", data, inuse, allocated, msgflags, iohq, iohq->biq_queuelen);

  if (BK_FLAG_ISCLEAR(flags, BK_IOH_BYPASSQUEUEFULL))
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

  if ((ret = ioh_queue(B, &ioh->ioh_writeq, data->ptr, data->len, data->len, 0, data, BID_FLAG_MESSAGE, flags)) == 0)
  {
    bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdout, BK_RUN_WANTWRITE, BK_RUN_WANTWRITE, 0);
  }

  BK_ORETURN(B, ret);
}



/**
 * Complete full block--IOH Type routines to queue data sent from user for output
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

  if ((ret = ioh_queue(B, &ioh->ioh_writeq, data->ptr, data->len, data->len, 0, data, BID_FLAG_MESSAGE, flags)) == 0)
  {
    /*
     * Yes, there may be blocks already partially sent, but if this condition
     * is true, then we are guarenteed that there is at least one block ready.
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

  // Do our own checks for queue size since we have two buffers which either both have to be on, or both off
  if (BK_FLAG_ISCLEAR(flags, BK_IOH_BYPASSQUEUEFULL))
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
  if (ioh_queue(B, &ioh->ioh_writeq, (char *)netdatalen, sizeof(*netdatalen), sizeof(*netdatalen), 0, NULL, 0, BK_IOH_BYPASSQUEUEFULL) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert vector length onto output stack\n");
    goto error;
  }

  if (ioh_queue(B, &ioh->ioh_writeq, data->ptr, data->len, data->len, 0, data, BID_FLAG_MESSAGE, BK_IOH_BYPASSQUEUEFULL) < 0)
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
 * Note that we do not enforce line message boundries on output.
 *
 * <TODO>Perhaps we *should* do "enforcement" in order to achive buffering.  Worry about UDP case as well.</TODO>
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
      int cnt = 0;
      struct iovec iov;

      for (bid = biq_minimum(ioh->ioh_writeq.biq_queue); bid; bid = biq_successor(ioh->ioh_writeq.biq_queue, bid))
      {	
	if (bid->bid_data)
	  break;
      }

      if (bid)
      {
	iov.iov_base = bid->bid_data + bid->bid_used;
	iov.iov_len = bid->bid_inuse;

	// <TODO>Perhaps attempt to write multiple buffers if available (here and elsewhere) </TODO>

	cnt = (*ioh->ioh_writefun)(B, ioh->ioh_opaque, ioh->ioh_fdout, &iov, 1, 0);

	if (cnt == 0 || cnt < 0 && IOH_EBLOCKING)
	{
	  // Not quite ready for writing yet
	  cnt = 0;
	}
	else if (cnt < 0)
	{
	  BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_ERROR_OUTPUT);
	  ioh_flush_queue(B, ioh, &ioh->ioh_writeq, NULL, 0);
	  CALLBACK(B, ioh, NULL, BK_IOH_STATUS_IOHWRITEERROR);
	}
	else
	{
	  ioh_dequeue_byte(B, ioh, &ioh->ioh_writeq, (u_int32_t)cnt, 0);

	  if (ioh->ioh_writeq.biq_queuelen < 1)
	  {
	    ioh->ioh_writeq.biq_queuelen = 0;
	    bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdout, 0, BK_RUN_WANTWRITE, 0);
	  }
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
	  bk_error_printf(B, BK_ERR_ERR, "Could not allocate input buffer for ioh %p of size %d: %s\n",ioh,size,strerror(errno));
	  BK_RETURN(B, -1);
	}

	if (ioh_queue(B, &ioh->ioh_readq, data, size, 0, 0, NULL, BID_FLAG_MESSAGE, BK_IOH_BYPASSQUEUEFULL) < 0)
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
      dict_iter iter;

      // Find out how many data segments we have
      iter = biq_iterate(ioh->ioh_readq.biq_queue, DICT_FROM_START);
      while (bid = biq_nextobj(ioh->ioh_readq.biq_queue, iter))
      {
	if (bid->bid_data && bid->bid_inuse > 0)
	  cnt++;
      }
      biq_iterate_done(ioh->ioh_readq.biq_queue, iter);

      if (!cnt)
	BK_RETURN(B,0);

      if (!BK_CALLOC_LEN(sendup,sizeof(*sendup)*(cnt+1)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not allocate data vectors to return data: %s\n", strerror(errno));
	BK_RETURN(B,-1);
      }
    
      // Actually fill out the data list
      cnt = 0;
      iter = biq_iterate(ioh->ioh_readq.biq_queue, DICT_FROM_START);
      while (bid = biq_nextobj(ioh->ioh_readq.biq_queue, iter))
      {
	if (bid->bid_data)
	{
	  sendup[cnt].ptr = bid->bid_data+bid->bid_used;
	  sendup[cnt].len = bid->bid_inuse;
	  cnt++;
	}
      }
      biq_iterate_done(ioh->ioh_readq.biq_queue, iter);

      CALLBACK(B, ioh, sendup, BK_IOH_STATUS_READCOMPLETE);

      // Nuke vector list
      free(sendup);

      // Nuke everything in the input queue, we have "used" the necessary stuff
      bk_ioh_flush(B, ioh, SHUT_RD, 0);
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
  dict_iter iter;

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

      cnt = (*ioh->ioh_writefun)(B, ioh->ioh_iofunopaque, ioh->ioh_fdout, iov, cnt, 0);
      free(iov);

      bk_debug_printf_and(B, 2, "Post-write, cnt %d, size %d\n", cnt, size);

      if (cnt == 0 || cnt < 0 && IOH_EBLOCKING)
      {
	// Not quite ready for writing yet
	cnt = 0;
	break;
      }
      else if (cnt < 0)
      {
	BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_ERROR_OUTPUT);
	ioh_flush_queue(B, ioh, &ioh->ioh_writeq, NULL, 0);
	CALLBACK(B, ioh, NULL, BK_IOH_STATUS_IOHWRITEERROR);
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

	if (ioh_queue(B, &ioh->ioh_readq, data, size, 0, 0, NULL, BID_FLAG_MESSAGE, BK_IOH_BYPASSQUEUEFULL) < 0)
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

      if (ioh->ioh_readq.biq_queuelen >= ioh->ioh_inbuf_hint)
      {						// We have enough data--get ready to send up

	size = cnt = 0;
	iter = biq_iterate(ioh->ioh_readq.biq_queue, DICT_FROM_START);
	while ((bid = biq_nextobj(ioh->ioh_readq.biq_queue, iter)) && size < ioh->ioh_inbuf_hint)
	{
	  if (bid->bid_data && bid->bid_inuse > 0)
	  {
	    cnt++;
	    size += bid->bid_inuse;
	  }
	}
	biq_iterate_done(ioh->ioh_readq.biq_queue, iter);

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
	size = cnt = 0;
	iter = biq_iterate(ioh->ioh_readq.biq_queue, DICT_FROM_START);
	while ((bid = biq_nextobj(ioh->ioh_readq.biq_queue, iter)) && size < ioh->ioh_inbuf_hint)
	{
	  if (bid->bid_data && bid->bid_inuse > 0)
	  {
	    sendup[cnt].ptr = bid->bid_data+bid->bid_used;
	    sendup[cnt].len = MIN(bid->bid_inuse,ioh->ioh_inbuf_hint - size);
	    size += sendup[cnt].len;
	    cnt++;
	  }
	}
	biq_iterate_done(ioh->ioh_readq.biq_queue, iter);

	CALLBACK(B, ioh, sendup, BK_IOH_STATUS_READCOMPLETE);

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
  u_int32_t room, size = 0;
  dict_iter iter;
  u_int32_t lengthfromwire = 0;
  int cnt = 0;

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
	cnt = (*ioh->ioh_writefun)(B, ioh->ioh_iofunopaque, ioh->ioh_fdout, iov, cnt, 0);

	if (cnt == 0 || cnt < 0 && IOH_EBLOCKING)
	{
	  // Not quite ready for writing yet
	  cnt = 0;
	}
	else if (cnt < 0)
	{
	  BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_ERROR_OUTPUT);
	  ioh_flush_queue(B, ioh, &ioh->ioh_writeq, NULL, 0);
	  CALLBACK(B, ioh, NULL, BK_IOH_STATUS_IOHWRITEERROR);
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
	    bk_error_printf(B, BK_ERR_ERR, "Could not allocate input buffer for ioh %p of size %d: %s\n",ioh,sizeof(lengthfromwire) - size,strerror(errno));
	    BK_RETURN(B, -1);
	  }

	  if (ioh_queue(B, &ioh->ioh_readq, data, sizeof(lengthfromwire) - size, 0, 0, NULL, 0, BK_IOH_BYPASSQUEUEFULL) < 0)
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
	  CALLBACK(B, ioh, NULL, BK_IOH_STATUS_IOHREADERROR);
	  BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_ERROR_INPUT);
	  bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdout, 0, BK_RUN_WANTREAD, 0);
	  BK_RETURN(B, 0);
	}
	
	bk_debug_printf_and(B, 2, "Attempting to allocate vectored storage of size %d (lengthfromwire %d)\n",room,lengthfromwire);
	if (!(data = malloc(room)))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not allocate input buffer for ioh %p of size %d: %s\n",ioh,room,strerror(errno));
	  BK_RETURN(B, -1);
	}

	if (ioh_queue(B, &ioh->ioh_readq, data, room, 0, 0, NULL, BID_FLAG_MESSAGE, BK_IOH_BYPASSQUEUEFULL) < 0)
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
      iter = biq_iterate(ioh->ioh_readq.biq_queue, DICT_FROM_START);
      while ((bid = biq_nextobj(ioh->ioh_readq.biq_queue, iter)) && size < lengthfromwire)
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
      biq_iterate_done(ioh->ioh_readq.biq_queue, iter);

      CALLBACK(B, ioh, sendup, BK_IOH_STATUS_READCOMPLETE);

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
 * the EOL character (or preferrably sequence but that would really
 * suck).
 * 
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
  u_int32_t room, needed, size = 0;
  dict_iter iter;
  u_int32_t lengthfromwire = 0;
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
      BK_ORETURN(B, ioht_raw_other(B, ioh, aux, cmd, flags));
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
       * is not guarenteed contiguous, this is not too interesting.
       */
      BK_ORETURN(B, ioht_raw_other(B, ioh, aux, cmd, flags));
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
    while (ioh->ioh_readq.biq_queuelen > 0)
    {
      bk_vptr *sendup;
      u_int32_t tmp = 0;

      // Find number of segments
      size = cnt = 0;
      iter = biq_iterate(ioh->ioh_readq.biq_queue, DICT_FROM_START);
      while (bid = biq_nextobj(ioh->ioh_readq.biq_queue, iter))
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
      biq_iterate_done(ioh->ioh_readq.biq_queue, iter);
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
      iter = biq_iterate(ioh->ioh_readq.biq_queue, DICT_FROM_START);
      while ((bid = biq_nextobj(ioh->ioh_readq.biq_queue, iter)) && size < needed)
      {
	if (bid->bid_data && bid->bid_inuse > 0)
	{
	  sendup[cnt].ptr = bid->bid_data + bid->bid_used;
	  sendup[cnt].len = MIN(bid->bid_inuse, needed-size);
	  size += sendup[cnt].len;
	  cnt++;
	}
      }
      biq_iterate_done(ioh->ioh_readq.biq_queue, iter);

      CALLBACK(B, ioh, sendup, BK_IOH_STATUS_READCOMPLETE);

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
 *	@param B BAKA Thread/global state
 *	@param ioh IOH state handle
 *	@param queue I/O data queue
 *	@param cmd Copy-out pending commands on this queue
 *	@param flags Flags
 */
void ioh_flush_queue(bk_s B, struct bk_ioh *ioh, struct bk_ioh_queue *queue, u_int32_t *cmd, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ioh_data *data;

  if (!ioh || !queue)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  bk_debug_printf_and(B, 1, "Internal flush of IOH %p queue %p\n", ioh, queue);

  if (cmd) *cmd = 0;

  while (data = biq_minimum(queue->biq_queue))
  {
    if (data->bid_flags)
    {
      if (cmd)
	*cmd |= data->bid_flags;
    }

    ioh_dequeue(B, NULL, queue, data, IOH_DEQUEUE_ABORT);
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
 *	@param B BAKA Thread/global state
 *	@param ioh IOH state handle
 *	@param fd File descriptor to read from
 *	@param data Where to place the data
 *	@param len Length to read
 *	@param flags Flags
 *	@return <i>-1</i> on call failure or read failure
 *	@return <br><i>0</i> on EOF
 *	@return <br><i>>0</i> indicating number of bytes read
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

  bk_debug_printf_and(B, 1, "Internal read IOH %p of %d bytes\n", ioh, len);

  // Worry about non-stream protocols--somehow
  ret = (*ioh->ioh_readfun)(B, ioh->ioh_iofunopaque, fd, data, len, flags);

  BK_RETURN(B,ret);
}



/**
 * Send all pending data on the input queue up to the user
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
  dict_iter iter;

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  bk_debug_printf_and(B, 1, "Send incomplete reads up for IOH %p\n", ioh);

  // Find out how many data segments we have
  iter = biq_iterate(ioh->ioh_readq.biq_queue, DICT_FROM_START);
  while (bid = biq_nextobj(ioh->ioh_readq.biq_queue, iter))
  {
    bk_debug_printf_and(B, 1, "Checking %p for incomplete %d (%p,%d) -- cnt %d\n", bid, bid->bid_flags, bid->bid_data, bid->bid_inuse, cnt);
    if (filter && BK_FLAG_ISCLEAR(bid->bid_flags, filter))
      continue;
    if (bid->bid_data && bid->bid_inuse)
      cnt++;
  }
  biq_iterate_done(ioh->ioh_readq.biq_queue, iter);

  if (!cnt)
  {
    // Only things left might be allocated but unused buffers.
    // No cmds can exist on the read queue.
    bk_ioh_flush(B, ioh, SHUT_RD, 0);
    BK_VRETURN(B);
  }

  if (!BK_CALLOC_LEN(sendup,sizeof(*sendup)*(cnt+1)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate data vectors to return data: %s\n", strerror(errno));
    BK_VRETURN(B);
  }
    
  // Actually fill out the data list
  cnt = 0;
  iter = biq_iterate(ioh->ioh_readq.biq_queue, DICT_FROM_START);
  while (bid = biq_nextobj(ioh->ioh_readq.biq_queue, iter))
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
  biq_iterate_done(ioh->ioh_readq.biq_queue, iter);

  bk_debug_printf_and(B, 1, "We have %d incomplete reads we are sending up\n", cnt);
  CALLBACK(B, ioh, sendup, BK_IOH_STATUS_INCOMPLETEREAD);

  // Nuke vector list
  free(sendup);

  // Nuke everything in the input queue, we have "used" the necessary stuff
  bk_ioh_flush(B, ioh, SHUT_RD, 0);

  BK_VRETURN(B);
}



/**
 * Execute all special elements on the front of the stack, until we see the first
 * non-special (note the first non-special may be the first).
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
  u_int cmd = 0;
  int ret = 0;

  if (!ioh || !queue)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  bk_debug_printf_and(B, 1, "Execute first items on stack if they are special for IOH %p queue %p\n", ioh, queue);

  while (bid=biq_minimum(queue->biq_queue))
  {
    bk_debug_printf_and(B, 2, "Checking bid %p (data-%p/%d/%d, flags-%x)\n", bid, bid->bid_data, bid->bid_inuse, bid->bid_used, bid->bid_flags);
    if (bid->bid_data)
      break;

    cmd |= bid->bid_flags;

    ioh_dequeue(B, NULL, queue, bid, 0);
  }

  if (cmd)
  {
    if ((ret = ioh_execute_cmds(B, ioh, cmd, 0)) < 0)
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
 * Execute the commands indicated by the bitfield cmds.
 *
 *	@param B BAKA Thread/global state
 *	@param ioh IOH state handle
 *	@param cmds Bitfield containing _CLOSE _SHUTDOWN commands
 *	@param flags Flags
 *	@return <i>-1</i> on call failure
 *	@return <br><i>0</i> on success
 *	@return <br><i>2</i> destroyed IOH
 */
static int ioh_execute_cmds(bk_s B, struct bk_ioh *ioh, u_int32_t cmds, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  bk_debug_printf_and(B, 1, "Execute cmds %x for IOH %p\n", cmds, ioh);

  if (BK_FLAG_ISSET(cmds, BID_FLAG_SHUTDOWN))
  {
    shutdown(ioh->ioh_fdin, SHUT_WR);
    BK_FLAG_CLEAR(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_OUTPUT_PEND);
    BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_OUTPUT);
  }

  if (BK_FLAG_ISSET(cmds, BID_FLAG_CLOSE))
  {
    bk_ioh_destroy(B, ioh);
    BK_RETURN(B,2);
  }

  BK_RETURN(B,0);
}



/**
 * Standard read() functionality in IOH api
 *
 *	@param B BAKA Thread/global state
 *	@param opaque Common opaque data for read and write funs
 *	@param fd File descriptor
 *	@param buf Data to read
 *	@param size Amount of data to read
 *	@param flags Fun for the future
 *	@return Standard @a read() return codes
 */
int bk_ioh_stdrdfun(bk_s B, void *opaque, int fd, caddr_t buf, __SIZE_TYPE__ size, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret, erno;

  errno = 0;
  ret = read(fd, buf, size);
  erno = errno;

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
 * Standard write() functionality in IOH api
 *
 *	@param B BAKA Thread/global state
 *	@param fd File descriptor
 *	@param opaque Common opaque data for read and write funs
 *	@param iovec Data to write
 *	@param size Number of iovec buffers
 *	@param flags Fun for the future
 *	@return Standard @a writev() return codes
 */
int bk_ioh_stdwrfun(bk_s B, void *opaque, int fd, struct iovec *buf, __SIZE_TYPE__ size, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret, erno;

  errno = 0;
  ret = writev(fd, buf, size);
  erno = errno;

  bk_debug_printf_and(B, 1, "System writev returns %d with errno %d\n",ret,errno);

  if (bk_debug_and(B, 0x20) && ret > 0)
  {
    bk_vptr dbuf;

    dbuf.ptr = buf[0].iov_base;
    dbuf.len = MIN(MIN(buf[0].iov_len, 32), (u_int)ret);

    bk_debug_printbuf_and(B, 0x20, "Buffer just wrote:", "\t", &dbuf);
  }

  errno = erno;
  BK_RETURN(B, ret);
}
