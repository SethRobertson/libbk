#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_debug.c,v 1.1 2001/07/07 13:41:14 seth Exp $";
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

#define debug_create(o,k,f,a)		ht_create(o,k,f,a)
#define debug_destroy(h)		ht_destroy(h)
#define debug_insert(h,o)		ht_insert(h,o)
#define debug_insert_uniq(h,n,o)	ht_insert(h,n,o)
#define debug_search(h,k)		ht_search(h,k)
#define debug_delete(h,o)		ht_delete(h,o)
#define debug_minimum(h)		ht_minimum(h)
#define debug_maximum(h)		ht_maximum(h)
#define debug_successor(h,o)		ht_successor(h,o)
#define debug_predecessor(h,o)		ht_predecessor(h,o)
#define debug_iterate(h,d)		ht_iterate(h,d)
#define debug_nextobj(h)		ht_nextobj(h)
#define debug_error_reason(h,i)		ht_error_reason(h,i)



/*
 * Initialize the debugging structures (don't actually attempt to start debugging)
 */
struct bk_debug *bk_debug_init(bk_s B, bk_flags flags)
{
  struct bk_debug *ret = NULL;

  if (!(ret = (struct bk_debug *)malloc(sizeof(*ret))))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate debug structure: %s\n",strerror(errno));
    return(NULL);
  }
  memset(ret, 0, sizeof(*ret));

  ret->bd_leveldb = NULL;
  ret->bd_fh = NULL;

  return(ret);
}



/*
 * Get rid of all debugging memory
 */
void bk_debug_destroy(bk_s B, struct bk_debug *bd)
{
  if (!bd)
  {
    bk_error_printf(B, BK_ERR_NOTICE, "Invalid argument\n");
    return;
  }

  if (bd->bd_leveldb)
  {
    struct bk_debugnode *cur;

    while (cur = (struct bk_debugnode *)debug_minimum(bd->bd_leveldb))
    {
      if (debug_delete(bd->bd_leveldb, cur) != DICT_OK)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not delete item from front of CLC: %s\n",debug_error_reason(bd->bd_leveldb,NULL));
	/* XXX - something drastic? */
      }
      free(cur->bd_name);
      free(cur);
    }
    debug_destroy(bd->bd_leveldb);
  }
}



/*
 * 
 */
void bk_debug_reinit(bk_s B, struct bk_debug *bd)
{
  return;
}



/*
 * 
 */
u_int32_t bk_debug_query(bk_s B, struct bk_debug *bdinfo, const char *funname, const char *pkgname, const char *grp, bk_flags flags)
{
  int ret = 0;

  return(ret);
}



/*
 * 
 */
int bk_debug_set(bk_s B, struct bk_debug *bdinfo, const char *name, u_int32_t level)
{
  int ret = 0;

  return(ret);
}



/*
 * 
 */
int bk_debug_setconfig(bk_s B, struct bk_debug *bdinfo, struct bk_config *config, const char *program)
{
  int ret = 0;

  return(ret);
}



/*
 * 
 */
void bk_debug_config(bk_s B, struct bk_debug *bdinfo, FILE *fh, int sysloglevel, bk_flags flags)
{
  return;
}



/*
 * 
 */
void bk_debug_iprint(bk_s B, struct bk_debug *bdinfo, char *buf)
{
  return;
}



/*
 * 
 */
void bk_debug_iprintf(bk_s B, struct bk_debug *bdinfo, char *format, ...)
{
}



/*
 * 
 */
void bk_debug_iprintbuf(bk_s B, struct bk_debug *bdinfo, bk_vptr *buf)
{
}



/*
 * 
 */
void bk_debug_ivprintf(bk_s B, struct bk_debug *bdinfo, char *format, va_list ap)
{
}
