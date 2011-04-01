#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2001-2011";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2001-2011 The Authors. All rights reserved.
 *
 * This source code is licensed to you under the terms of the file
 * LICENSE.TXT in this release for further details.
 *
 * Send e-mail to <projectbaka@baka.org> for further information.
 *
 * - -Copyright BAKA- -
 */

/**
 * @file
 * These functions provide a Function tracing/stack, and provide function name
 * information for debugging/error message, and provide per-function debug levels.
 * See BK_ENTRY, BK_*RETURN, and bk_fun* macros.
 */

#include <libbk.h>
#include "libbk_internal.h"



/**
 * @name Defines: funstack_clc
 * Stack of functions currently called CLC definitions
 * to hide CLC choice.
 */
// @{
#define funstack_create(o,k,f)		dll_create((o),(k),(f))
#define funstack_destroy(h)		dll_destroy(h)
#define funstack_insert(h,o)		dll_insert((h),(o))
#define funstack_insert_uniq(h,n,o)	dll_insert_uniq((h),(n),(o))
#define funstack_append(h,o)		dll_append((h),(o))
#define funstack_append_uniq(h,n,o)	dll_append_uniq((h),(n),(o))
#define funstack_search(h,k)		dll_search((h),(k))
#define funstack_delete(h,o)		dll_delete((h),(o))
#define funstack_minimum(h)		dll_minimum(h)
#define funstack_maximum(h)		dll_maximum(h)
#define funstack_successor(h,o)		dll_successor((h),(o))
#define funstack_predecessor(h,o)	dll_predecessor((h),(o))
#define funstack_iterate(h,d)		dll_iterate((h),(d))
#define funstack_nextobj(h,i)		dll_nextobj(h,i)
#define funstack_iterate_done(h,i)	dll_iterate_done(h,i)
#define funstack_error_reason(h,i)	dll_error_reason((h),(i))
// @}




/**
 * Initialize the function stack
 *
 * THREADS: MT-SAFE
 *
 *	@return <i>NULL</i> on allocation (or other CLC) failure
 *	@return <br><i>Function stack</i> on success
 */
dict_h bk_fun_init(void)
{
  return(funstack_create(NULL, NULL, DICT_UNORDERED|DICT_THREAD_NOCOALESCE));
}



/**
 * Destroy the function stack
 *
 * THREADS: MT-SAFE
 *
 *	@param funstack The stack of functions currently called
 */
void bk_fun_destroy(dict_h funstack)
{
  struct bk_funinfo *fh = NULL;

  DICT_NUKE_CONTENTS(funstack, funstack, fh, break, free(fh));
  funstack_destroy(funstack);
}



/**
 * Entering a function--record infomation
 *
 * THREADS: MT-SAFE (assumes B is thread private)
 *
 *	@param B BAKA Thread/global state
 *	@param func The name of the function we are in
 *	@param package The name of the package we are in (typically filename)
 *	@param grp The name of the group we are in (typically library)
 *	@return <i>NULL</i> if function tracing is not enabled, or on allocation failure
 *	@return <br><i>encoded function info</i> on success
 */
struct bk_funinfo *bk_fun_entry(bk_s B, const char *func, const char *package, const char *grp)
{
  struct bk_funinfo *fh = NULL;

  if (B && !BK_GENERAL_FLAG_ISFUNON(B))
  {
    /* Function tracing is not required */
    return(NULL);
  }

  if (!BK_MALLOC(fh))
  {
    if (B)
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate storage for function header: %s\n",strerror(errno));
    return(NULL);
  }

  fh->bf_funname = func;
  fh->bf_pkgname = package;
  fh->bf_grpname = grp;
  fh->bf_debuglevel = 0;

  if (BK_BT_ISFUNSTATSON(B))
  {
    gettimeofday(&fh->bf_starttime, NULL);
  }
  else
  {
    fh->bf_starttime.tv_sec = 0;		// Stupid, yes, but funstats could be turned on at any moment...
  }

  bk_fun_reentry_i(B, fh);			/* OK, not re-entry, but code concentration... */

  return(fh);
}



/**
 * The current function has gone away--clean it (and any stale children) up
 *
 * THREADS: MT-SAFE (assumes B is thread private)
 *
 *	@param B BAKA Thread/global state
 *	@param fh Encoded function information produced by a @a bk_fun_entry which has exited
 */
void bk_fun_exit(bk_s B, struct bk_funinfo *fh)
{
  struct bk_funinfo *cur = NULL;
  int save_errno = errno;

  if (!fh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    goto done;
  }

  if (!B)
  {
    free(fh);
    goto done;
  }

  if (fh->bf_starttime.tv_sec && BK_BT_ISFUNSTATSON(B))
  {
    struct timeval end, sum;
    u_quad_t thisus = 0;
    gettimeofday(&end, NULL);
    BK_TV_SUB(&sum, &end, &fh->bf_starttime);
    thisus = BK_SECSTOUSEC((u_quad_t)sum.tv_sec) + sum.tv_usec;
    bk_stat_add(B, BK_BT_FUNSTATS(B), "Function Tracing", fh->bf_funname, thisus, BK_STATS_NO_LOCKS_NEEDED);
  }


  for(cur = (struct bk_funinfo *)funstack_minimum(BK_BT_FUNSTACK(B)); cur && cur != fh; cur = (struct bk_funinfo *)funstack_successor(BK_BT_FUNSTACK(B), cur))
    ; // Intentionally void

  if (cur)
  {
    for(;cur;cur=fh)
    {
      if (fh = (struct bk_funinfo *)funstack_predecessor(BK_BT_FUNSTACK(B), cur))
	bk_error_printf(B, BK_ERR_NOTICE,"Implicit exit of %s during %s exit\n",fh->bf_funname,cur->bf_funname);
      funstack_delete(BK_BT_FUNSTACK(B), cur);
      free(cur);
    }
    BK_BT_CURFUN(B) = (struct bk_funinfo *)funstack_minimum(BK_BT_FUNSTACK(B));
  }
  else
  {
    bk_error_printf(B, BK_ERR_ERR,"Could not find function to exit: %s\n",fh->bf_funname);
    /* free(fh);  ---  Whether to leak memory or to double free... */
  }

 done:
  errno = save_errno;
}



