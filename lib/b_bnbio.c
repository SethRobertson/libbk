#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_bnbio.c,v 1.7 2002/03/06 22:51:46 dupuy Exp $";
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

#define SHOULD_LINGER(bib,flags) ((BK_FLAG_ISSET((bib)->bib_flags, BK_IOHH_BNBIO_FLAG_SYNC) && BK_FLAG_ISCLEAR((flags), BK_IOHH_BNBIO_FLAG_NO_LINGER)) || (BK_FLAG_ISSET((flags), BK_IOHH_BNBIO_FLAG_LINGER)))




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
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bib: %s\n", strerror(errno));
    BK_RETURN(B,NULL);
  }

  if (!(bib->bib_bpi = bk_polling_io_create(B, ioh, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create polling structure\n");
    goto error;
  }

  bib->bib_flags = flags;

  BK_RETURN(B,bib);
  
 error:
  if (bib) 
    bk_iohh_bnbio_destroy(B, bib);
  BK_RETURN(B,NULL);
  
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
 *	@param B BAKA thread/global state.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>size of read</i> on success (0 means EOF).
 */
int
bk_iohh_bnbio_read(bk_s B, struct bk_iohh_bnbio *bib, bk_vptr **datap, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  bk_ioh_status_e status;
  int ret;

  if (!bib || !datap)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

 reread:
  // first, generate *some* form of useful information
  while ((ret = bk_polling_io_read(B, bib->bib_bpi, datap, &status, 0)) == 1)
    /* void */;

  if (ret < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "reading from iohh poll level failed severely\n");
    BK_RETURN(B,-1);
  }

  if (ret == 0 && !(*datap))
  {
    // EOF indicator, just pass this along.
    BK_RETURN(B,0);
  }

  switch (status)
  {
  case BkIohStatusIncompleteRead:
  case BkIohStatusReadComplete:
    BK_RETURN(B,(*datap)->len);
    break;

  case BkIohStatusIohReadError:
    // Don't log anything here since this error actually occured a long time ago
    BK_RETURN(B,-1);
    break;
    

  case BkIohStatusWriteAborted:
  case BkIohStatusWriteComplete:
    // Maybe free data....
    // Intentional fall through
  case BkIohStatusIohWriteError:
    // Don't log anything here since this error actually occured a long time ago
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
  
  BK_RETURN(B,0);
}




/**
 * Write out a buffer to blocking system possibly "lingering" untill we know that it's written
 *
 *	@param B BAKA thread/global state.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_iohh_bnbio_write(bk_s B, struct bk_iohh_bnbio *bib, bk_vptr *data, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  bk_ioh_status_e status;
  bk_vptr *ddata;

  if (!bib || !data)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (bk_polling_io_write(B, bib->bib_bpi, data, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not write out data\n");
    BK_RETURN(B,-1);
  }

  // Wait for the buffer to return.
  if ((BK_FLAG_ISSET(bib->bib_flags, BK_IOHH_BNBIO_FLAG_SYNC) && 
       BK_FLAG_ISCLEAR(flags, BK_IOHH_BNBIO_FLAG_NO_LINGER)) || 
      (BK_FLAG_ISSET(flags, BK_IOHH_BNBIO_FLAG_SYNC)))
  {
    // <WARNING> HIDEOUS LAYER VIOLATION </WARNING>
    while (bib->bib_bpi->bpi_ioh->ioh_writeq.biq_queuelen > 0)
    {
      if (bk_polling_io_do_poll(B, bib->bib_bpi, &ddata, &status, 0) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "polling run failed seveerly\n");
	BK_RETURN(B,-1);      
      }

    /*
     * Continue checking the queue even if ret == 1 (which generally means
     * "no progress". The problem here is that we don't return write
     * buffers back for freeing ('cause we don't always linger), so it's
     * quite possible for us to make forward progress in terms of *writing*
     * out data without the poll subsystem really knowing it.
     */
    }

  }
  BK_RETURN(B,0);  
}




/**
 * Seek to a position in a blocking way.
 *
 *	@param B BAKA thread/global state.
 *	@param bib The @a bk_iohh_bnbio to use.
 *	@param ioh The @a bk_ioh to use.
 *	@param offset The offset (ala @a lseek(2)).
 *	@param whence Wence (ala @a lseek(2)).
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
    bk_error_printf(B, BK_ERR_ERR, "polling run failed seveerly\n");
    BK_RETURN(B,-1);      
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
    bk_error_printf(B, BK_ERR_ERR, "Unexpected or unknown status from seek: %d\n", status);
    
  }
    
  BK_RETURN(B,ret);
  
 error:
  BK_RETURN(B,-1);
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
  BK_RETURN(B,bib->bib_bpi->bpi_tell);  
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


  // <TODO> Jtt is not at all sure that this is correct and won't leak memory </TODO>

  if ((BK_FLAG_ISSET(bib->bib_flags, BK_IOHH_BNBIO_FLAG_LINGER) && 
       BK_FLAG_ISCLEAR(flags, BK_IOHH_BNBIO_FLAG_NO_LINGER)) || 
      (BK_FLAG_ISSET(flags, BK_IOHH_BNBIO_FLAG_LINGER)))
  {
    bk_polling_io_close(B, bib->bib_bpi, BK_POLLING_CLOSE_FLAG_LINGER);
    while (((ret = bk_polling_io_do_poll(B, bib->bib_bpi, &data, &status, 0)) >= 0) && 
	   (status != BkIohStatusIohClosing))
    {
      if (data)
      {
	bk_polling_io_data_destroy(B, data);
      }
    }

    if (ret < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "polling run failed seveerly\n");
      BK_VRETURN(B);
    }
    // We have to destroy this ourselves now.
    bk_polling_io_destroy(B, bib->bib_bpi);
  }
  else
  {
    // We're not linger so we can just let the run loop run its course when it chooses to.
    bk_polling_io_close(B, bib->bib_bpi, 0);
  }
  
  bib->bib_bpi = NULL;
  bk_iohh_bnbio_destroy(B, bib);
  BK_VRETURN(B);
}
