#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_ioh.c,v 1.3 2001/11/06 18:25:24 seth Exp $";
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
 * All of the baka ioh public and private functions.
 */

#include <libbk.h>
#include "libbk_internal.h"



#define CALLBACK(ioh,data, state) ((*((ioh)->ioh_handler))((data), (ioh)->ioh_opaque, (ioh), (state)))




/**
 * Information about a chunk of user data in use by the ioh subsystem
 */
struct bk_ioh_data
{
  char		       *bid_data;		/**< Actual data */
  u_int32_t		bid_allocated;		/**< Allocated size of data */
  u_int32_t		bid_inuse;		/**< Amount actually used (!including consumed) */
  u_int32_t		bid_used;		/**< Amount virtually consumed */
  bk_vptr	       *bid_vptr;		/**< Stored vptr to return in callback */
};



/**
 * Information about a queue of data pending for I/O
 */
struct bk_ioh_queue
{
  u_int32_t		biq_queuelen;		/**< Amount of non-consumed data current in queue */
  u_int32_t		biq_queuemax;		/**< Maximum of non-consumed data storable queue */
  dict_h		biq_queue;		/**< Queued data */
};



/**
 * Information about a specific ioh.
 */
struct bk_ioh
{
  int			ioh_fdin;		/**< Input file descriptor */
  int			ioh_fdout;		/**< Output file descriptor */
  bk_iofunc 		ioh_readfun;		/**< Function to read data */
  bk_iofunc		ioh_writefun;		/**< Function to write data */
  bk_iohhandler		ioh_handler;		/**< Callback function for event handling */
  void		       *ioh_opaque;		/**< Opaque data for handler */
  u_int32_t		ioh_inbuf_hint;		/**< Hint for input buffer sizes */
  u_int32_t		ioh_inbuf_max;		/**< Maxmum size of input buffer */
  u_int32_t		ioh_outbuf_max;		/**< Maxmum amount of data buffered for output */
  struct bk_run	       *ioh_run;		/**< BK_RUN environment */
  struct bk_ioh_queue	ioh_readq;		/**< Data queued for reading */
  struct bk_ioh_queue	ioh_writeq;		/**< Data queued for writing */
  bk_flags		ioh_extflags;		/**< Flags--see libbk.h */
  bk_flags		ioh_intflags;		/**< Flags */
#define IOH_FLAGS_SHUTDOWN_INPUT	0x01	/**< Input shut down */
#define IOH_FLAGS_SHUTDOWN_OUTPUT	0x02	/**< Output shut down */
#define IOH_FLAGS_SHUTDOWN_OUTPUT_PEND	0x04	/**< Output shut down */
#define IOH_FLAGS_SHUTDOWN_CLOSING	0x08	/**< Attempting to close */
#define IOH_FLAGS_SHUTDOWN_DESTROYING	0x10	/**< Attempting to destroy */
#define IOH_FLAGS_SHUTDOWN_NOTIFYDIE	0x20	/**< Notify callback on actual death */
#define IOH_FLAGS_DONTCLOSEFDS		0x40	/**< Don't close FDs on death */
};



/**
 * @group biqclc bk ioh queue of data pending for I/O defintions.
 * @{
 */
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
/**@}*/



/*
 * Static functions
 */
static void ioh_runhandler(bk_s B, struct bk_run *run, u_int fd, u_int gottypes, void *opaque, struct timeval starttime);
static int ioht_raw_queue(bk_s B, struct bk_ioh *ioh, bk_vptr *data, bk_flags flags);
static int ioht_block_queue(bk_s B, struct bk_ioh *ioh, bk_vptr *data, bk_flags flags);
static int ioht_vector_queue(bk_s B, struct bk_ioh *ioh, bk_vptr *data, bk_flags flags);
static int ioht_line_queue(bk_s B, struct bk_ioh *ioh, bk_vptr *data, bk_flags flags);
static int ioht_raw_other(bk_s B, struct bk_ioh *ioh, u_int data, bk_flags flags);
static int ioht_block_other(bk_s B, struct bk_ioh *ioh, u_int data, bk_flags flags);
static int ioht_vector_other(bk_s B, struct bk_ioh *ioh, u_int data, bk_flags flags);
static int ioht_line_other(bk_s B, struct bk_ioh *ioh, u_int data, bk_flags flags);
#define IOHT_SHUTDOWN		0x01		// Other command is a shutdown
#define IOHT_FLUSH		0x02		// Other command is a flush
#define IOHT_CLOSE		0x04		// Other command is a close



