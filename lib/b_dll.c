#if !defined(lint) && !defined(__INSIGHT__)
#include "libbk_compiler.h"
UNUSED static const char libbk__copyright[] = "Copyright (c) 2003";
UNUSED static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright LIBBK++
 *
 * Copyright (c) 2001-2003 The Authors.  All rights reserved.
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
 *
 * This file implements the "light weight" doubly linked lists that are mostly
 * implmented as inlines in libbk_inline.h Some of the functions are a bit
 * long for inlining and cause problems with the now functional inline warnings
 * of gcc 3.3.x
 */

/**
 * Insert a dll element onto the list. In keeping with CLC insert
 * prepends. Again flags is supporessed to keep this in line with the CLC
 * APi.
 *
 *	@param handle The handle of dll.
 *	@param obj The object to insert.
 *	@return !<i>DICT_ERR</i> on failure.<br>
 *	@return <i>DICT_OK</i> on success.
 */
int
bk_dll_insert_internal(struct bk_generic_dll_handle *gdh, struct bk_generic_dll_element *gde, struct bk_generic_dll_element **old_gdep, int flags, int append)
{
  struct bk_generic_dll_element *g;
  int ret = DICT_OK;
  int compare;

  if (!flags)
  {
    // General case where we want an unordered list and duplicates are fine.
    if (append)
      BK_DLL_INSERT_AFTER(gdh, gde, gdh->gdh_tail);
    else
      BK_DLL_INSERT_BEFORE(gdh, gde, gdh->gdh_head);
  }
  else
  {
    // This sections covers *both* ordering and unique keys, but at least one must be present.
    int ordered = flags & DICT_ORDERED;
    int unique_keys = flags & DICT_UNIQUE_KEYS;

    if (old_gdep)
      *old_gdep = NULL;

    for (g = gdh->gdh_head; g; g = g->gde_next)
    {
      compare = (*gdh->gdh_oo_cmp)(g, gde);

      if (compare == 0)
      {
	if (unique_keys)
	{
	  if (old_gdep)
	    *old_gdep = g;
	  gdh->gdh_errno = DICT_EEXISTS;
	  ret = DICT_ERR;
	}
	else
	{
	  if (ordered)
	  {
	    BK_DLL_INSERT_BEFORE(gdh, gde, g);
	    break;
	  }
	}
      }
      else if (compare > 0)
      {
	if (ordered)
	{
	  BK_DLL_INSERT_BEFORE(gdh, gde, g);
	  break;
	}
      }
    }

    if (ret == DICT_OK)
    {
      if (ordered && ret == DICT_OK)
      {
	BK_DLL_INSERT_AFTER(gdh, gde, gdh->gdh_tail);
      }
      else
      {
	if (append)
	  BK_DLL_INSERT_AFTER(gdh, gde, gdh->gdh_tail);
	else
	  BK_DLL_INSERT_BEFORE(gdh, gde, gdh->gdh_head);
      }
    }
  }

  return(ret);
}
