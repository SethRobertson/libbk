#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_pollio.c,v 1.17 2003/05/09 19:02:18 seth Exp $";
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



/**
 * The ioh data (buffers and status) held pending user read.
 */
struct polling_io_data
{
  bk_flags		pid_flags;		///< Everyone needs flags.
  bk_vptr *		pid_data;		///< The actual data.
  bk_ioh_status_e	pid_status;		///< The returned status.
};


/**
 * @name Defines: Polling io data lists.
 * List of ioh buffers and status's from the ioh level.
 *
 * <WARNING>
 * IF YOU CHANGE THIS FROM A DLL, YOU MUST SEARCH FOR THE STRING clc_add (2
 * instances) AND CHANGE THOSE REFERENCES THAT DO NOT MATCH THE MACRO
 * FORMAT.
 * </WARNING>
 */
// @{
#define pidlist_create(o,k,f)		dll_create((o),(k),(f))
#define pidlist_destroy(h)		dll_destroy(h)
#define pidlist_insert(h,o)		dll_insert((h),(o))
#define pidlist_insert_uniq(h,n,o)	dll_insert_uniq((h),(n),(o))
#define pidlist_append(h,o)		dll_append((h),(o))
#define pidlist_append_uniq(h,n,o)	dll_append_uniq((h),(n),(o))
#define pidlist_search(h,k)		dll_search((h),(k))
#define pidlist_delete(h,o)		dll_delete((h),(o))
#define pidlist_minimum(h)		dll_minimum(h)
#define pidlist_maximum(h)		dll_maximum(h)
#define pidlist_successor(h,o)		dll_successor((h),(o))
#define pidlist_predecessor(h,o)	dll_predecessor((h),(o))
#define pidlist_iterate(h,d)		dll_iterate((h),(d))
#define pidlist_nextobj(h,i)		dll_nextobj(h,i)
#define pidlist_iterate_done(h,i)	dll_iterate_done(h,i)
#define pidlist_error_reason(h,i)	dll_error_reason((h),(i))
// @}



static struct polling_io_data *pid_create(bk_s B);
static void pid_destroy(bk_s B, struct polling_io_data *pid);
static void polling_io_ioh_handler(bk_s B, bk_vptr *data, void *args, struct bk_ioh *ioh, bk_ioh_status_e status);
static int polling_io_flush(bk_s B, struct bk_polling_io *bpi, bk_flags flags );



/**
 * Create the context for on demand operation. <em>NB</em> We take control
 * of the @a ioh, just like in bk_relay().
 *
 *	@param B BAKA thread/global state.
 *	@param ioh The BAKA ioh structure to use.
 *	@param flags BK_POLLING_THREADED if callee is guarenteed in a seperate thread from bk_run_run
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a bk_polling_io on success.
 */