/**
 * Create and initialize the ioh environment.
 *	@param B BAKA thread/global state 
 *	@param fdin The file descriptor to read from.  -1 if no input is desired.
 *	@param fdout The file descriptor to write to.  -1 if no output is desired.
 *	@param readfun The function to use to read data.
 *	@param writefun The function to use to write data.
 *	@param handler The user callback to notify on complete I/O or other events
 *	@param opaque The opaque data for the user callback.
 *	@param inbufhint A hint for the input routines (0 for 128 bytes)
 *	@param inbufmax The maximum buffer size of incomplete data (0 for unlimited)
 *	@param outbufmax The maximum amount of data queued for transmission (0 for unlimited)
 *	@param run The bk run environment to use with the fd.
 *	@param flags The type of data on the file descriptors.
 *	@return NULL on call failure, allocation failure, or other fatal error.
 *	@return The initialized ioh structure if successful.
 */
struct bk_ioh *bk_ioh_init(bk_s B, int fdin, int fdout, bk_iofunc readfun, bk_iofunc writefun, bk_iohhandler handler, void *opaque, u_int32_t inbufhint, u_int32_t inbufmax, u_int32_t outbufmax, struct bk_run *run, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ioh *curioh = NULL;
  int tmp;
 
  if ((fdin < 0 && fdout < 0) || (fdin < 0 && !readfun) || (fdout < 0 && !writefun) || !handler || !run)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }


  // Check for invalid flags combinations
  tmp = 0;
  if (BK_FLAG_ISSET(flags, BK_IOH_RAW)) tmp++;
  if (BK_FLAG_ISSET(flags, BK_IOH_BLOCKED)) tmp++;
  if (BK_FLAG_ISSET(flags, BK_IOH_VECTORED)) tmp++;
  if (BK_FLAG_ISSET(flags, BK_IOH_LINE)) tmp++;

  if (tmp != 1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Must have one parse method in flags (RAW/BLOCKED/VECTORED/LINE)\n");
    BK_RETURN(B, NULL);
  }


  // Check for invalid flags combinations
  tmp = 0;
  if (BK_FLAG_ISSET(flags, BK_IOH_STREAM)) tmp++;

  if (tmp != 1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Must have one input type in flags (STREAM)\n");
    BK_RETURN(B, NULL);
  }

  if (!(curioh = malloc(sizeof(*curioh))))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate ioh structure: %s\n",strerror(errno));
    BK_RETURN(B, NULL);
  }
  memset(curioh, 0, sizeof(*curioh));

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
  curioh->ioh_inbuf_hint = inbufhint;
  curioh->ioh_readq.biq_queuemax = inbufmax;
  curioh->ioh_writeq.biq_queuemax = outbufmax;
  curioh->ioh_run = run;
  curioh->ioh_extflags = flags;

  curioh->ioh_fdin = fdin;
  if (curioh->ioh_fdin >= 0)
  {
    if (bk_run_handle(B, curioh->ioh_run, curioh->ioh_fdin, ioh_runhandler, curioh, BK_RUN_WANTREAD, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not put this new read ioh into the bk_run environment\n");
      goto error;
    }
  }
  else
  {
    BK_FLAG_SET(curioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_INPUT);
  }

  curioh->ioh_fdout = fdout;
  if (curioh->ioh_fdout >= 0 && curioh->ioh_fdin != curioh->ioh_fdout)
  {
    if (bk_run_handle(B, curioh->ioh_run, curioh->ioh_fdout, ioh_runhandler, curioh, 0, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not put this new write ioh into the bk_run environment\n");
      goto error;
    }
  }
  else
  {
    BK_FLAG_SET(curioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_OUTPUT);
  }

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
 *	@param B BAKA thread/global state 
 *	@param ioh The IOH environment to update
 *	@param readfun The function to use to read data.
 *	@param writefun The function to use to write data.
 *	@param handler The user callback to notify on complete I/O or other events
 *	@param opaque The opaque data for the user callback.
 *	@param inbufhint A hint for the input routines (0 for 128 bytes)
 *	@param inbufmax The maximum buffer size of incomplete data (0 for unlimited)
 *	@param outbufmax The maximum amount of data queued for transmission (0 for unlimited)
 *	       -- lowered maximum will not affect previously queued data
 *	@param flags The type of data on the file descriptors.
 *	       -- not all (any?) flags changes will take affect or will have a positive effect--handle with care
 *	@return -1 on call failure.
 *	@return 0 on success.
 */
int bk_ioh_update(bk_s B, struct bk_ioh *ioh, bk_iofunc readfun, bk_iofunc writefun, int (*handler)(bk_vptr *data, void *opaque, struct bk_ioh *ioh, u_int state_flags), void *opaque, u_int32_t inbufhint, u_int32_t inbufmax, u_int32_t outbufmax, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ioh *curioh = NULL;
  int tmp;
 
  if (!ioh || (ioh->ioh_readfun && !readfun) || (ioh->ioh_writefun && !writefun) || !handler)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  curioh->ioh_readfun = readfun;
  curioh->ioh_writefun = writefun;
  curioh->ioh_handler = handler;
  curioh->ioh_opaque = opaque;
  curioh->ioh_inbuf_hint = inbufhint;
  curioh->ioh_readq.biq_queuemax = inbufmax;
  curioh->ioh_writeq.biq_queuemax = outbufmax;
  curioh->ioh_extflags = flags;

  BK_RETURN(B, 0);
}



/**
 * Request various configuration parameters of a IOH instance.
 *	@param B BAKA thread/global state 
 *	@param ioh The IOH environment to update
 *	@param fdin The file descriptor to read from.  -1 if no input is desired.
 *	@param fdout The file descriptor to write to.  -1 if no output is desired.
 *	@param readfun The function to use to read data.
 *	@param writefun The function to use to write data.
 *	@param handler The user callback to notify on complete I/O or other events
 *	@param opaque The opaque data for the user callback.
 *	@param inbufhint A hint for the input routines (0 for 128 bytes)
 *	@param inbufmax The maximum buffer size of incomplete data (0 for unlimited)
 *	@param outbufmax The maximum amount of data queued for transmission (0 for unlimited)
 *	@param run The bk run environment to use with the fd.
 *	@param flags The type of data on the file descriptors.
 *	@return -1 on call failure.
 *	@return 0 on success.
 */
int bk_ioh_get(bk_s B, struct bk_ioh *ioh, int *fdin, int *fdout, bk_iofunc *readfun, bk_iofunc *writefun, int (**handler)(bk_vptr *data, void *opaque, struct bk_ioh *ioh, u_int state_flags), void **opaque, u_int32_t *inbufhint, u_int32_t *inbufmax, u_int32_t *outbufmax, struct bk_run **run, bk_flags *flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ioh *curioh = NULL;
  int tmp;
 
  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (fdin) *fdin = ioh->ioh_fdin;
  if (fdout) *fdout = ioh->ioh_fdout;
  if (readfun) *readfun = ioh->ioh_readfun;
  if (writefun) *writefun = ioh->ioh_writefun;
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
 *	@param B BAKA thread/global state 
 *	@param ioh The IOH environment to update
 *	@param data The data to be sent
 *	@param flags Future expansion
 *	@return -1 on call failure or subsystem refusal
 *	@return 0 on success
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

  if (BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_CLOSING) ||
      BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_DESTROYING) ||
      BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_OUTPUT) ||
      BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_OUTPUT_PEND))
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
    BK_RETURN(B, -1);
  }

  if (ret < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not append user data to outgoing message queue\n");
    BK_RETURN(B, -1);
  }

  bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdout, BK_RUN_WANTWRITE, BK_RUN_WANTWRITE, 0);

  // <TODO>Consider trying a spontaneous write here, if nbio is set, especially if writes are not pending </TODO>

  BK_RETURN(B, 0);
}