/**
 * This is an function for main to virtually re-enter BK_ENTRY after B
 * is initialized.  This is required since @a bk_fun_init() cannot be
 * called before @a bk_fun_entry is called.
 *
 * THREADS: MT-SAFE (assumes B is thread private)
 *
 *	@param B BAKA Thread/global state information
 *	@param fh Function trace produced by a previous @a bk_fun_entry
 */
void bk_fun_reentry_i(bk_s B, struct bk_funinfo *fh)
{
  if (B && fh)
  {
    if (BK_GENERAL_FLAG_ISDEBUGON(B))
      fh->bf_debuglevel = bk_debug_query(B, BK_GENERAL_DEBUG(B), fh->bf_funname, fh->bf_pkgname, fh->bf_grpname, 0);
    else
      fh->bf_debuglevel = 0;

    if (funstack_insert(BK_BT_FUNSTACK(B), fh) != DICT_OK)
      bk_error_printf(B, BK_ERR_WARN, "Could not insert function stack frame: %s\n",funstack_error_reason(BK_BT_FUNSTACK(B), NULL));

    BK_BT_CURFUN(B) = fh;
  }
}



/**
 * Dump the function stack, showing where we all are
 *
 * THREADS: THREAD-REENTRANT (assumes B is thread private)
 *
 *	@param B BAKA Thread/global state
 *	@param out File handle to output data on (NULL to disable)
 *	@param sysloglevel System log level to dump function stack (BK_ERR_NONE to disable)
 *	@param flags Fun for the future
 */
void bk_fun_trace(bk_s B, FILE *out, int sysloglevel, bk_flags flags)
{
  struct bk_funinfo *cur = NULL;

  if (!B || !BK_BT_FUNSTACK(B))
    return;

  for(cur = (struct bk_funinfo *)funstack_maximum(BK_BT_FUNSTACK(B)); cur; cur = (struct bk_funinfo *)funstack_predecessor(BK_BT_FUNSTACK(B), cur))
  {
    if (out)
      fprintf(out,"Stack trace: %s %s %s\n",cur->bf_funname, cur->bf_pkgname, cur->bf_grpname);
    if (sysloglevel > BK_ERR_NONE)
      bk_general_syslog(B, sysloglevel, BK_SYSLOG_FLAG_NOFUN|BK_SYSLOG_FLAG_NOLEVEL, "Stack trace: %s %s %s\n",cur->bf_funname, cur->bf_pkgname, cur->bf_grpname);
  }
}



/**
 * Turn function tracing off and back on
 *
 * THREADS: THREAD-REENTRANT (assumes B is thread private)
 *
 *	@param B BAKA Thread/global state
 *	@param state BK_FUN_ON to enable tracing, BK_FUN_OFF to disable
 *	@param flags Fun for the future
 */
void bk_fun_set(bk_s B, int state, bk_flags flags)
{
  if (!B)
  {
    return;
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&BK_GENERAL_WRMUTEX(B)) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (state == BK_FUN_OFF)
    BK_FLAG_CLEAR(BK_GENERAL_FLAGS(B),BK_BGFLAGS_FUNON);
  else if (state == BK_FUN_ON)
    BK_FLAG_SET(BK_GENERAL_FLAGS(B),BK_BGFLAGS_FUNON);
  else
    bk_error_printf(B, BK_ERR_ERR, "Invalid state argument: %d\n",state);

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&BK_GENERAL_WRMUTEX(B)) != 0)
    abort();
#endif /* BK_USING_PTHREADS */
}



/**
 * Reset the debug levels on all currently entered functions
 * (Presumably debug levels have changed)
 *
 * THREADS: MT-SAFE (assumes B is thread private)
 *
 *	@param B BAKA Thread/global state
 *	@param flags Fun for the future
 *	@return <i>-1</i> Call failure
 *	@return <br><i>0</i> on success (or if debugging is disabled)
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



/**
 * Discover the function name of my nth ancestor in the function stack
 *
 * THREADS: MT-SAFE (assuming B's function stack is thread-private to this thread)
 *
 *	@param B BAKA Thread/global state
 *	@param ancestordepth The degree of ancestry that you wish to know about
 *	@param flags Fun for the future
 *	@return <i>NULL</i> if function tracing is diabled or if there are not that many ancestors
 *	@return <br><i>Function name string</i> on success.
 */
const char *bk_fun_funname(bk_s B, int ancestordepth, bk_flags flags)
{
  struct bk_funinfo *cur = NULL;

  if (!BK_GENERAL_FLAG_ISFUNON(B))
    return(NULL);				/* No function tracing, no function name */

  for(cur = (struct bk_funinfo *)funstack_minimum(BK_BT_FUNSTACK(B)); cur && ancestordepth--; cur = (struct bk_funinfo *)funstack_successor(BK_BT_FUNSTACK(B), cur))
    ; // Intentionally Void

  if (!cur)
    return(NULL);				/* Don't have that many ancestors! */

  return(cur->bf_funname);
}
