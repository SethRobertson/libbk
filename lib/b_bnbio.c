#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_bnbio.c,v 1.18 2003/03/07 20:29:42 jtt Exp $";
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
 * Association of file descriptor/handle to callback.  Queue data for
 * output, translate input stream into messages.
 */

#include <libbk.h>
#include "libbk_internal.h"

#define SHOULD_LINGER(bib, flags)				\
 ((BK_FLAG_ISSET((bib)->bib_flags, BK_IOHH_BNBIO_FLAG_SYNC) &&	\
   BK_FLAG_ISCLEAR((flags), BK_IOHH_BNBIO_FLAG_NO_LINGER))	\
  || (BK_FLAG_ISSET((flags), BK_IOHH_BNBIO_FLAG_LINGER)))


static void bnbio_timeout(bk_s B, struct bk_run *run, void *opaque, const struct timeval *stattime, bk_flags flags);
static int bnbio_set_timeout(bk_s B, struct bk_iohh_bnbio *bib, time_t msecs, bk_flags flags);


/**
 * @file b_bnbio.c
 *
 * All the routines required to handle "blocking" I/O in an asynchronous
 * environment.
 */



/**
 * Create a bnbio structure.
 *
 *	@param B BAKA thread/global state.
 *	@param ioh The @a bk_ioh I need to use.
 *	@param flags. Flags I need to set in the structure.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a @a new bk_iohh_bnbio on success.
 */