struct bk_polling_io *
bk_polling_io_create(bk_s B, struct bk_ioh *ioh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_polling_io *bpi = NULL;
  int pidflags = 0;

  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!(BK_MALLOC(bpi)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bpi: %s\n", strerror(errno));
    goto error;
  }

  pidflags = DICT_UNORDERED
#ifdef BK_USING_PTHREADS
    |(BK_GENERAL_FLAG_ISTHREADON(B)?DICT_THREADED_SAFE:0)
#endif /* BK_USING_PTHREADS */
    ;

  if (!(bpi->bpi_data = pidlist_create(NULL, NULL, pidflags)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create data dll\n");
  }

#ifdef BK_USING_PTHREADS
  pthread_mutex_init(&bpi->bpi_lock, NULL);
#endif /* BK_USING_PTHREADS */

  bpi->bpi_ioh = ioh;
  bpi->bpi_flags = 0;
  bpi->bpi_size = 0;
  bpi->bpi_throttle_cnt = 0;
  bpi->bpi_tell = 0;

  if (BK_FLAG_ISSET(flags, BK_POLLING_THREADED))
    BK_FLAG_SET(bpi->bpi_flags, BPI_FLAG_THREADED);

  if (bk_ioh_update(B, ioh, NULL, NULL, NULL, polling_io_ioh_handler, bpi, 0, 0, 0, 0, BK_IOH_UPDATE_HANDLER | BK_IOH_UPDATE_OPAQUE) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not update ioh with blocking handler.\n");
    goto error;
  }

  BK_RETURN(B,bpi);
 error:
  if (bpi) bk_polling_io_destroy(B, bpi);
  BK_RETURN(B,NULL);
}



/**
 * Close up on demand I/O.
 *
 * <WARNING>
 * For various and sundry reasons it has become clear that ioh should be
 * closed here too. Therefore if you are using polling io then you should
 * surrender control of the ioh (or more to the point control it via the
 * polling routines). In this respect polling io is like b_relay.c.
 * </WARNING>
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param bpi The on demand state to close.
 *	@param flags Flags for the future.
 */
void
bk_polling_io_close(bk_s B, struct bk_polling_io *bpi, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bpi)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&bpi->bpi_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_FLAG_SET(bpi->bpi_flags, BPI_FLAG_CLOSING);
  if (BK_FLAG_ISSET(flags, BK_POLLING_CLOSE_FLAG_LINGER))
  {
    BK_FLAG_SET(bpi->bpi_flags, BPI_FLAG_DONT_DESTROY);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&bpi->bpi_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  bk_ioh_close(B, bpi->bpi_ioh, 0);

  BK_VRETURN(B);
}



/**
 * Destroy the blocking I/O context.
 *
 * THREADS: REENTRANT
 *
 *	@param B BAKA thread/global state.
 *	@param bpi. The context info to destroy.
 */
void
bk_polling_io_destroy(bk_s B, struct bk_polling_io *bpi)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct polling_io_data *pid;

  if (!bpi)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  while((pid=pidlist_minimum(bpi->bpi_data)))
  {
    if (pidlist_delete(bpi->bpi_data, pid) != DICT_OK)
      break;
    pid_destroy(B, pid);
  }
  pidlist_destroy(bpi->bpi_data);

#ifdef BK_USING_PTHREADS
  pthread_mutex_destroy(&bpi->bpi_lock);
#endif /* BK_USING_PTHREADS */

  free(bpi);
  BK_VRETURN(B);
}




/**
 * The on demand I/O subsystem's ioh handler. Remember this handles both
 * reads and writes.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param opaque My local data.
 *	@param ioh ioh over which the data arrived.
 *	@param state What's going on in the world.
 */