/**
 * Tell the system that no futher system I/O will be permitted.
 * If data is currently queued for input, this data will sent to the user as incomplete.
 * If data is currented queued for output, this data will be drained before the shutdown takes effect.
 * See bk_ioh_flush for a way to avoid this.
 *	@param B BAKA thread/global state 
 *	@param ioh The IOH environment to update
 *	@param how -- SHUT_RD to shut down reads, SHUT_WR to shut down writes, SHUT_RDWR for both.
 *	@param flags Future expansion
 *	@return -1 on call failure or subsystem refusal
 *	@return 0 on success
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

  if (BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_CLOSING) ||
      BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_DESTROYING))
  {
    bk_error_printf(B, BK_ERR_ERR, "Cannot perform action after close\n");
    BK_VRETURN(B);
  }

  if (how == SHUT_RD || how == SHUT_RDWR)
  {
    if (BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_INPUT))
    {
      if (how == SHUT_RD)
	BK_VRETURN(B);				// Already done

      how = SHUT_WR;
    }
  }

  if (how == SHUT_WR || how == SHUT_RDWR)
  {
    if (BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_OUTPUT) ||
	BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_OUTPUT_PEND))
    {
      if (how == SHUT_WR)
	BK_VRETURN(B);				// Already done

      how = SHUT_RD;
    }
  }

  // The message type performs the necessary flushing, draining, and actual shutdown
  if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_RAW))
  {
    ret = ioht_raw_other(B, ioh, how, IOHT_SHUTDOWN);
  }
  else if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_BLOCKED))
  {
    ret = ioht_block_other(B, ioh, how, IOHT_SHUTDOWN);
  }
  else if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_VECTORED))
  {
    ret = ioht_vector_other(B, ioh, how, IOHT_SHUTDOWN);
  }
  else if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_LINE))
  {
    ret = ioht_line_other(B, ioh, how, IOHT_SHUTDOWN);
  }
  else
  {
    bk_error_printf(B, BK_ERR_ERR, "Unknown message format type %x\n",ioh->ioh_extflags);
    BK_VRETURN(B);
  }

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
      BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_OUTPUT_PEND);
    else
      BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_OUTPUT);
  }

  BK_VRETURN(B);
}



/**
 * Flush data queued in the ioh.
 * Data may be queued for input or output.
 *	@param B BAKA thread/global state 
 *	@param ioh The IOH environment to update
 *	@param how -- SHUT_RD to flush input, SHUT_WR to flush output, SHUT_RDWR for both.
 *	@param flags Future expansion
 *	@return -1 on call failure or subsystem refusal
 *	@return 0 on success
 */
