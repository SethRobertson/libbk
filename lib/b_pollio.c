#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_pollio.c,v 1.23 2003/05/14 22:22:33 jtt Exp $";
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
 *
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
 * All the state which the on demand I/O subsytem requires.
 */
struct bk_polling_io
{
  bk_flags		bpi_flags;		///< Everyone needs flags.
#define BPI_FLAG_CLOSING		0x1	///< We are closing down bpi.
#define BPI_FLAG_READ_DEAD		0x2	///< Read side is finished.
#define BPI_FLAG_WRITE_DEAD		0x4	///< Write side is finished.
#define BPI_FLAG_SAW_EOF		0x8	///< We have seen EOF.
#define BPI_FLAG_DONT_DESTROY		0x10	///< Tell io hander not destroy bpi.
#define BPI_FLAG_IOH_DEAD		0x20	///< Bpi not destroyed, ioh was
#define BPI_FLAG_THREADED		0x40	///< Don't worry about recursively calling bk_run
#define BPI_FLAG_SYNC			0x80	///< Synchronous write
#define BPI_FLAG_LINGER			0x100	///< Synchronous write
  u_int			bpi_size;		///< Amount of data I'm buffering.
  dict_h		bpi_data;		///< Queue of data vptrs.
  struct bk_ioh *	bpi_ioh;		///< Ioh structure.
  u_int			bpi_throttle_cnt;	///< Count the number of people who want to throttle me.
  int64_t		bpi_tell;		///< Where we are in the stream.
  u_int			bpi_wroutstanding;	///< Number of outstanding writes
  void		       *bpi_rdtimeoutevent;	///< Timeout event handle for rd timeout
  void		       *bpi_wrtimeoutevent;	///< Timeout event handle for wr timeout
#ifdef BK_USING_PTHREADS
  pthread_mutex_t	bpi_lock;		///< Lock on bpi management
  pthread_cond_t	bpi_wrcond;		///< Forward progress on writes?
  pthread_cond_t	bpi_rdcond;		///< Forward progress on reads?
#endif /* BK_USING_PTHREADS */
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
static void bpi_rdtimeout(bk_s B, struct bk_run *run, void *opaque, const struct timeval *stattime, bk_flags flags);
static void bpi_wrtimeout(bk_s B, struct bk_run *run, void *opaque, const struct timeval *stattime, bk_flags flags);
static void bk_polling_io_destroy(bk_s B, struct bk_polling_io *bpi);



/**
 * Create the context for on demand operation. <em>NB</em> We take control
 * of the @a ioh, just like in bk_relay().
 *
 * THREADS: MT-SAFE
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
  pthread_cond_init(&bpi->bpi_wrcond, NULL);
  pthread_cond_init(&bpi->bpi_rdcond, NULL);
#endif /* BK_USING_PTHREADS */

  bpi->bpi_ioh = ioh;
  bpi->bpi_flags = 0;
  bpi->bpi_size = 0;
  bpi->bpi_throttle_cnt = 0;
  bpi->bpi_tell = 0;

  if (BK_FLAG_ISSET(flags, BK_POLLING_SYNC))
    BK_FLAG_SET(bpi->bpi_flags, BPI_FLAG_SYNC);

  if (BK_FLAG_ISSET(flags, BK_POLLING_LINGER))
    BK_FLAG_SET(bpi->bpi_flags, BPI_FLAG_LINGER);

  BK_FLAG_SET(bpi->bpi_flags, BPI_FLAG_DONT_DESTROY);

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
  struct polling_io_data *pid;

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
  BK_FLAG_CLEAR(bpi->bpi_flags, BPI_FLAG_DONT_DESTROY);
  if (BK_FLAG_ISSET(flags, BK_POLLING_LINGER))
  {
    BK_FLAG_SET(bpi->bpi_flags, BPI_FLAG_LINGER);
  }

  if (BK_FLAG_ISSET(flags, BK_POLLING_DONT_LINGER))
  {
    BK_FLAG_CLEAR(bpi->bpi_flags, BPI_FLAG_LINGER);
  }

  // Nuke everything from the cached read list
  while (pid = pidlist_minimum(bpi->bpi_data))
  {
    if (pidlist_delete(bpi->bpi_data, pid) != DICT_OK)
      break;
    pid_destroy(B, pid);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&bpi->bpi_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_IOH_DEAD))
  {
    bk_polling_io_destroy(B, bpi);
  }
  else
  {
    bk_ioh_shutdown(B, bpi->bpi_ioh, SHUT_RD, 0);
    bk_ioh_close(B, bpi->bpi_ioh, (BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_LINGER)?0:BK_IOH_ABORT));
  }

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
static void
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
  pthread_cond_destroy(&bpi->bpi_wrcond);
  pthread_cond_destroy(&bpi->bpi_rdcond);