struct bk_iohh_bnbio *
bk_iohh_bnbio_create(bk_s B, struct bk_ioh *ioh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_iohh_bnbio *bib = NULL;

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }
  

  if (!(BK_CALLOC(bib)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bib\n");
    BK_RETURN(B, NULL);
  }

  if (!(bib->bib_bpi = bk_polling_io_create(B, ioh, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create polling structure\n");
    goto error;
  }

  bib->bib_flags = flags;

  BK_RETURN(B, bib);
  
 error:
  if (bib) 
    bk_iohh_bnbio_destroy(B, bib);
  BK_RETURN(B, NULL);
  
}



/**
 * Destroy a @a bk_iohh_bnbio structure.
 *
 *	@param B BAKA thread/global state.
 *	@param bib The @a bk_iohh_bnbio structure to destroy.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
void
bk_iohh_bnbio_destroy(bk_s B, struct bk_iohh_bnbio *bib)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bib)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  // Generally this should be closed and NULL'ed out during bk_iohh_bnbio_close()
  if (bib->bib_bpi)
    bk_polling_io_destroy(B, bib->bib_bpi);

  free(bib);
  BK_VRETURN(B);
}



/**
 * Read from a bk_iohh_bnbio configuration.
 *
 *	@param B BAKA thread/global state.
 *	@param bib The @a bk_iohh_bnbio structure to use.
 *	@param datap Copyout data pointer to use.
 *	@param timeout The timeout for the read in milliseconds.
 *	@param B BAKA thread/global state.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>size of read</i> on success (0 means EOF).
 */
int
bk_iohh_bnbio_read(bk_s B, struct bk_iohh_bnbio *bib, bk_vptr **datap, time_t timeout, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  bk_ioh_status_e status;
  int ret = 0;
  int is_canceled = 0;

  if (!bib || !datap)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

 reread:
  if (timeout)
    bnbio_set_timeout(B, bib, timeout, 0);

  BK_FLAG_CLEAR(bib->bib_flags, BK_IOHH_BNBIO_FLAG_TIMEDOUT);

  // first, generate *some* form of useful information
  while (!bk_iohh_bnbio_is_timedout(B, bib) &&
	 (!(is_canceled = bk_iohh_bnbio_is_canceled(B, bib, 0))) && 
	 ((ret = bk_polling_io_read(B, bib->bib_bpi, datap, &status, 0)) == 1))
    /* void */;

  if (timeout)
    bnbio_set_timeout(B, bib, 0, 0);

  // Check for timeout unless we miraculously found some data
  // <WARNING> The check for data was a late change. It might not be correct in all cases </WARNING>
  if (!*datap && (bk_iohh_bnbio_is_timedout(B, bib) || is_canceled))
  {
    if (is_canceled)
      bk_error_printf(B, BK_ERR_WARN, "Blocking NBIO read was canceled\n");

    BK_RETURN(B, -1);
  }

  if (ret < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Blocking NBIO read poll failed\n");
    BK_RETURN(B, -1);
  }

  if (ret == 0 && !(*datap))
  {
    // EOF indicator, just pass this along.
    BK_RETURN(B, 0);
  }

  switch (status)
  {
  case BkIohStatusIncompleteRead:
  case BkIohStatusReadComplete:
    BK_RETURN(B, (*datap)->len);
    break;

  case BkIohStatusIohReadError:
    // Don't log anything here since error is sticky, and may not be new
    BK_RETURN(B, -1);
    break;
    

  case BkIohStatusWriteAborted:
  case BkIohStatusWriteComplete:
    // Maybe free data....
    // Intentional fall through
  case BkIohStatusIohWriteError:
    // Don't log anything here since error is sticky, and may not be new
    goto reread;
    break;

  case BkIohStatusIohReadEOF:
    break;

  case BkIohStatusIohClosing:
    // Free up data? I don't think so.
    break;

  case BkIohStatusIohSeekSuccess:
  case BkIohStatusIohSeekFailed:
    bk_error_printf(B, BK_ERR_ERR, "How did I get a seek reply in read?\n");
    goto reread;
    break;

  default:
    bk_error_printf(B, BK_ERR_ERR,"Unknown status: %d\n", status);
    break;
  }
  
  BK_RETURN(B, 0);
}



/**
 * Write out a buffer to blocking system.
 *
 * Possibly "lingering" until we know that it's written.  The awb proto plugins
 * claim something about returning -1 "if pending write cannot succeed with
 * LINGER turned on" (whatever *that* means).  At any rate, the linger setting
 * has no effect on return codes as this was coded before I added error return,
 * and it still doesn't.  The only thing linger affects is whether we are
 * willing to keep calling bk_polling_io_do_poll until the output queue is
 * drained or we get cancellation/ioh-error-state.
 *
 *	@param B BAKA thread/global state.
 *	@return <i>-1</i> on failure (client must free data).<br>
 *	@return <i>0</i> on success.<br>
 *	@return <i>1</i> on error (client must not use data).
 */
int
bk_iohh_bnbio_write(bk_s B, struct bk_iohh_bnbio *bib, bk_vptr *data, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  bk_ioh_status_e status;
  bk_vptr *ddata = NULL;
  int linger = 0;
  int is_canceled = 0;

  if (!bib || !data)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (bk_polling_io_write(B, bib->bib_bpi, data, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not write out data\n");
    BK_RETURN(B, -1);
  }


  // Wait for the buffer to return.
  if (SHOULD_LINGER(bib, flags))
    linger = 1;

  // Do at least one poll to clean up the state from this write under normal conditions.
  do
  {
    if (bk_polling_io_do_poll(B, bib->bib_bpi, &ddata, &status, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Blocking NBIO write poll failed\n");
      BK_RETURN(B, -1);      
    }

    /*
     * Continue checking the queue even if ret == 1 (which generally means
     * "no progress". The problem here is that we don't return write
     * buffers back for freeing ('cause we don't always linger), so it's
     * quite possible for us to make forward progress in terms of *writing*
     * out data without the poll subsystem really knowing it.
     */
  } while (linger && !(is_canceled = bk_iohh_bnbio_is_canceled(B, bib, 0)) &&
	   status != BkIohStatusIohWriteError &&
	   bib->bib_bpi->bpi_ioh->ioh_writeq.biq_queuelen > 0);

  if (is_canceled)
  {
    bk_error_printf(B, BK_ERR_WARN, "Blocking NBIO write was canceled\n");
    BK_RETURN(B, 1);
  }
  if (status == BkIohStatusIohWriteError)
    // Don't log anything here since error is sticky, and may not be new
    BK_RETURN(B, 1);

  BK_RETURN(B, 0);  
}



/**
 * Seek to a position in a blocking way.
 *
 *	@param B BAKA thread/global state.
 *	@param bib The @a bk_iohh_bnbio to use.
 *	@param ioh The @a bk_ioh to use.
 *	@param offset The offset (as in @a lseek(2)).
 *	@param whence Whence (as in @a lseek(2)).
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_iohh_bnbio_seek(bk_s B, struct bk_iohh_bnbio *bib, off_t offset, int whence, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  bk_ioh_status_e status;
  int ret = 0;

  if (!bib)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  if (bk_ioh_seek(B, bib->bib_bpi->bpi_ioh, offset, whence) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not ioh seek\n");
    goto error;
  }

  while ((ret = bk_polling_io_do_poll(B, bib->bib_bpi, NULL, &status, 0)) == 1)
    /* void */;
  
  if (ret < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Blocking NBIO seek poll failed\n");
    BK_RETURN(B, -1);      
  }

  switch (status)
  {
  case BkIohStatusIohSeekSuccess:
    bib->bib_bpi->bpi_tell = offset;
    ret = 0;
    break;
  case BkIohStatusIohSeekFailed:
    ret = -1;
    break;
  default: 
    bk_error_printf(B, BK_ERR_ERR, "Unexpected seek status: %d\n", status);
    
  }
    
  BK_RETURN(B, ret);
  
 error:
  BK_RETURN(B, -1);
}



/**
 * Return the current position in the data stream (as far as the blocking client knows).
 *
 *	@param B BAKA thread/global state.
 *	@param bib The @a bk_iohh_bnbio structure to tell on.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int64_t
bk_iohh_bnbio_tell(bk_s B, struct bk_iohh_bnbio *bib, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bib)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  // <WARNING> LAYER VIOLATION </WARNING>
  BK_RETURN(B, bib->bib_bpi->bpi_tell);  
}



/**
 * Close up a blocking configuration
 *
 *	@param B BAKA thread/global state.
 *	@param bib The @a bk_iohh_bnbio structure to close.
 *	@param flags Flags for future use.
 */
void
bk_iohh_bnbio_close(bk_s B, struct bk_iohh_bnbio *bib, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret = 0;
  bk_vptr *data;
  bk_ioh_status_e status;

  if (!bib)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (bib->bib_read_to_handle)
    bk_run_dequeue(B, POLLING_IOH_RUN(bib->bib_bpi), bib->bib_read_to_handle, BK_RUN_DEQUEUE_EVENT);

  // <TODO> Jtt is not sure that this is correct and won't leak memory </TODO>

  if (SHOULD_LINGER(bib, flags))
  {
    bk_polling_io_close(B, bib->bib_bpi, BK_POLLING_CLOSE_FLAG_LINGER);
    while (((ret = bk_polling_io_do_poll(B, bib->bib_bpi, &data, &status, 0))
	    >= 0) && (status != BkIohStatusIohClosing))
    {
      if (data)
      {
	bk_polling_io_data_destroy(B, data);
      }
    }

    if (ret < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Blocking NBIO close poll failed\n");
      BK_VRETURN(B);
    }
    // We have to destroy this ourselves now.
    bk_polling_io_destroy(B, bib->bib_bpi);
  }
  else
  {
    // We're not lingering; the run loop is responsible for cleaning up
    bk_polling_io_close(B, bib->bib_bpi, 0);
  }
  
  bib->bib_bpi = NULL;
  bk_iohh_bnbio_destroy(B, bib);
  BK_VRETURN(B);
}



/**
 * Set a read timeout for a bnbio (0 means clear timeout).
 *
 *	@param B BAKA thread/global state.
 *	@param bib The @a bk_iohh_bnbio to use.
 *	@param msecs The timeout in milliseconds.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
bnbio_set_timeout(bk_s B, struct bk_iohh_bnbio *bib, time_t msecs, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bib)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (bib->bib_read_to_handle && (bk_run_dequeue(B, POLLING_IOH_RUN(bib->bib_bpi), bib->bib_read_to_handle, BK_RUN_DEQUEUE_EVENT) < 0))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not dequeue bnbio timeout\n");
    goto error;
  }

  bib->bib_read_to_handle = NULL;

  if (msecs && (bk_run_enqueue_delta(B, POLLING_IOH_RUN(bib->bib_bpi), msecs, bnbio_timeout, bib, &bib->bib_read_to_handle, 0) < 0))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not enqueue new bnbio timeout event\n");
    goto error;
  }
      
  BK_RETURN(B, 0);  
  
 error:
  BK_RETURN(B, -1);  
}



/**
 * Timeout a bnbio read.
 *
 *	@param B BAKA thread/global state.
 *	@param run The bk_run struct.
 *	@param opaque Data the caller asked to be returned.
 *	@param starttime The starting time of this event run.
 *	@param flags Flags.
 */
static void
bnbio_timeout(bk_s B, struct bk_run *run, void *opaque, const struct timeval *stattime, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_iohh_bnbio *bib = opaque;

  if (!run || !bib)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }
  
  bib->bib_read_to_handle = NULL;

  BK_FLAG_SET(bib->bib_flags, BK_IOHH_BNBIO_FLAG_TIMEDOUT);
  BK_VRETURN(B);  
}



/**
 * Is this bnbio struct in a timed out state.
 *
 *	@param B BAKA thread/global state.
 *	@param bnbio The bk_iohh_bnbio to check
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success and not in timeout.
 *	@return <i>0</i> on success and in timeout.
 */
int
bk_iohh_bnbio_is_timedout(bk_s B, struct bk_iohh_bnbio *bib)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bib)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  BK_RETURN(B, BK_FLAG_ISSET(bib->bib_flags, BK_IOHH_BNBIO_FLAG_TIMEDOUT));  
}