static void
polling_io_ioh_handler(bk_s B, bk_vptr *data, void *args, struct bk_ioh *ioh, bk_ioh_status_e status)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_polling_io *bpi = args;
  struct polling_io_data *pid = NULL;
  u_int data_cnt = 0;
  bk_vptr *ndata = NULL;
  char *p;
  int size = 0;
  int (*clc_add)(dict_h dll, dict_obj obj) = dll_append;

  if (!bpi || !ioh)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (!(pid = pid_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate pid: %s\n", strerror(errno));
    goto error;
  }

  switch (status)
  {
  case BkIohStatusReadComplete:
  case BkIohStatusIncompleteRead:
    // Copy the data--<BUG>why are we not using coalesce?</BUG> Sigh....
    for(data_cnt=0; data[data_cnt].ptr; data_cnt++)
    {
      size += data[data_cnt].len;
    }
    if (!(BK_CALLOC(ndata)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate copy vptr: %s\n", strerror(errno));
      goto error;
    }

    if (!(BK_CALLOC_LEN(ndata->ptr, size)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate copy data pointer: %s\n", strerror(errno));
      goto error;
    }

    p = ndata->ptr;
    for(data_cnt=0; data[data_cnt].ptr; data_cnt++)
    {
      memmove(p, data[data_cnt].ptr, data[data_cnt].len);
      p += data[data_cnt].len;
    }
    pid->pid_data = ndata;
    pid->pid_data->len = size;
    break;

  case BkIohStatusIohClosing:

#ifdef BK_USING_PTHREADS			// Lock to prevent race between user and I/O system
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&bpi->bpi_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */
    if (BK_FLAG_ISCLEAR(bpi->bpi_flags, BPI_FLAG_CLOSING))
    {
      bk_error_printf(B, BK_ERR_WARN, "Polling io underlying IOH was nuked before bpi closed. Not Good (probably not fatal).\n");
      // Forge on.
    }
#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&bpi->bpi_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */

    if (BK_FLAG_ISCLEAR(bpi->bpi_flags, BPI_FLAG_DONT_DESTROY))
    {
      // <BUG>There would appear to be a problem here since io_destroy does not notify user, so user will think he can use the bpi</BUG>
      bk_polling_io_destroy(B, bpi);
      if (pid) pid_destroy(B, pid);
      BK_VRETURN(B);
    }

#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&bpi->bpi_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */

    // The ioh is now dead.
    BK_FLAG_SET(bpi->bpi_flags, BPI_FLAG_IOH_DEAD);

#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&bpi->bpi_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */
    break;

  case BkIohStatusIohReadError:
#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&bpi->bpi_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */

    BK_FLAG_SET(bpi->bpi_flags, BPI_FLAG_READ_DEAD);

#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&bpi->bpi_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */

    break;

  case BkIohStatusIohWriteError:
#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&bpi->bpi_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */

    BK_FLAG_SET(bpi->bpi_flags, BPI_FLAG_WRITE_DEAD);

#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&bpi->bpi_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */

    break;

  case BkIohStatusIohReadEOF:
#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&bpi->bpi_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */

    BK_FLAG_SET(bpi->bpi_flags, BPI_FLAG_SAW_EOF);

#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&bpi->bpi_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */

    break;

  case BkIohStatusWriteComplete:
  case BkIohStatusWriteAborted:
    free(data->ptr);
    free(data);
    data = NULL;
    break;

  case BkIohStatusIohSeekSuccess:
    polling_io_flush(B, bpi, 0);
    // Intentional fall through.
  case BkIohStatusIohSeekFailed:
    clc_add = dll_insert;			// Put seek messages on front.
    break;

    // No default so gcc can catch missing cases.
  }

  if ((*clc_add)(bpi->bpi_data, pid) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not append data to data list: %s\n", pidlist_error_reason(bpi->bpi_data, NULL));
    goto error;
  }

  pid->pid_status = status;

  if (pid->pid_data)
  {
#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&bpi->bpi_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */

    bpi->bpi_size += pid->pid_data->len;

#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&bpi->bpi_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */

    /*
     * Pause reading if buffer is full.  <TODO> if file open for only writing
     * mark the case so we don't bother.</TODO>
     */
    if (ioh->ioh_readq.biq_queuemax && bpi->bpi_size >= ioh->ioh_readq.biq_queuemax)
    {
      bk_polling_io_throttle(B, bpi, 0);
    }
  }

  BK_VRETURN(B);

 error:
  if (ndata) bk_polling_io_data_destroy(B, ndata);
  if (pid) pid_destroy(B, pid);

  BK_VRETURN(B);
}



/**
 * Destroy a NULL terminated vptr list. The user <em>must</em> call this on
 * data read from the subsystem or the underlying memory will never be
 * freed.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param data The list of vptrs to nuke.
 */
void
bk_polling_io_data_destroy(bk_s B, bk_vptr *data)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!data)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }

  /*
   * Free up all the data.
   */
  if (data->ptr)
    free(data->ptr);
  free(data);

  BK_VRETURN(B);
}



/**
 * Do one polling read.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param bpi The polling state to use.
 *	@param datap Data to pass up to the user (copyout).
 *	@param statusp Status to pass up to the user (copyout).
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success (with data).
 *	@return <i>positive</i> on no progress.
 */
int
bk_polling_io_read(bk_s B, struct bk_polling_io *bpi, bk_vptr **datap, bk_ioh_status_e *status, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bpi || !datap || !status)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  *datap = NULL;

  // If ioh is dead, go away.. <BUG>If there is data still queued on the channel, return that first</BUG>
  if (BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_READ_DEAD))
  {
    bk_error_printf(B, BK_ERR_ERR, "Reading from dead channel\n");
    BK_RETURN(B, -1);
  }

  BK_RETURN(B,bk_polling_io_do_poll(B, bpi, datap, status, 0));
}




/**
 * Do one polling poll. If we have some data, dequeue it and
 * return. Otherwise call bk_run_once() one time.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param bpi The polling state to use.
 *	@param datap Data to pass up to the user (copyout).
 *	@param statusp Status to pass up to the user (copyout).
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success (with data).
 *	@return <i>positive</i> on no progress.
 */