void bk_ioh_flush(bk_s B, struct bk_ioh *ioh, int how, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret;
 
  if (!ioh || (how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR))
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_DESTROYING))
  {
    bk_error_printf(B, BK_ERR_ERR, "Cannot perform action after close\n");
    BK_VRETURN(B);
  }

  // The message type performs the necessary flushing, draining, and actual shutdown
  if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_RAW))
  {
    ret = ioht_raw_other(B, ioh, how, IOHT_FLUSH);
  }
  else if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_BLOCKED))
  {
    ret = ioht_block_other(B, ioh, how, IOHT_FLUSH);
  }
  else if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_VECTORED))
  {
    ret = ioht_vector_other(B, ioh, how, IOHT_FLUSH);
  }
  else if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_LINE))
  {
    ret = ioht_line_other(B, ioh, how, IOHT_FLUSH);
  }
  else
  {
    bk_error_printf(B, BK_ERR_ERR, "Unknown message format type %x\n",ioh->ioh_extflags);
    BK_VRETURN(B);
  }

  BK_VRETURN(B);
}



/**
 * Control whether or not new input will be accepted from the system.
 * Note there may be incomplete data on the input queue which will remain pending.
 *	@param B BAKA thread/global state 
 *	@param ioh The IOH environment to update
 *	@param isallowed Zero if no reads desired, non-zero if reads allowed
 *	@param flags Future expansion
 *	@return -1 on call failure or subsystem refusal
 *	@return 0 on success
 */
void bk_ioh_readallowed(bk_s B, struct bk_ioh *ioh, int isallowed, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  u_int pref;
 
  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (ioh->ioh_fdin < 0 || BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_INPUT))
  {
    bk_error_printf(B, BK_ERR_WARN, "You cannot manipulate the read desireability if input is technically impossible\n");
    BK_VRETURN(B);
  }

  // Get old preference to preserve write/xcpt
  if ((pref = bk_run_getpref(B, ioh->ioh_run, ioh->ioh_fdin, 0)) == (u_int)-1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Cannot obtain I/O preferences\n");
    BK_VRETURN(B);
  }

  pref &= ~BK_RUN_WANTREAD;
  pref |= isallowed?BK_RUN_WANTREAD:0;

  // Set new preference
  if (bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdin, pref, 0, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Cannot set I/O preferences\n");
    BK_VRETURN(B);
  }

  BK_VRETURN(B);
}



/**
 * Indicate that no further activity is desired on this ioh.
 * The ioh may linger if data requires draining, unless abort.
 * Incomplete data pending on the input queue will be sent to user as incomplete unless abort.
 * Additional callbacks may occur--WRITECOMPLETEs or IOHWRITEERRORs
 *  (if no abort), WRITEABORTEDs (if abort), IOHCLOSING (if
 *  NOTIFYANYWAY)
 *	@param B BAKA thread/global state 
 *	@param ioh The IOH environment to update
 *	@param flags Future expansion
 */
