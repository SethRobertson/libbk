#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_memx.c,v 1.1 2001/08/27 03:10:23 seth Exp $";
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

#include <libbk.h>
#include "libbk_internal.h"



/*
 * Extensible buffer management
 */
struct bk_memx *bk_memx_create(bk_s B, size_t objsize, u_int start_hint, u_int incr_hint, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libsos");
  struct bk_memx *ret;

  if (objsize < 1)
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
  ret->bm_unitsize = objsize;
  ret->bm_curused = 0;
  ret->bm_incr = incr_hint;
  ret->bm_flags = flags;

  if (!(ret->bm_array = malloc(objsize * start_hint)))
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



/*
 * Destroy buffer and management
 */
void bk_memx_destroy(bk_s B, struct bk_memx *bm, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libsos");

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



/*
 * Get an element (new or otherwise)
 */
void *bk_memx_get(bk_s B, struct bk_memx *bm, u_int count, u_int *curused, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libsos");
  void *ret = NULL;

  if (curused && bm) *curused = bm->bm_curalloc;

  if (!bm || (BK_FLAG_ISSET(flags, BK_MEMX_GETNEW) && (count < 1)) ||
      (BK_FLAG_ISCLEAR(flags, BK_MEMX_GETNEW) && (count >= bm->bm_curused)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  if (BK_FLAG_ISSET(flags, BK_MEMX_GETNEW))
  {						/* Want "new" allocation */
    if (bm->bm_curused + count > bm->bm_curalloc)
    {						/* Need to extend array */
      u_int incr = MAX(bm->bm_incr,count-(bm->bm_curalloc-bm->bm_curused));

      if (!(bm->bm_array = realloc(bm->bm_array, (bm->bm_curalloc + incr) * bm->bm_unitsize)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not extend array: %s\n",strerror(errno));
	/* XXX - should we destroy our bm? */
	BK_RETURN(B, NULL);
      }
    }

    ret = ((char *)bm->bm_array) + bm->bm_curused * bm->bm_unitsize;
    bm->bm_curused += count;

  if (curused && bm) *curused = bm->bm_curalloc;
  }
  else
  {						/* Want existing record */
    ret = ((char *)bm->bm_array) + count * bm->bm_unitsize;
  }

  BK_RETURN(B,ret);
}



/*
 * Reset memory extender used count (truncate)
 */
int bk_memx_trunc(bk_s B, struct bk_memx *bm, u_int count, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libsos");

  if (!bm)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, -1);
  }

  if (count < bm->bm_curused)
    bm->bm_curused = count;

  BK_RETURN(B, 0);
}
