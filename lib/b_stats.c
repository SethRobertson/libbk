#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_stats.c,v 1.7 2003/06/13 02:27:59 seth Exp $";
static const char libbk__copyright[] = "Copyright (c) 2001";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright LIBBK++
 *
 * Copyright (c) 2002 The Authors.  All rights reserved.
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
 * All of the support routines for dealing with performance statistic tracking
 */

#include <libbk.h>
#include "libbk_internal.h"



/**
 * We cannot use bk_fun since bk_fun uses us.  Nasty recursion if we do...
 */
// @{
#undef BK_ENTRY
#undef BK_RETURN
#undef BK_VRETURN
#define BK_ENTRY(B, fun, pkg, grp) struct bk_funinfo *__bk_funinfo = NULL
#define BK_RETURN(B, ret) do { return(ret); } while (__bk_funinfo)
#define BK_VRETURN(B) do { return; } while (__bk_funinfo)
// @}


#define MAXPERFINFO	8192			///< Maximum size for a performance information line



/**
 * Performance tracking list
 */
struct bk_stat_list
{
  dict_h	bsl_list;			///< List of performance tracks
  bk_flags	bsl_flags;			///< Flags for the future
};



/**
 * Performance tracking node
 */
struct bk_stat_node
{
  const char	       *bsn_name1;		///< Primary name of tracking node
  const char	       *bsn_name2;		///< Subsidiary name of tracking node
  u_quad_t		bsn_minutime;		///< Minimum number of microseconds we have seen for this item
  u_quad_t		bsn_maxutime;		///< Maximum number of microseconds we have seen for this item
  u_quad_t		bsn_sumutime;		///< Sum of microseconds we have seen for this itme
  struct timeval	bsn_start;		///< Start time for current tracking
  u_int			bsn_count;		///< Number of times we have tracked
  bk_flags		bsn_flags;		///< Flags for the future
#ifdef BK_USING_PTHREADS
  pthread_mutex_t	bsn_lock;		///< Per-node locking
#endif /* BK_USING_PTHREADS */
};



/**
 * @name Defines: bsl_clc
 * Performance tracking database CLC definitions
 * to hide CLC choice.
 */
// @{
#define bsl_create(o,k,f,a)	ht_create((o),(k),(f),(a))
#define bsl_destroy(h)		ht_destroy(h)
#define bsl_insert(h,o)		ht_insert((h),(o))
#define bsl_insert_uniq(h,n,o)	ht_insert_uniq((h),(n),(o))
#define bsl_append(h,o)		ht_append((h),(o))
#define bsl_append_uniq(h,n,o)	ht_append_uniq((h),(n),(o))
#define bsl_search(h,k)		ht_search((h),(k))
#define bsl_delete(h,o)		ht_delete((h),(o))
#define bsl_minimum(h)		ht_minimum(h)
#define bsl_maximum(h)		ht_maximum(h)
#define bsl_successor(h,o)	ht_successor((h),(o))
#define bsl_predecessor(h,o)	ht_predecessor((h),(o))
#define bsl_iterate(h,d)	ht_iterate((h),(d))
#define bsl_nextobj(h,i)	ht_nextobj((h),(i))
#define bsl_iterate_done(h,i)	ht_iterate_done((h),(i))
#define bsl_error_reason(h,i)	ht_error_reason((h),(i))
static int bsl_oo_cmp(struct bk_stat_node *a, struct bk_stat_node *b);
static int bsl_ko_cmp(struct bk_stat_node *a, struct bk_stat_node *b);
static ht_val bsl_obj_hash(struct bk_stat_node *a);
static ht_val bsl_key_hash(struct bk_stat_node *a);
static struct ht_args bsl_args = { 512, 1, (ht_func)bsl_obj_hash, (ht_func)bsl_key_hash };
// @}



/**
 * Create a performance statistic tracking list
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param flags Fun for the future
 *	@return <i>NULL</i> on failure.<br>
 *	@return <br><i>performance list</i> on success
 */