int
bk_polling_io_do_poll(bk_s B, struct bk_polling_io *bpi, bk_vptr **datap, bk_ioh_status_e *status, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct polling_io_data *pid;
  bk_flags bk_run_once_flags;


  if (!bpi || !status)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (datap)
    *datap = NULL;

  if ((pid = pidlist_minimum(bpi->bpi_data)) ||
      BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_SAW_EOF))
  {
  // Seth sez: do bk_run_once regardless of presence of existing data to report.
    bk_run_once_flags = BK_RUN_ONCE_FLAG_DONT_BLOCK;
  }
  else
  {
    bk_run_once_flags = 0;
  }

#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISCLEAR(bpi->bpi_flags, BPI_FLAG_THREADED) || !BK_GENERAL_FLAG_ISTHREADON(B))
#endif /* BK_USING_PTHREADS */
  {
    if (BK_FLAG_ISCLEAR(bpi->bpi_flags, BPI_FLAG_IOH_DEAD) &&
	bk_run_once(B, bpi->bpi_ioh->ioh_run, bk_run_once_flags) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "polling bk_run_once failed severely\n");
      goto error;
    }
  }

  if (!pid)
    pid = pidlist_minimum(bpi->bpi_data);

  // Now that we either have data or might
  if (!pid)
  {
    /*
     * If the IOH is in an EOF state then return 0. However if there *is*
     * data then go ahead and return it. This will occur at *least* when
     * the pid contains the actual EOF message.
     */
    if (BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_SAW_EOF))
    {
      BK_RETURN(B,0);
    }
    BK_RETURN(B,1);
  }

  // You always send status
  *status = pid->pid_status;

  if (pidlist_delete(bpi->bpi_data, pid) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not delete pid from data list: %s\n", pidlist_error_reason(bpi->bpi_data, NULL));
    goto error;
  }


  // If this is read, then subtract data from my queue.
  if (pid->pid_status == BkIohStatusReadComplete ||
      pid->pid_status == BkIohStatusIncompleteRead)
  {
    /*
     * We're removing data which is has been read by the user, so we
     * decrease the amount of data on the queue and increase our "tell"
     * position.
     */
    bpi->bpi_size -= pid->pid_data->len;
    bpi->bpi_tell += pid->pid_data->len;
  }


  /*
   * Enable reading if buffer is not full.  <TODO> if file open for only
   * writing mark the case so we don't bother.</TODO> <WARNING>Requires
   * unthrottle as no-op if not throttled.</WARNING>
   */
  if (BK_FLAG_ISCLEAR(bpi->bpi_flags, BPI_FLAG_IOH_DEAD) &&
      bpi->bpi_ioh->ioh_readq.biq_queuemax &&
      bpi->bpi_size < bpi->bpi_ioh->ioh_readq.biq_queuemax)
  {
    bk_polling_io_unthrottle(B, bpi, 0);
  }

  if (datap)
  {
    *datap = pid->pid_data;
    pid->pid_data = NULL;			// Passed off. We're not responsible anymore.
  }

  pid_destroy(B, pid);

  BK_RETURN(B,0);

 error:
  BK_RETURN(B,-1);
}



