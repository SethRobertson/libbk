#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_memx.c,v 1.11 2002/11/11 22:53:58 jtt Exp $";
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
 * Extendible buffer management routines (dynamicly sized arrays).
 */

#include <libbk.h>
#include "libbk_internal.h"


// XX Alex sez: we need an accessor function for bm_curused(definitely) and bm_unitsize(maybe).

/**
 * Information about an extensible buffer being managed
 */
struct bk_memx
{
  void		*bm_array;			///< Extensible memory
  size_t	bm_unitsize;			///< Size of units
  size_t	bm_curalloc;			///< Allocated memory
  size_t	bm_curused;			///< Current used
  u_int		bm_incr;			///< Increment amount
  bk_flags	bm_flags;			///< Fun for the future
};




/**
 * Create the extensible buffer management state along with an initial allocation
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state
 *	@param objsize Object size in bytes/octets
 *	@param start_hint Number of objects to start out with
 *	@param incr_hint Number of objects to grow when more are needed
 *	@param flags Fun for the future
 *	@return <i>NULL</i> on call failure, allocation failure
 *	@return <br><i>Buffer handle</i> on success
 */
struct bk_memx *bk_memx_create(bk_s B, size_t unitsize, u_int start_hint, u_int incr_hint, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_memx *ret;

  if (unitsize < 1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  if (start_hint < 1) start_hint = 1;
  if (incr_hint < 1) incr_hint = 1;

  if (!(ret = malloc(sizeof *ret)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate memory manager: %s\n",strerror(errno));
    BK_RETURN(B, NULL);
  }
  ret->bm_unitsize = unitsize;
  ret->bm_curused = 0;
  ret->bm_incr = incr_hint;
  ret->bm_flags = flags;

  if (!(ret->bm_array = malloc(unitsize * start_hint)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate initial memory: %s\n",strerror(errno));
    goto error;
  }
  ret->bm_curalloc = start_hint;

  BK_RETURN(B, ret);

 error:
  if (ret)
    bk_memx_destroy(B, ret, 0);
  BK_RETURN(B, NULL);
}



/**
 * Destroy buffer and management
 *
 * THREADS: MT-SAFE (as long as bm is thread-private)
 *
 *	@param B BAKA Thread/global state 
 *	@param bm Buffer management handle
 *	@param flags BK_MEMX_PRESERVE_ARRAY if the allocated memory
 *		must live on (will be free'd later) but the dynamic buffer
 *		management side should still go away
 */
void bk_memx_destroy(bk_s B, struct bk_memx *bm, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bm)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  if (bm->bm_array && BK_FLAG_ISCLEAR(flags, BK_MEMX_PRESERVE_ARRAY))
    free(bm->bm_array);
  free(bm);

  BK_VRETURN(B);
}



/**
 * Get an element (new or otherwise)
 *
 * THREADS: MT-SAFE (as long as bm is thread-private)
 *
 *	@param B BAKA Thread/global state
 *	@param bm Buffer management handle
 *	@param count Number of new elements to obtain (GETNEW) or location of old element to return (!GETNEW)
 *	@param curused Copy-out the number of elements currently in use
 *	@param flags BK_MEMX_GETNEW will request a new allocation; otherwise will request a prior allocation
 *	@return <i>NULL</i> on call failure, allocation failure
 *	@return <br><i>new allocation</i> (GETNEW) pointer to first of new allocation (array layout)
 *	@return <br><i>old allocation</i> (!GETNEW) pointer to the requested location of a particular prior allocation (array layout).
 *		Note that you can obtain a pointer to one past the end of the array.  Avoid
 *		reading or writing to this location.  This can be useful in certain circumstances.
 */
void *bk_memx_get(bk_s B, struct bk_memx *bm, u_int count, u_int *curused, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  void *ret = NULL;
  void *tmp;

  if (curused && bm) *curused = bm->bm_curused;

  if (!bm || (BK_FLAG_ISSET(flags, BK_MEMX_GETNEW) && (count < 1)) ||
      (BK_FLAG_ISCLEAR(flags, BK_MEMX_GETNEW) && (count > bm->bm_curused)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  if (BK_FLAG_ISSET(flags, BK_MEMX_GETNEW))
  {						/* Want "new" allocation */
    if (bm->bm_curused + count > bm->bm_curalloc)
    {						/* Need to extend array */
      // XXX - add code to allocate in integral bm_incr size chunks?
      u_int incr = MAX(bm->bm_incr, count - (bm->bm_curalloc - bm->bm_curused));

      if (!(tmp = realloc(bm->bm_array, (bm->bm_curalloc + incr) * bm->bm_unitsize)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not extend array: %s\n",strerror(errno));
	// don't destroy our bm -- data still valid
	BK_RETURN(B, NULL);
      }
      bm->bm_array = tmp;
    }

    ret = ((char *)bm->bm_array) + bm->bm_curused * bm->bm_unitsize;
    bm->bm_curused += count;

    if (curused) *curused = bm->bm_curused;
  }
  else
  {						/* Want existing record */
    ret = ((char *)bm->bm_array) + count * bm->bm_unitsize;
  }

  BK_RETURN(B,ret);
}



/**
 * Reset memory extender used count (truncate).
 *
 * THREADS: MT-SAFE (as long as bm is thread-private)
 *
 *	@param B BAKA Thread/global state
 *	@param bm Buffer management handle
 *	@param count The number of elements to virtually set as the length--truncation only
 *	@param flags Fun for the future
 *	@return <i>-1</i> on call failure
 *	@return <br><i>0</i> on success (including "already smaller than this size")
 */
int bk_memx_trunc(bk_s B, struct bk_memx *bm, u_int count, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bm)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, -1);
  }

  if (count < bm->bm_curused)
    bm->bm_curused = count;

  BK_RETURN(B, 0);
}




/**
 * Lop off the front of a memx and slide the contents down. This is expensive.
 *
 * THREADS: MT-SAFE (as long as bm is thread-private)
 *
 *	@param B BAKA thread/global state.
 *	@param bm Buffer management handle
 *	@param count The number of elements to lop
 *	@param flags Fun for the future
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_memx_lop(bk_s B, struct bk_memx *bm, u_int count, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bm)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  // if lopping off less than half the current used bytes, overlap will occur
  memmove(bm->bm_array, (char *)bm->bm_array + count * bm->bm_unitsize, (bm->bm_curused - count) * bm->bm_unitsize);
  bm->bm_curused -= count;

  BK_RETURN(B,0);    
}
