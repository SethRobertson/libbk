#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_fun.c,v 1.8 2001/08/30 19:57:32 seth Exp $";
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


#define funstack_create(o,k,f)		dll_create((o),(k),(f))
#define funstack_destroy(h)		dll_destroy(h)
#define funstack_insert(h,o)		dll_insert((h),(o))
#define funstack_insert_uniq(h,n,o)	dll_insert_uniq((h),(n),(o))
#define funstack_search(h,k)		dll_search((h),(k))
#define funstack_delete(h,o)		dll_delete((h),(o))
#define funstack_minimum(h)		dll_minimum(h)
#define funstack_maximum(h)		dll_maximum(h)
#define funstack_successor(h,o)		dll_successor((h),(o))
#define funstack_predecessor(h,o)	dll_predecessor((h),(o))
#define funstack_iterate(h,d)		dll_iterate((h),(d))
#define funstack_nextobj(h)		dll_nextobj(h)
#define funstack_error_reason(h,i)	dll_error_reason((h),(i))




/*
 * Initialize the function stack
 */
dict_h bk_fun_init(void)
{
  return(funstack_create(NULL, NULL, DICT_UNORDERED));
}



/*
 * Destroy the function stack
 */
void bk_fun_destroy(dict_h funstack)
{
  struct bk_funinfo *fh = NULL;

  DICT_NUKE_CONTENTS(funstack, funstack, fh, break, free(fh));
  funstack_destroy(funstack);
}



/*
 * Entering a function--record infomation
 */
struct bk_funinfo *bk_fun_entry(bk_s B, const char *func, const char *package, const char *grp)
{
  struct bk_funinfo *fh = NULL;

  if (B && !BK_GENERAL_FLAG_ISFUNON(B))
  {
    /* Function tracing is not required */
    return(NULL);
  }

  if (!(fh = (struct bk_funinfo *)malloc(sizeof *fh)))
  {
    if (B)
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate storage for function header: %s\n",strerror(errno));
    return(NULL);
  }

  fh->bf_funname = func;
  fh->bf_pkgname = package;
  fh->bf_grpname = grp;
  fh->bf_debuglevel = 0;

  bk_fun_reentry_i(B, fh);			/* OK, not re-entry, but code concentration... */

  return(fh);
}



/*
 * The current function has gone away--clean it (and any stale children) up
 */
void bk_fun_exit(bk_s B, struct bk_funinfo *fh)
{
  struct bk_funinfo *cur = NULL;

  for(cur = (struct bk_funinfo *)funstack_minimum(BK_BT_FUNSTACK(B)); cur && cur != fh; cur = (struct bk_funinfo *)funstack_successor(BK_BT_FUNSTACK(B), cur)) ;

  if (cur)
  {
    for(;cur;cur=fh)
    {
      if (fh = (struct bk_funinfo *)funstack_predecessor(BK_BT_FUNSTACK(B), cur))
	bk_error_printf(B, BK_ERR_NOTICE,"Implicit exit of %s\n",fh->bf_funname);
      funstack_delete(BK_BT_FUNSTACK(B), cur);
      free(cur);
    }
    BK_BT_CURFUN(B) = (struct bk_funinfo *)funstack_minimum(BK_BT_FUNSTACK(B));
  }
  else
  {
    bk_error_printf(B, BK_ERR_ERR,"Could not find function to exit: %s\n",fh->bf_funname);
  }
}



/*
 * This is an function for main to virtually re-enter BK_ENTRY after B
 * is initialized (or perform initial entry from bk_fun_entry).
 */
void bk_fun_reentry_i(bk_s B, struct bk_funinfo *fh)
{
  if (B && fh)
  {
    if (BK_GENERAL_FLAG_ISDEBUGON(B))
      fh->bf_debuglevel = bk_debug_query(B, BK_GENERAL_DEBUG(B), fh->bf_funname, fh->bf_pkgname, fh->bf_grpname, 0);
    else
      fh->bf_debuglevel = 0;

    funstack_insert(BK_BT_FUNSTACK(B), fh);
    BK_BT_CURFUN(B) = fh;
  }
}



/*
 * Dump the function stack, showing where we all are
 */
void bk_fun_trace(bk_s B, FILE *out, int sysloglevel, bk_flags flags)
{
  struct bk_funinfo *cur = NULL;

  for(cur = (struct bk_funinfo *)funstack_maximum(BK_BT_FUNSTACK(B)); cur; cur = (struct bk_funinfo *)funstack_predecessor(BK_BT_FUNSTACK(B), cur))
  {
    if (out)
      fprintf(out,"Stack trace: %s %s %s\n",cur->bf_funname, cur->bf_pkgname, cur->bf_grpname);
    if (sysloglevel > BK_ERR_NONE)
      bk_general_syslog(B, sysloglevel, BK_SYSLOG_FLAG_NOFUN|BK_SYSLOG_FLAG_NOLEVEL, "Stack trace: %s %s %s\n",cur->bf_funname, cur->bf_pkgname, cur->bf_grpname);
  }
}



/*
 * Turn function tracing off and back on
 */
void bk_fun_set(bk_s B, int state, bk_flags flags)
{
  if (!B)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    return;
  }

  if (state == BK_FUN_OFF)
    BK_FLAG_CLEAR(BK_GENERAL_FLAGS(B),BK_BGFLAGS_FUNON);
  else if (state == BK_FUN_ON)
    BK_FLAG_SET(BK_GENERAL_FLAGS(B),BK_BGFLAGS_FUNON);
  else
    bk_error_printf(B, BK_ERR_ERR, "Invalid state argument: %d\n",state);
}



/*
 * Reset the debug levels on all currently entered functions
 * (Presumably debug levels have changed)
 */
int bk_fun_reset_debug(bk_s B, bk_flags flags)
{
  struct bk_funinfo *cur = NULL;

  if (!B)
    return(-1);

  if (!BK_GENERAL_FLAG_ISFUNON(B))
    return(0);					/* No function tracing */

  for(cur = (struct bk_funinfo *)funstack_minimum(BK_BT_FUNSTACK(B)); cur; cur = (struct bk_funinfo *)funstack_successor(BK_BT_FUNSTACK(B), cur))
  {
    if (BK_GENERAL_FLAG_ISDEBUGON(B))
      cur->bf_debuglevel = bk_debug_query(B, BK_GENERAL_DEBUG(B), cur->bf_funname, cur->bf_pkgname, cur->bf_grpname, 0);
    else
      cur->bf_debuglevel = 0;
  }

  return(0);
}



/*
 * Discover the function name of my nth ancestor in the function stack
 */
const char *bk_fun_funname(bk_s B, int ancestordepth, bk_flags flags)
{
  struct bk_funinfo *cur = NULL;

  if (!BK_GENERAL_FLAG_ISFUNON(B))
    return(NULL);				/* No function tracing, no function name */

  for(cur = (struct bk_funinfo *)funstack_minimum(BK_BT_FUNSTACK(B)); cur && ancestordepth--; cur = (struct bk_funinfo *)funstack_successor(BK_BT_FUNSTACK(B), cur)) ;

  if (!cur)
    return(NULL);				/* Don't have that many ancestors! */

  return(cur->bf_funname);
}