struct bk_stat_list *bk_stat_create(bk_s B, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_stat_list *blist;

  if (!BK_CALLOC(blist))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate performance list structure: %s\n", strerror(errno));
    BK_RETURN(B, NULL);
  }

  if (!(blist->bsl_list = bsl_create((dict_function)bsl_oo_cmp, (dict_function)bsl_ko_cmp, DICT_UNIQUE_KEYS|bk_thread_safe_if_thread_ready, &bsl_args)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate performance list: %s\n", bsl_error_reason(NULL, NULL));
    goto error;
  }

  BK_RETURN(B, blist);

 error:
  if (blist)
    bk_stat_destroy(B, blist);
  BK_RETURN(B, NULL);
}



/**
 * Destroy a performance statistic tracking list
 *
 * THREADS: MT-SAFE (Assuming different blist)
 * THREADS: REENTRANT (Otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param blist List to destroy
 */
void bk_stat_destroy(bk_s B, struct bk_stat_list *blist)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_stat_node *bnode;

  if (!blist)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  if (blist->bsl_list)
  {
    DICT_NUKE(blist->bsl_list, bsl, bnode, bk_error_printf(B, BK_ERR_ERR, "Could not delete what we just minimized: %s\n", bsl_error_reason(blist->bsl_list, NULL)); break, bk_stat_node_destroy(B, bnode));
  }
  free(blist);

  BK_VRETURN(B);
}



/**
 * Create a performance statistic tracking node, attach to blist
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param blist Performance list
 *	@param name1 Primary name
 *	@param name2 Secondary name
 *	@param flags Fun for the future
 *	@return <i>NULL</i> on failure.<br>
 *	@return <br><i>performance node</i> on success
 */
struct bk_stat_node *bk_stat_nodelist_create(bk_s B, struct bk_stat_list *blist, const char *name1, const char *name2, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_stat_node *bnode;

  if (!blist || !name1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!(bnode = bk_stat_node_create(B, name1, name2, flags)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create node for list\n");
    BK_RETURN(B, NULL);
  }

  if (bsl_insert(blist->bsl_list, bnode) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert autocreated performance node %s/%s, pressing on\n",name1, name2?name2:"");
    bk_stat_node_destroy(B, bnode);
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B, bnode);
}



/**
 * Create a performance statistic tracking node
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param name1 Primary name
 *	@param name2 Secondary name
 *	@param flags Fun for the future
 *	@return <i>NULL</i> on failure.<br>
 *	@return <br><i>performance node</i> on success
 */
struct bk_stat_node *bk_stat_node_create(bk_s B, const char *name1, const char *name2, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_stat_node *bnode;

  if (!name1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!BK_CALLOC(bnode))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate performance node: %s\n", strerror(errno));
    BK_RETURN(B, NULL);
  }

#ifdef BK_USING_PTHREADS
  if (pthread_mutex_init(&bnode->bsn_lock, NULL) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (!(bnode->bsn_name1 = strdup(name1)) || (name2 && !(bnode->bsn_name2 = strdup(name2))))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not duplicate performance node name: %s\n", strerror(errno));
    goto error;
  }
  bnode->bsn_minutime = UINT_MAX;

  BK_RETURN(B, bnode);

 error:
  if (bnode)
    bk_stat_node_destroy(B, bnode);
  BK_RETURN(B, NULL);
}



/**
 * Destroy a performance statistic tracking node
 *
 * THREADS: THREAD-REENTRANT (Otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param bnode Node to destroy
 */
void bk_stat_node_destroy(bk_s B, struct bk_stat_node *bnode)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bnode)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  if (bnode->bsn_name1)
    free((void *)bnode->bsn_name1);

  if (bnode->bsn_name2)
    free((void *)bnode->bsn_name2);

