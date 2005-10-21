#if !defined(lint) && !defined(__INSIGHT__)
#include "libbk_compiler.h"
UNUSED static const char libbk__rcsid[] = "$Id: b_vptr.c,v 1.7 2005/10/21 23:33:50 lindauer Exp $";
UNUSED static const char libbk__copyright[] = "Copyright (c) 2003";
UNUSED static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
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

#include <libbk.h>
#include "libbk_internal.h"


/**
 * @file
 * Some vptr utilities.
 */



/**
 * Append one bk_vptr to a bk_vptr.
 *
 *	@param B BAKA thread/global state.
 *	@param dp The destination @a bk_vptr.
 *	@param sp the source @a bk_vptr.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
extern int bk_vptr_append(bk_s B, bk_vptr *dp, const bk_vptr *sp)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  void *tmp;

  if (!dp || !sp)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!(tmp = realloc(dp->ptr, dp->len + sp->len)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not realloc alloc_ptr\n");
    BK_RETURN(B,-1);
  }

  dp->ptr = tmp;
  memcpy((char *)dp->ptr + dp->len, sp->ptr, sp->len);
  dp->len += sp->len;

  BK_RETURN(B,0);
}



/**
 * Trim a @a bk_vptr.
 *
 *	@param B BAKA thread/global state.
 *	@param vptr The @a bk_vptr to lop off.
 *	@param ptr First position to save.
 *	@param flags BK_VPTR_FLAG_NO_RESIZE - Don't realloc memory
 *	@return <i>-1</i> on failure.<br>
 *	@return the <i>length</i> <em>removed</em> success.
 */
int bk_vptr_trimleft(bk_s B, bk_vptr *vptr, const void *ptr, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  size_t removed;
  size_t left_over;
  void *new;

  if (!vptr || !ptr || ptr < vptr->ptr)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  removed = (char *)ptr - (char *)vptr->ptr;
  if (removed > vptr->len)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  left_over = vptr->len - removed;

  if (left_over)
    memmove(vptr->ptr, ptr, left_over);

  if (BK_FLAG_ISCLEAR(flags, BK_VPTR_FLAG_NO_RESIZE))
  {
    // realloc pointer for efficiency.  Iff !leftover, realloc to 1
    if ((new = realloc(vptr->ptr, left_over ? left_over : 1)))
      vptr->ptr = new;				// probably the same anyhow
    else
    {
      /*
       * We can still continue since the original block is left untouched in case
       * of realloc failure. But generate a warning since this is unexpected.
       * Note that most realloc implementations waste the space in any case.
       */
      bk_error_printf(B, BK_ERR_WARN, "Trimmed vptr wastes %zu bytes.\n",
		      removed);
    }

    vptr->len = left_over;
  }

  BK_RETURN(B,removed);
}



/**
 * Trim a @a bk_vptr.
 *
 *	@param B BAKA thread/global state.
 *	@param vptr The @a bk_vptr to lop off.
 *	@param n number of bytes to remove
 *	@return <i>-1</i> on failure.<br>
 *	@return the <i>length</i> <em>removed</em> on success.
 */
extern int bk_vptr_ntrimleft(bk_s B, bk_vptr *vptr, size_t n)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!vptr)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  BK_RETURN(B, bk_vptr_trimleft(B, vptr, (char*)(vptr->ptr) + n, 0));
}



/**
 * Compare two bk_vptrs.
 *
 *	@param v1 The first @a bk_vptr address
 *	@param v2 The second @a bk_vptr address
 *	@return < 0 if v1 is shorter than v2 or less than v2
 *	@return 0 if contents of v1 == v2
 *	@return > 0 otherwise
 */
int bk_vptr_cmp(bk_vptr *v1, bk_vptr *v2)
{
  int v1short = 0;
  u_int shortlen = 0;
  int r;

  if (v1->len == v2->len)
    return memcmp(v1->ptr, v2->ptr, v1->len);

  if (v1->len < v2->len)
  {
    v1short = 1;
    shortlen = v1->len;
  }
  else
  {
    v1short = 0;
    shortlen = v2->len;
  }

  r = memcmp(v1->ptr, v2->ptr, shortlen);

  if (r == 0)
    return v1short?-1:1;
  else
    return r;
}