/**
 * Write out a buffer polling. Basically <em>all</em> bk_ioh writes are
 * "polling", but we need this function to provide the glue between some
 * layers which might not have access to the @a bk_ioh structure and the
 * actual ioh level.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param bpi The @a bk_polling_io struct to use.
 *	@param data The data to write out.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_polling_io_write(bk_s B, struct bk_polling_io *bpi, bk_vptr *data, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret;


  if (!bpi || !data)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }


  // If ioh is dead, go away..
  if (BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_WRITE_DEAD))
  {
    bk_error_printf(B, BK_ERR_ERR, "Reading from dead channel\n");
    BK_RETURN(B, -1);
  }

  /*
   * Keep trying to write until we either succeed or fail miserably. This
   * works because the underlying ioh routines *will* permit a single write
   * which exceeds the queue max (assuming there is one) to progress
   * provided that the queue is *empty* when the write is intiated. In all
   * other cases the data is not queued and the write returns 1. So as long
   * as we get > 0 as a return code we try to drain the queue. Eventually
   * the queue *will* drain (or we will fail miserably) and no matter how
   * large ddata is, the next bk_ioh_write will succeed.
   */
  while ((ret = bk_ioh_write(B, bpi->bpi_ioh, data, 0)) > 0)
  {
#ifdef BK_USING_PTHREADS
    if (BK_FLAG_ISCLEAR(bpi->bpi_flags, BPI_FLAG_THREADED) || !BK_GENERAL_FLAG_ISTHREADON(B))
#endif /* BK_USING_PTHREADS */
    {
      if (BK_FLAG_ISCLEAR(bpi->bpi_flags, BPI_FLAG_IOH_DEAD) &&
	  bk_run_once(B, bpi->bpi_ioh->ioh_run, BK_RUN_ONCE_FLAG_DONT_BLOCK) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "polling bk_run_once failed severely\n");
	BK_RETURN(B,-1);
      }
    }
  }

  if (ret < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not submit write\n");
    BK_RETURN(B,-1);
  }
  else
  {
#ifdef BK_USING_PTHREADS
    if (BK_FLAG_ISCLEAR(bpi->bpi_flags, BPI_FLAG_THREADED) || !BK_GENERAL_FLAG_ISTHREADON(B))
#endif /* BK_USING_PTHREADS */
    {
      // Allow our now successfull write a change to go out.
      if (BK_FLAG_ISCLEAR(bpi->bpi_flags, BPI_FLAG_IOH_DEAD) &&
	  bk_run_once(B, bpi->bpi_ioh->ioh_run, BK_RUN_ONCE_FLAG_DONT_BLOCK) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "polling bk_run_once failed severely\n");
	BK_RETURN(B,-1);
      }
    }
  }

  BK_RETURN(B,0);
}



/**
 * Create an on_demand_data structure.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a bk_on_demand_data on success.
 */
static struct polling_io_data *
pid_create(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct polling_io_data *pid;

  if (!(BK_CALLOC(pid)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate pid: %s\n", strerror(errno));
    BK_RETURN(B,NULL);
  }
  BK_RETURN(B, pid);
}



/**
 * Destroy an pid.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param pid The @a bk_on_demand_data to destroy.
 */
static void
pid_destroy(bk_s B, struct polling_io_data *pid)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  if (!pid)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (pid->pid_data) bk_polling_io_data_destroy(B, pid->pid_data);
  free(pid);
  BK_VRETURN(B);
}



/**
 * Throttle an polling I/O stream.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param bpi The @a bk_polling_io to throttle.
 *	@param flags Flag for the future.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_polling_io_throttle(bk_s B, struct bk_polling_io *bpi, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bpi)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_READ_DEAD))
  {
    bk_error_printf(B, BK_ERR_ERR, "Attempt to throttle a dead ioh\n");
    BK_RETURN(B,-1);
  }

  if (bpi->bpi_throttle_cnt == 0)
  {
    bk_ioh_readallowed(B, bpi->bpi_ioh, 0, 0);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&bpi->bpi_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  bpi->bpi_throttle_cnt++;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&bpi->bpi_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B,0);
}



/**
 * Unthrottle an polling I/O stream.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param bpi The @a bk_polling_io to unthrottle.
 *	@param flags Flag for the future.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_polling_io_unthrottle(bk_s B, struct bk_polling_io *bpi, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int actual = 0;

  if (!bpi)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_READ_DEAD))
  {
    bk_error_printf(B, BK_ERR_ERR, "Attempt to unthrottle a dead ioh\n");
    BK_RETURN(B,-1);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&bpi->bpi_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  // <TRICKY>Other code requires unthrottle as no-op if not throttled.</TRICKY>
  if (bpi->bpi_throttle_cnt != 0)
    if (--bpi->bpi_throttle_cnt == 0)
      actual = 1;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&bpi->bpi_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (actual)
    bk_ioh_readallowed(B, bpi->bpi_ioh, 1, 0);

  BK_RETURN(B,0);
}




/**
 * Flush all data associated with polling stuff.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param bpi @a bk_polling_io to flush.
 *	@param flags Flags for the future.
 */