#endif /* BK_USING_PTHREADS */

  free(bpi);
  BK_VRETURN(B);
}



/**
 * Seek to a position in a blocking way.
 *
 * THREADS: MT-SAFE (assuming different bib)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param bpi The @a bk_polling_io to use.
 *	@param ioh The @a bk_ioh to use.
 *	@param offset The offset (as in @a lseek(2)).
 *	@param whence Whence (as in @a lseek(2)).
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_polling_io_seek(bk_s B, struct bk_polling_io *bpi, off_t offset, int whence, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  bk_ioh_status_e status;
  int ret = 0;
  bk_vptr *discard;

  if (!bpi)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_CLOSING|BPI_FLAG_READ_DEAD))
  {
    bk_error_printf(B, BK_ERR_ERR, "Polling has been closed (perhaps for reading only), cannot seek.\n");
    BK_RETURN(B, -1);
  }

  if (bk_ioh_seek(B, bpi->bpi_ioh, offset, whence) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not ioh seek\n");
    goto error;
  }

  while (1)
  {
    ret = bk_polling_io_read(B, bpi, &discard, &status, 0, 0);

    if (ret < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Seek failed: could not read up until status message\n");
      goto error;
    }

    if (ret == 0)
    {
      if (status == BkIohStatusIohSeekSuccess ||
	  status == BkIohStatusIohSeekFailed)
	break;

      if (discard && discard->ptr)
      {
	free(discard->ptr);
	bpi->bpi_tell += discard->len;	      // In case seek fails...
      }
    }
  }

  switch (status)
  {
  case BkIohStatusIohSeekSuccess:
    bpi->bpi_tell = offset;
    ret = 0;
    break;
  case BkIohStatusIohSeekFailed:
    // Note data may have been discarded...
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
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param bpi The @a bk_polling_io structure to tell on.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int64_t
bk_polling_io_tell(bk_s B, struct bk_polling_io *bpi, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bpi)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_CLOSING|BPI_FLAG_READ_DEAD))
  {
    bk_error_printf(B, BK_ERR_ERR, "Polling has been closed (perhaps for reading only), cannot seek.\n");
    BK_RETURN(B, -1);
  }

  BK_RETURN(B, bpi->bpi_tell);
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
    // Coalesce into one buffer for output
    if (!(pid->pid_data  = bk_ioh_coalesce(B, data, NULL, BK_IOH_COALESCE_FLAG_MUST_COPY, NULL)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not coalesce relay data\n");
      goto error;
    }

#ifdef BK_USING_PTHREADS
    pthread_cond_broadcast(&bpi->bpi_rdcond);
#endif /* BK_USING_PTHREADS */

    break;

  case BkIohStatusIohClosing:
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

    bpi->bpi_wroutstanding--;
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
#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&bpi->bpi_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */
    bpi->bpi_wroutstanding--;
#ifdef BK_USING_PTHREADS
    pthread_cond_broadcast(&bpi->bpi_wrcond);
#endif /* BK_USING_PTHREADS */
#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&bpi->bpi_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */
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
 * Do one polling read.  You will return when data is available, or
 * when the timeout has expired, or when the channel has been canceled.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param bpi The polling state to use.
 *	@param datap Data to pass up to the user (copyout).
 *	@param statusp Status to pass up to the user (copyout).
 *	@param timeout Maximum time to wait in milliseconds (0->forever, -1->no wait)
 *	@param flags Fun for the future
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success (with data).
 *	@return <i>positive</i> on no progress.
 */
int
bk_polling_io_read(bk_s B, struct bk_polling_io *bpi, bk_vptr **datap, bk_ioh_status_e *status, time_t timeout, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct polling_io_data *pid;
  int ret = 0;
  int timedout = 0;

  if (!bpi || !datap || !status)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  *datap = NULL;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&bpi->bpi_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (timeout > 0
#ifdef BK_USING_PTHREADS
      && BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_THREADED) && BK_GENERAL_FLAG_ISTHREADON(B)
#endif /* BK_USING_PTHREADS */
      )
  {
    if (bk_run_enqueue_delta(B, bpi->bpi_ioh->ioh_run, timeout, bpi_rdtimeout, bpi, &bpi->bpi_rdtimeoutevent, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not enqueue new pollio timeout event\n");
      ret = -1;
      goto unlockexit;
    }
  }

  while (!(pid = pidlist_minimum(bpi->bpi_data)) && !timedout)
  {
    if (BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_SAW_EOF) || BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_READ_DEAD) || BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_IOH_DEAD) || bk_polling_io_is_canceled(B, bpi, 0))
    {
      bk_error_printf(B, BK_ERR_ERR, "Reading from dead/canceled channel\n");
      ret = -1;
      goto unlockexit;
    }

    if (timeout == -1 || timedout)
    {
      bk_error_printf(B, BK_ERR_ERR, "No further progress possible\n");
      ret = 1;
      goto unlockexit;
    }