/**
 * Register a @a bnbio for cancellation
 *
 *	@param B BAKA thread/global state.
 *	@param bib The @a bk_iohh_bnbio to use.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_iohh_bnbio_cancel_register(bk_s B, struct bk_iohh_bnbio *bib, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bib)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  BK_RETURN(B, bk_polling_io_cancel_register(B, bib->bib_bpi, 0));  
}



/**
 * Unregister a @a bnbio for cancellation
 *
 *	@param B BAKA thread/global state.
 *	@param bib The @a bk_iohh_bnbio to unregister.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_iohh_bnbio_cancel_unregister(bk_s B, struct bk_iohh_bnbio *bib, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bib)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  BK_RETURN(B, bk_polling_io_cancel_unregister(B, bib->bib_bpi, 0));  
}



/**
 * Check if a bnbio has been registered for cancellation.
 *
 *	@param B BAKA thread/global state.
 *	@param bib The @a bk_iohh_bnbio to use.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success and <b>not</b> registered.
 *	@return <i>1</i> on success and registered.
 */
int
bk_iohh_bnbio_is_canceled(bk_s B, struct bk_iohh_bnbio *bib, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bib)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  BK_RETURN(B, bk_polling_io_is_canceled(B, bib->bib_bpi, 0));  
}



/**
 * Cancel a bib
 *
 *	@param B BAKA thread/global state.
 *	@param bib The @a bk_iohh_bnbio to cancel
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_iohh_bnbio_cancel(bk_s B, struct bk_iohh_bnbio *bib, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bib)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  BK_RETURN(B, bk_polling_io_cancel(B, bib->bib_bpi, flags));  
}



/**
 * Get error message from a bib
 *
 *	@param B BAKA thread/global state.
 *	@param bib The @a bk_iohh_bnbio to cancel
 *	@return an explanatory error message.<br>
 *	@return NULL if we don't have a clue.
 */
const char *
bk_iohh_bnbio_geterr(bk_s B, struct bk_iohh_bnbio *bib)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int err;

  if (!bib || !bib->bib_bpi || !bib->bib_bpi->bpi_ioh)
    err = EINVAL;
  else
    err = bib->bib_bpi->bpi_ioh->ioh_errno;

  if (!err)
    BK_RETURN(B, NULL);
    

  BK_RETURN(B, strerror(err));
}