void
bk_polling_io_flush(bk_s B, struct bk_polling_io *bpi, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct polling_io_data *pid, *npid;

  if (!bpi)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }

  pid = pidlist_maximum(bpi->bpi_data);
  while(pid)
  {
    npid = pidlist_successor(bpi->bpi_data, pid);
    // Only nuke data vbufs.
    if (pid->pid_data->ptr || pid->pid_status == BkIohStatusIohReadEOF)
    {
      // If we're flushing off an EOF, then clear the fact that we have seen EOF.
      if (pid->pid_status == BkIohStatusIohReadEOF)
      {
#ifdef BK_USING_PTHREADS
	if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&bpi->bpi_lock) != 0)
	  abort();
#endif /* BK_USING_PTHREADS */

	BK_FLAG_CLEAR(bpi->bpi_flags, BPI_FLAG_SAW_EOF);

#ifdef BK_USING_PTHREADS
	if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&bpi->bpi_lock) != 0)
	  abort();
#endif /* BK_USING_PTHREADS */
      }
      pidlist_delete(bpi->bpi_data, pid);
      // Failure here will not kill the loop since we've already grabbed successor
      pid_destroy(B, pid);
    }

    pid = npid;
  }

  BK_VRETURN(B);
}




/**
 * Flush out the polling cache. Very similar to ioh flush. Flush all data buf and EOF messages.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param bpi The @a bk_polling_io to use.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
polling_io_flush(bk_s B, struct bk_polling_io *bpi, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct polling_io_data *pid, *npid;

  if (!bpi)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  pid = pidlist_minimum(bpi->bpi_data);
  while(pid)
  {
    npid = pidlist_successor(bpi->bpi_data, pid);
    if (pid->pid_data->ptr || pid->pid_status == BkIohStatusIohReadEOF)
    {
      if (pidlist_delete(bpi->bpi_data, pid) != DICT_OK)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not delete pid from bpi data list: %s\n", pidlist_error_reason(bpi->bpi_data, NULL));
	break;
      }

      if (pid->pid_data)
      {
#ifdef BK_USING_PTHREADS
	if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&bpi->bpi_lock) != 0)
	  abort();
#endif /* BK_USING_PTHREADS */

	/*
	 * We're removing data which *hasn't'* been read by the user so we
	 * reduce *both* the amount of data on the queue and our tell
	 * position (in the io_poll routine we *increased* the later.
	 */
	bpi->bpi_size -= pid->pid_data->len;
	bpi->bpi_tell -= pid->pid_data->len;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&bpi->bpi_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

	// <WARNING>Requires unthrottle as no-op if not throttled.</WARNING>
	if (bpi->bpi_ioh->ioh_readq.biq_queuemax &&
	    bpi->bpi_size < bpi->bpi_ioh->ioh_readq.biq_queuemax)
	{
	  bk_polling_io_unthrottle(B, bpi, 0);
	}
      }
      pid_destroy(B, pid);
    }
    pid = npid;
  }
  BK_RETURN(B,0);
}





/**
 * Register a bpi for cacellation.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param bpi The @a bk_polling_io struct to register.
 *	@param flags Flags passed through.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_polling_io_cancel_register(bk_s B, struct bk_polling_io *bpi, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bpi)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  BK_RETURN(B,bk_ioh_cancel_register(B, bpi->bpi_ioh, flags));
}





/**
 * Unregister a bpi for cacellation.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param bpi The @a bk_polling_io struct to unregister.
 *	@param flags Flags passed through.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_polling_io_cancel_unregister(bk_s B, struct bk_polling_io *bpi, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bpi)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  BK_RETURN(B,bk_ioh_cancel_unregister(B, bpi->bpi_ioh, flags));
}





/**
 * Check if a @a bk_polling_io has been registered for cancellation.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param bpi The @a bk_polling_io to use.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success and <b>not</b> registered.
 *	@return <i>0</i> on success and registered.
 */
int
bk_polling_io_is_canceled(bk_s B, struct bk_polling_io *bpi, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bpi)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  BK_RETURN(B,bk_ioh_is_canceled(B, bpi->bpi_ioh, 0));
}




/**
 * Cancel a bpi.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param bpi The @a bk_polling_io to cancel.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_polling_io_cancel(bk_s B, struct bk_polling_io *bpi, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bpi)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  BK_RETURN(B,bk_ioh_cancel(B, bpi->bpi_ioh, flags));
}