#ifdef BK_USING_PTHREADS
    if (BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_THREADED) && BK_GENERAL_FLAG_ISTHREADON(B))
    {
      struct timespec ts;

      if (timeout == 0)
      {
	pthread_cond_wait(&bpi->bpi_rdcond, &bpi->bpi_lock);
      }
      else
      {
	ts.tv_sec = timeout / 1000;
	ts.tv_nsec = (timeout % 1000) * 1000000;
	pthread_cond_timedwait(&bpi->bpi_rdcond, &bpi->bpi_lock, &ts);
	timedout++;
      }
    }
    else
#endif /* BK_USING_PTHREADS */
    {
#ifdef BK_USING_PTHREADS
      if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&bpi->bpi_lock) != 0)
	abort();
#endif /* BK_USING_PTHREADS */
      if (bk_run_once(B, bpi->bpi_ioh->ioh_run, 0) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "polling bk_run_once failed severely\n");
	ret = -1;
	goto unlockexit;
      }
#ifdef BK_USING_PTHREADS
      if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&bpi->bpi_lock) != 0)
	abort();
#endif /* BK_USING_PTHREADS */

      if (timeout > 0 && !bpi->bpi_wrtimeoutevent)
	timedout++;
    }
  }

  if (ret == 0)
  {
    if (pid->pid_data)
    {
      bpi->bpi_tell += pid->pid_data->len;
      bpi->bpi_size -= pid->pid_data->len;
      *datap = pid->pid_data;
    }
    *status = pid->pid_status;
    pid_destroy(B, pid);
  }

 unlockexit:
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&bpi->bpi_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B, ret);
}



/**
 * Write out a buffer polling.  You will return when the data has been
 * committed to the IOH subsystem (typically immediately unless the
 * ioh is full) or when the timeout has expired.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param bpi The @a bk_polling_io struct to use.
 *	@param data The data to wirte out.
 *	@param timeout Maximum time to wait in milliseconds (0->forever, -1->no wait)
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 *	@return <i>1</i> on timeout/cancel
 */
int
bk_polling_io_write(bk_s B, struct bk_polling_io *bpi, bk_vptr *data, time_t timeout, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret = -1;
  int timedout = 0;

  if (!bpi || !data)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }


  // If ioh is dead, go away..
  if (BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_WRITE_DEAD) || BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_IOH_DEAD))
  {
    bk_error_printf(B, BK_ERR_ERR, "Writing to dead channel\n");
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

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&bpi->bpi_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (timeout > 0
#ifdef BK_USING_PTHREADS
      && BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_THREADED) && BK_GENERAL_FLAG_ISTHREADON(B)
#endif /* BK_USING_PTHREADS */
      )
  {
    if (bk_run_enqueue_delta(B, bpi->bpi_ioh->ioh_run, timeout, bpi_wrtimeout, bpi, &bpi->bpi_wrtimeoutevent, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not enqueue new pollio timeout event\n");
      ret = -1;
      goto unlockexit;
    }
  }

  while ((ret = bk_ioh_write(B, bpi->bpi_ioh, data, 0)) > 0 && !timedout)
  {
    if (bk_polling_io_is_canceled(B, bpi, 0))
    {
      ret = -1;
      goto unlockexit;
    }

    if (timeout == -1 || timedout)
    {
      ret = 1;
      goto unlockexit;
    }

#ifdef BK_USING_PTHREADS
    if (BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_THREADED) && BK_GENERAL_FLAG_ISTHREADON(B))
    {
      struct timespec ts;

      if (timeout == 0)
      {
	pthread_cond_wait(&bpi->bpi_wrcond, &bpi->bpi_lock);
      }
      else
      {
	ts.tv_sec = timeout / 1000;
	ts.tv_nsec = (timeout % 1000) * 1000000;
	pthread_cond_timedwait(&bpi->bpi_wrcond, &bpi->bpi_lock, &ts);
      }
    }
    else
#endif /* BK_USING_PTHREADS */
    {
#ifdef BK_USING_PTHREADS
      if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&bpi->bpi_lock) != 0)
	abort();
