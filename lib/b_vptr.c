#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_vptr.c,v 1.1 2003/12/05 00:02:47 zz Exp $";
static const char libbk__copyright[] = "Copyright (c) 2003";
static const char libbk__contact[] = "<projectbaka@baka.org>";
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
 * Trim a @a bk_vptr.
 *
 *	@param B BAKA thread/global state.
 *	@param vptr The @a bk_vptr to lop off.
 *	@param ptr First position to save.
 *	@return <i>-1</i> on failure.<br>
 *	@return the <i>length</i> <em>removed</em> success.
 */
extern int vptr_trimleft(bk_s B, struct bk_vptr *vptr, const void *ptr)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"blue-plugin");
  size_t removed;
  size_t left_over;

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

  // realloc pointer for efficiency.  Iff !leftover, realloc to 1
  if (!realloc(vptr->ptr, left_over ? left_over : 1))
  {
    /*
     * We can still continue since the original block is left
     * untouched in case of realloc failure. But generate a warning
     * anyway since this is unexpected.
     */
    bk_error_printf(B, BK_ERR_WARN, "Failed to realloc pointer -- efficiency may be affected.\n");
  }

  vptr->len = left_over;

  BK_RETURN(B,removed);
}



/**
 * Trim a @a bk_vptr.
 *
 *	@param B BAKA thread/global state.
 *	@param vptr The @a bk_vptr to lop off.
 *	@param n number of bytes to remove
 *	@return <i>-1</i> on failure.<br>
 *	@return the <i>length</i> <em>removed</em> success.
 */
extern int vptr_ntrimleft(bk_s B, struct bk_vptr *vptr, size_t n)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"blue-plugin");

  if (!vptr)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  BK_RETURN(B, vptr_trimleft(B, vptr, (char*)(vptr->ptr) + n));
}
