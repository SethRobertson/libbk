#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_nvmap.c,v 1.9 2003/01/24 18:20:24 lindauer Exp $";
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
 * Function for conveniently dealing mapping string names to fixed values.
 */

#include <libbk.h>
#include "libbk_internal.h"


/**
 * Convert the name to a value.
 *
 *	@param B BAKA thread/global state.
 *	@param name The name to convert
 *	@return <i>-1</i> on failure.<br>
 *	@return value on success.
 */
int
bk_nvmap_name2value(bk_s B, struct bk_name_value_map *nvmap, const char *name)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_name_value_map *n;

  if (!nvmap || !name)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  for(n = nvmap; n->bnvm_name; n++)
  {
    if (BK_STREQ(n->bnvm_name, name))
    {
      BK_RETURN(B,n->bnvm_val);      
    }
  }
  BK_RETURN(B,-1);  
}




/**
 * Convert a value to a name.
 *
 *	@param B BAKA thread/global state.
 *	@param val The value to match.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>const string</i> on success.
 */
const char *
bk_nvmap_value2name(bk_s B, struct bk_name_value_map *nvmap, int val)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_name_value_map *n;

  if (!nvmap)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  for(n = nvmap; n->bnvm_name; n++)
  {
    if (n->bnvm_val == val)
    {
      BK_RETURN(B,n->bnvm_name);      
    }
  }
  BK_RETURN(B,NULL);  
}