#endif /* BK_USING_PTHREADS */
      if (bk_run_once(B, bpi->bpi_ioh->ioh_run, 0) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "polling bk_run_once failed severely\n");
	ret = -1;
	goto unlockexit;
      }
#ifdef BK_USING_PTHREADS
      if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&bpi->bpi_lock) != 0)
	abort();
#endif /* BK_USING_PTHREADS */

      if (timeout > 0 && !bpi->bpi_wrtimeoutevent)
	timedout++;
    }
  }

  // Made it in time--need to dequeue timeout event
  if (timeout > 0 && bpi->bpi_wrtimeoutevent && !timedout)
  {
    bk_run_dequeue(B, bpi->bpi_ioh->ioh_run, bpi->bpi_wrtimeoutevent, BK_RUN_DEQUEUE_EVENT);
  }


  if (ret < 0)
    bk_error_printf(B, BK_ERR_ERR, "Could not submit write\n");

  if (ret == 0)
    bpi->bpi_wroutstanding++;

  // Handle requests to wait until data has hit OS
  if (BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_SYNC))
  {
#ifdef BK_USING_PTHREADS
    if (BK_FLAG_ISSET(bpi->bpi_flags, BPI_FLAG_THREADED) && BK_GENERAL_FLAG_ISTHREADON(B))
    {
      int waitcount = bpi->bpi_wroutstanding;

      /*
       * <BUG>Note that timeouts do not work here, nor is it clear
       * what exactly they should do, since the data is somewhere in
       * the ioh can no longer be recalled, so what return value could
       * I return?</BUG>
       */
       while (waitcount-- > 0)
       {
	 pthread_cond_wait(&bpi->bpi_wrcond, &bpi->bpi_lock);
       }
    }
    else
#endif /* BK_USING_PTHREADS */
    {
      // <BUG>This might wait for more time than necessary, since other writes can come in..</BUG>
      while (bpi->bpi_wroutstanding > 0)
      {
	if (bk_run_once(B, bpi->bpi_ioh->ioh_run, 0) < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "polling bk_run_once failed severely\n");
	  ret = -1;
	  goto unlockexit;
	}
      }
    }
  }

 unlockexit:
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&bpi->bpi_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B, ret);
}



#if 0
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
#endif



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
 *	@return <i>1</i> on success and registered.
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

  BK_RETURN(B, bk_ioh_is_canceled(B, bpi->bpi_ioh, 0));
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



/**
 * Get error message from a bpi
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param bpi The @a bk_polling_io to query
 *	@return an explanatory error message.<br>
 *	@return NULL if we don't have a clue.
 */
const char *
bk_polling_io_geterr(bk_s B, struct bk_polling_io *bpi)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int err;

  if (!bpi || !bpi->bpi_ioh)
    err = EINVAL;
  else
    err = bpi->bpi_ioh->ioh_errno;

  if (!err)
    BK_RETURN(B, NULL);


  BK_RETURN(B, strerror(err));
}



/**
 * Timeout a polling_io read.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param run The bk_run struct.
 *	@param opaque Data the caller asked to be returned.
 *	@param starttime The starting time of this event run.
 *	@param flags Flags.
 */
static void
bpi_rdtimeout(bk_s B, struct bk_run *run, void *opaque, const struct timeval *stattime, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_polling_io *bpi = opaque;

  if (!run || !bpi)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&bpi->bpi_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  /*
   * <BUG>Race condition between event fire lock obtaining (on one
   * side), and event dequeue and new requeue on other</BUG>
   */

  bpi->bpi_rdtimeoutevent = NULL;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&bpi->bpi_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_VRETURN(B);
}



/**
 * Timeout a polling_io write.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param run The bk_run struct.
 *	@param opaque Data the caller asked to be returned.
 *	@param starttime The starting time of this event run.
 *	@param flags Flags.
 */
static void
bpi_wrtimeout(bk_s B, struct bk_run *run, void *opaque, const struct timeval *stattime, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_polling_io *bpi = opaque;

  if (!run || !bpi)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&bpi->bpi_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  /*
   * <BUG>Race condition between event fire lock obtaining (on one
   * side), and event dequeue and new requeue on other</BUG>
   */

  bpi->bpi_wrtimeoutevent = NULL;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&bpi->bpi_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_VRETURN(B);
}