void bk_ioh_close(bk_s B, struct bk_ioh *ioh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret = 0;
 
  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (BK_FLAG_ISSET(flags, BK_IOH_DONTCLOSEFDS))
    BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_DONTCLOSEFDS);

  if (BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_CLOSING) && BK_FLAG_ISCLEAR(flags, BK_IOH_ABORT) ||
      BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_DESTROYING))
  {
    bk_error_printf(B, BK_ERR_WARN, "Close already in progress\n");
    BK_VRETURN(B);
  }

  BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_CLOSING);

  if (BK_FLAG_ISSET(flags, BK_IOH_ABORT))
    bk_ioh_flush(B, ioh, SHUT_RDWR, 0);

  if (BK_FLAG_ISSET(ioh->ioh_intflags, BK_IOH_NOTIFYANYWAY))
    BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_NOTIFYDIE);

  // The message type performs the necessary flushing, draining, and actual shutdown
  if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_RAW))
  {
    ret = ioht_raw_other(B, ioh, flags, IOHT_CLOSE);
  }
  else if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_BLOCKED))
  {
    ret = ioht_block_other(B, ioh, flags, IOHT_CLOSE);
  }
  else if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_VECTORED))
  {
    ret = ioht_vector_other(B, ioh, flags, IOHT_CLOSE);
  }
  else if (BK_FLAG_ISSET(ioh->ioh_extflags, BK_IOH_LINE))
  {
    ret = ioht_line_other(B, ioh, flags, IOHT_CLOSE);
  }
  else
  {
    bk_error_printf(B, BK_ERR_ERR, "Unknown message format type %x\n",ioh->ioh_extflags);
  }

  if (BK_FLAG_ISSET(ioh->ioh_intflags, BK_IOH_ABORT) || ret == 0)
    bk_ioh_destroy(B, ioh);

  BK_VRETURN(B);
}



/**
 * Indicate that no further activity is desired on this ioh.
 * All queued data will be flushed (use _close instead).
 * User data queued on system probably will not be freed (use _close instead).
 * No user notification (use _close instead).
 * Did we mention you should use _close instead of this interface?
 *	@param B BAKA thread/global state 
 *	@param ioh The IOH environment to update
 */
void bk_ioh_destroy(bk_s B, struct bk_ioh *ioh)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  u_int pref;
  struct bk_ioh_data *data;
 
  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_DESTROYING))
  {
    bk_error_printf(B, BK_ERR_WARN, "Close already in progress\n");
    BK_VRETURN(B);
  }

  BK_FLAG_SET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_DESTROYING);

  if (BK_FLAG_ISCLEAR(ioh->ioh_intflags, IOH_FLAGS_DONTCLOSEFDS))
  {
    if (ioh->ioh_fdin >= 0)
      close(ioh->ioh_fdin);

    if (ioh->ioh_fdout >= 0)
      close(ioh->ioh_fdout);
  }

  if (BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_NOTIFYDIE))
    CALLBACK(ioh, NULL, BK_IOH_STATUS_IOHCLOSING);

  // Nuke input queue, deleting queued data
  DICT_NUKE_CONTENTS(ioh->ioh_readq.biq_queue, biq, data, break, if (data->bid_data) free(data->bid_data); free(data));
  biq_destroy(ioh->ioh_readq.biq_queue);

  // Nuke output queue, discarding queued data since it belongs to the user
  DICT_NUKE_CONTENTS(ioh->ioh_writeq.biq_queue, biq, data, break, free(data));
  biq_destroy(ioh->ioh_writeq.biq_queue);

  free(ioh);

  BK_VRETURN(B);
}



/*
 * Run's interface into the IOH
 */
static void ioh_runhandler(bk_s B, struct bk_run *run, u_int fd, u_int gottypes, void *opaque, struct timeval starttime)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ioh *ioh = opaque;
 
  switch (gottypes)
  {
  }

  BK_VRETURN(B);
}



/*
 * IOH Type routines
 */
static int ioht_raw_queue(bk_s B, struct bk_ioh *ioh, bk_vptr *data, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  BK_RETURN(B, 0);
}

static int ioht_block_queue(bk_s B, struct bk_ioh *ioh, bk_vptr *data, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  BK_RETURN(B, 0);
}

static int ioht_vector_queue(bk_s B, struct bk_ioh *ioh, bk_vptr *data, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  BK_RETURN(B, 0);
}

static int ioht_line_queue(bk_s B, struct bk_ioh *ioh, bk_vptr *data, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  BK_RETURN(B, 0);
}

static int ioht_raw_other(bk_s B, struct bk_ioh *ioh, u_int data, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  BK_RETURN(B, 0);
}

static int ioht_block_other(bk_s B, struct bk_ioh *ioh, u_int data, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  BK_RETURN(B, 0);
}

static int ioht_vector_other(bk_s B, struct bk_ioh *ioh, u_int data, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  BK_RETURN(B, 0);
}

static int ioht_line_other(bk_s B, struct bk_ioh *ioh, u_int data, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  BK_RETURN(B, 0);
}