#ifdef BK_USING_PTHREADS
  if (pthread_mutex_unlock(&bnode->bsn_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  free(bnode);

  BK_VRETURN(B);
}



/**
 * Start a performance interval, by name
 *
 * THREADS: MT-SAFE

 * @param B BAKA thread/global environment
 * @param blist Performance list
 * @param name1 Primary name
 * @param name2 Secondary name
 * @param flags Fun for the future
 */
void bk_stat_start(bk_s B, struct bk_stat_list *blist, const char *name1, const char *name2, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_stat_node *bnode;
  struct bk_stat_node searchnode;

  if (!blist || !name1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  searchnode.bsn_name1 = name1;
  searchnode.bsn_name2 = name2;

  if (!(bnode = bsl_search(blist->bsl_list, &searchnode)))
  {
    // New node, start tracking
    if (!(bnode = bk_stat_nodelist_create(B, blist, name1, name2, 0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not autocreate performance node %s/%s, pressing on\n",name1, name2?name2:"");
      BK_VRETURN(B);
    }
  }
  bk_stat_node_start(B, bnode, 0);

  BK_VRETURN(B);
}



/**
 * End a performance interval, by name
 *
 * THREADS: MT-SAFE
 *
 * @param B BAKA thread/global environment
 * @param blist Performance list
 * @param name1 Primary name
 * @param name2 Secondary name
 * @param flags Fun for the future
 */
void bk_stat_end(bk_s B, struct bk_stat_list *blist, const char *name1, const char *name2, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_stat_node *bnode;
  struct bk_stat_node searchnode;

  if (!blist || !name1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  searchnode.bsn_name1 = name1;
  searchnode.bsn_name2 = name2;

  if (!(bnode = bsl_search(blist->bsl_list, &searchnode)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Tried to end an performance interval %s/%s which did not appear in list\n", name1, name2?name2:name2);
    BK_VRETURN(B);
  }
  bk_stat_node_end(B, bnode, 0);

  BK_VRETURN(B);
}



/**
 * Start a performance interval
 *
 * THREADS: MT-SAFE (assuming different bnode)
 * THREADS: THREAD-REENTRANT (Otherwise)
 *
 * @param B BAKA thread/global environment
 * @param bnode Node to start interval
 * @param flags Fun for the future
 */
void bk_stat_node_start(bk_s B, struct bk_stat_node *bnode, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bnode)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }


  if (bnode->bsn_start.tv_sec != 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Performance interval %s/%s has already been started\n", bnode->bsn_name1,bnode->bsn_name2?bnode->bsn_name2:"");
    BK_VRETURN(B);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&bnode->bsn_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  gettimeofday(&bnode->bsn_start, NULL);

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&bnode->bsn_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_VRETURN(B);
}



/**
 * End a performance interval
 *
 * THREADS: MT-SAFE (assuming different bnode)
 * THREADS: THREAD-REENTRANT (Otherwise)
 *
 * @param B BAKA thread/global environment
 * @param bnode Node to end interval
 * @param flags Fun for the future
 */
void bk_stat_node_end(bk_s B, struct bk_stat_node *bnode, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct timeval end, sum;
  u_quad_t thisus = 0;

  if (!bnode)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  if (bnode->bsn_start.tv_sec == 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Performance interval %s/%s was ended but not started (perhaps double-started?)\n", bnode->bsn_name1,bnode->bsn_name2?bnode->bsn_name2:"");
    BK_VRETURN(B);
  }

  gettimeofday(&end, NULL);

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&bnode->bsn_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_TV_SUB(&sum, &end, &bnode->bsn_start);

#ifdef THISUS_IS_INT
  if (sum.tv_sec >= 4294)
  {
    bk_error_printf(B, BK_ERR_WARN, "Performance tracking only good for intervals <= 2^32/10^6 seconds\n");
    thisus = UINT_MAX;
  }
  else
#endif /*THISUS_IS_INT*/
    thisus = BK_SECSTOUSEC(sum.tv_sec) + sum.tv_usec;

  if (thisus < bnode->bsn_minutime)
    bnode->bsn_minutime = thisus;

  if (thisus > bnode->bsn_maxutime)
    bnode->bsn_maxutime = thisus;

  bnode->bsn_sumutime += thisus;
  bnode->bsn_count++;

  bnode->bsn_start.tv_sec = 0;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&bnode->bsn_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_VRETURN(B);
}



/**
 * Return a string containing all performance information (which you must free)
 *
 * THREADS: MT-SAFE
 *
 * @param B BAKA thread/global environment
 * @param blist Performance list
 * @param flags BK_STAT_DUMP_HTML
 * @param <i>NULL</i> on call failure, allocation failure
 * @param <br><i>string you must free</i> on success
 */
char *bk_stat_dump(bk_s B, struct bk_stat_list *blist, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_stat_node *bnode;
  char perfbuf[MAXPERFINFO];
  bk_vstr ostring;
  char *funstatfilessave = BK_GENERAL_FUNSTATFILE(B);

  if (!blist)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  /*
   * Prevent recursion and locking problems.  However disables
   * (unlocked, thank you very much) *global* function tracing.  If we
   * implement per-thread stats, this will have to change.  Really only
   * suitable for program cleanup.
   */
  BK_GENERAL_FUNSTATFILE(B) = NULL;

  ostring.ptr = malloc(MAXPERFINFO);
  ostring.max = MAXPERFINFO;
  ostring.cur = 0;

  if (BK_FLAG_ISSET(flags, BK_STAT_DUMP_HTML))
  {
    if (bk_vstr_cat(B, 0, &ostring, "<table summary=\"Performance Information\"><caption><em>Program Performance Statistics</em></caption><tr><th>Primary Name</th><th>Secondary Name</th><th>Minimum time (usec)</th><th>Average time (usec)</th><th>Maximum time (usec)</th><th>Count</th><th>Total time (sec)</th></tr>\n") < 0)
      goto error;
  }

  for (bnode = bsl_minimum(blist->bsl_list); bnode; bnode = bsl_successor(blist->bsl_list, bnode))
  {
#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&bnode->bsn_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */

    if (BK_FLAG_ISSET(flags, BK_STAT_DUMP_HTML))
    {
      snprintf(perfbuf, sizeof(perfbuf), "<tr><td>%s</td><td>%s</td><td align=\"right\">%llu</td><td align=\"right\">%.3f</td><td align=\"right\">%llu</td><td align=\"right\">%u</td><td align=\"right\">%.6lf</td></tr>\n",bnode->bsn_name1, bnode->bsn_name2, bnode->bsn_minutime, bnode->bsn_count?(float)bnode->bsn_sumutime/bnode->bsn_count:0.0, bnode->bsn_maxutime, bnode->bsn_count,(double)bnode->bsn_sumutime/1000000.0);
    }
    else
    {
      snprintf(perfbuf, sizeof(perfbuf), "\"%s\",\"%s\",\"%llu\",\"%.3f\",\"%llu\",\"%u\",\"%.6lf\"\n",bnode->bsn_name1, bnode->bsn_name2, bnode->bsn_minutime, bnode->bsn_count?(float)bnode->bsn_sumutime/bnode->bsn_count:0.0, bnode->bsn_maxutime, bnode->bsn_count,(double)bnode->bsn_sumutime/1000000.0);
    }

#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&bnode->bsn_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */

    if (bk_vstr_cat(B, 0, &ostring, perfbuf) < 0)
      goto error;
  }

  if (BK_FLAG_ISSET(flags, BK_STAT_DUMP_HTML))
  {
    if (bk_vstr_cat(B, 0, &ostring, "</table>\n") < 0)
      goto error;
  }

  if (0)
  {
  error:
    bk_error_printf(B, BK_ERR_ERR, "Allocation failure: %s\n", strerror(errno));
    if (ostring.ptr)
      free(ostring.ptr);
    ostring.ptr = NULL;
  }

  BK_GENERAL_FUNSTATFILE(B) = funstatfilessave;

  BK_RETURN(B, ostring.ptr);
}



/**
 * Return a performance interval
 *
 * THREADS: MT-SAFE
 *
 * @param B BAKA thread/global environment
 * @param blist Performance list
 * @param name1 Primary name
 * @param name2 Secondary name
 * @param minusec Copy-out for node information
 * @param maxusec Copy-out for node information
 * @param sumusec Copy-out for node information
 * @param count Copy-out for node information
 * @param flags Fun for the future
 */
void bk_stat_info(bk_s B, struct bk_stat_list *blist, const char *name1, const char *name2, u_quad_t *minusec, u_quad_t *maxusec, u_quad_t *sumutime, u_int *count, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_stat_node *bnode;
  struct bk_stat_node searchnode;

  if (!blist || !name1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  searchnode.bsn_name1 = name1;
  searchnode.bsn_name2 = name2;

  if (!(bnode = bsl_search(blist->bsl_list, &searchnode)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Tried to dump a performance interval %s/%s which did not appear in list\n", name1, name2?name2:name2);
    BK_VRETURN(B);
  }

  bk_stat_node_info(B, bnode, minusec, maxusec, sumutime, count, 0);

  BK_VRETURN(B);
}



/**
 * Return a performance interval
 *
 * THREADS: MT-SAFE (assuming different bnode)
 * THREADS: THREAD-REENTRANT (Otherwise)
 *
 * @param B BAKA thread/global environment
 * @param bnode Node to return information for
 * @param minusec Copy-out for node information
 * @param maxusec Copy-out for node information
 * @param sumusec Copy-out for node information
 * @param count Copy-out for node information
 * @param flags Fun for the future
 */
void bk_stat_node_info(bk_s B, struct bk_stat_node *bnode, u_quad_t *minusec, u_quad_t *maxusec, u_quad_t *sumutime, u_int *count, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bnode)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&bnode->bsn_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (minusec)
    *minusec = bnode->bsn_minutime;

  if (maxusec)
    *maxusec = bnode->bsn_maxutime;

  if (sumutime)
    *sumutime = bnode->bsn_sumutime;

  if (count)
    *count = bnode->bsn_count;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&bnode->bsn_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_VRETURN(B);
}



/**
 * Add units to a performance interval, by name
 *
 * THREADS: MT-SAFE
 *
 * @param B BAKA thread/global environment
 * @param blist Performance list
 * @param name1 Primary name
 * @param name2 Secondary name
 * @param usec Units (usec usually) to add
 * @param flags Fun for the future
 */
void bk_stat_add(bk_s B, struct bk_stat_list *blist, const char *name1, const char *name2, u_quad_t usec, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_stat_node *bnode;
  struct bk_stat_node searchnode;

  if (!blist || !name1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  searchnode.bsn_name1 = name1;
  searchnode.bsn_name2 = name2;

  if (!(bnode = bsl_search(blist->bsl_list, &searchnode)))
  {
    // New node, start tracking
    if (!(bnode = bk_stat_nodelist_create(B, blist, name1, name2, 0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not autocreate performance node %s/%s, pressing on\n",name1, name2?name2:"");
      BK_VRETURN(B);
    }
  }
  bk_stat_node_add(B, bnode, usec, 0);

  BK_VRETURN(B);
}



/**
 * Add to a performance interval
 *
 * THREADS: MT-SAFE (assuming different bnode)
 * THREADS: THREAD-REENTRANT (Otherwise)
 *
 * @param B BAKA thread/global environment
 * @param bnode Node to end interval
 * @param usec Units (usec usually) to add
 * @param flags Fun for the future
 */
void bk_stat_node_add(bk_s B, struct bk_stat_node *bnode, u_quad_t usec, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bnode)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&bnode->bsn_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (usec < bnode->bsn_minutime)
    bnode->bsn_minutime = usec;

  if (usec > bnode->bsn_maxutime)
    bnode->bsn_maxutime = usec;

  bnode->bsn_sumutime += usec;
  bnode->bsn_count++;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&bnode->bsn_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_VRETURN(B);
}



/*
 * THREADS: MT-SAFE
 */
static int bsl_oo_cmp(struct bk_stat_node *a, struct bk_stat_node *b)
{
  int ret;

  if (ret = strcmp(a->bsn_name1, b->bsn_name1))
    return(ret);
  if (a->bsn_name2 && b->bsn_name2)
    return(strcmp(a->bsn_name2, b->bsn_name2));
  else
    return(a->bsn_name2 - b->bsn_name2);
}
static int bsl_ko_cmp(struct bk_stat_node *a, struct bk_stat_node *b)
{
  int ret;

  if (ret = strcmp(a->bsn_name1, b->bsn_name1))
    return(ret);
  if (a->bsn_name2 && b->bsn_name2)
    return(strcmp(a->bsn_name2, b->bsn_name2));
  else
    return(a->bsn_name2 - b->bsn_name2);
}
static ht_val bsl_obj_hash(struct bk_stat_node *a)
{
  u_int ret = bk_strhash(a->bsn_name1, 0);

  if (a->bsn_name2)
    ret += bk_strhash(a->bsn_name2, 0);
  return(ret);
}
static ht_val bsl_key_hash(struct bk_stat_node *a)
{
  u_int ret = bk_strhash(a->bsn_name1, 0);

  if (a->bsn_name2)
    ret += bk_strhash(a->bsn_name2, 0);
  return(ret);
}
