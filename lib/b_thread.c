#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_thread.c,v 1.9 2003/05/15 03:15:23 seth Exp $";
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

#include <libbk.h>
#include "libbk_internal.h"


/**
 * @file
 * Thread convenience routines
 */


/**
 * @name Defines: btl_clc
 * Key-value database CLC definitions
 * to hide CLC choice.
 */
// @{
#define btl_create(o,k,f,a)	bst_create((o),(k),(f))
#define btl_destroy(h)		bst_destroy(h)
#define btl_insert(h,o)		bst_insert((h),(o))
#define btl_insert_uniq(h,n,o)	bst_insert_uniq((h),(n),(o))
#define btl_append(h,o)		bst_append((h),(o))
#define btl_append_uniq(h,n,o)	bst_append_uniq((h),(n),(o))
#define btl_search(h,k)		bst_search((h),(k))
#define btl_delete(h,o)		bst_delete((h),(o))
#define btl_minimum(h)		bst_minimum(h)
#define btl_maximum(h)		bst_maximum(h)
#define btl_successor(h,o)	bst_successor((h),(o))
#define btl_predecessor(h,o)	bst_predecessor((h),(o))
#define btl_iterate(h,d)	bst_iterate((h),(d))
#define btl_nextobj(h,i)	bst_nextobj((h),(i))
#define btl_iterate_done(h,i)	bst_iterate_done((h),(i))
#define btl_error_reason(h,i)	bst_error_reason((h),(i))
static int btl_oo_cmp(struct bk_threadnode *a, struct bk_threadnode *b);
static int btl_ko_cmp(const char *a, struct bk_threadnode *b);
static ht_val btl_obj_hash(struct bk_threadnode *b);
static ht_val btl_key_hash(const char *a);
static const struct ht_args btl_args = { 512, 1, (ht_func)btl_obj_hash, (ht_func)btl_key_hash };
// @}





/**
 * Thread management list
 */
struct bk_threadlist
{
  dict_h		btl_list;		///< List of known threads
  pthread_mutex_t	btl_lock;		///< Lock on thread list
  bk_flags		btl_flags;		///< Fun for the future
};



/**
 * Thread management node
 */
struct bk_threadnode
{
  const char	       *btn_threadname;		///< Name/purpose of thread
  pthread_t		btn_thid;		///< Thread identifier
  bk_s			btn_B;			///< Baka environment for thread
  bk_flags		btn_flags;		///< Fun for the future
};



/**
 * Thread parent-child communication structure
 */
struct bk_threadcomm
{
  void		       *(*btc_start)(bk_s B, void *opaque); ///< How to start child
  void		       *btc_opaque;		///< Temporary communication (parent->child)
  bk_s			btc_B;			///< Baka environment for thread
};



#ifdef BK_USING_PTHREADS
static void *bk_thread_continue(void *opaque);




/**
 * Thread safe counting initialization
 *
 * THREADS: MT-SAFE
 *
 * @param B Baka thread/global environment
 * @param bac Atomic counter structure
 * @param start Starting value
 * @param flags Fun for the future
 * @return <i>zero</i> on success
 * @return <br><i>-1</i> on call failure, lock failure
 */
int bk_atomic_add_init(bk_s B, struct bk_atomic_cntr *bac, int start, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret;

  if (!bac)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, -1);
  }

  if ((ret = pthread_mutex_init(&bac->bac_lock, NULL)) != 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not init mutex (%d): %s\n", ret, strerror(errno));
    BK_RETURN(B, -1);
  }

  bac->bac_cntr = start;

  BK_RETURN(B, 0);
}



/**
 * Thread safe counting/addition/subtraction
 *
 * THREADS: MT-SAFE
 *
 * @param B Baka thread/global environment
 * @param bac Atomic counter structure
 * @param delta Number which we add to counter (negative to subtract, zero to probe)
 * @param result Optional copy-out of result of operation
 * @param flags Fun for the future
 * @return <i>zero</i> on success
 * @return <br><i>-1</i> on call failure, lock failure
 */
int bk_atomic_addition(bk_s B, struct bk_atomic_cntr *bac, int delta, int *result, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret, myresult;

  if (!bac)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, -1);
  }

  if ((ret = pthread_mutex_lock(&bac->bac_lock)) != 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not lock mutex (%d): %s\n", ret, strerror(errno));
    BK_RETURN(B, -1);
  }

  myresult = bac->bac_cntr += delta;

  if ((ret = pthread_mutex_unlock(&bac->bac_lock)) != 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not unlock mutex (%d): %s\n", ret, strerror(errno));
    BK_RETURN(B, -1);
  }

  if (result)
    *result = myresult;

  BK_RETURN(B, 0);
}



/**
 * Blocking-non-blocking method to acquire pthread mutex lock.
 *
 * DEPRECATED FUNCTION--DO NOT USE
 *
 * @param B BAKA Thread/global state
 * @return <i>0</i> on success
 * @return <i>negative</i> on error
 */
int bk_pthread_mutex_lock(bk_s B, struct bk_run *run, pthread_mutex_t *mutex, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!run || !mutex)
  {
    bk_error_printf(B, BK_ERR_ERR, "Internal error: invalid arguments\n");
    BK_RETURN(B, -1);
  }

  while (pthread_mutex_trylock(mutex) != 0)
  {
    if (errno == EBUSY)
    {
      bk_run_once(B, run, BK_RUN_ONCE_FLAG_DONT_BLOCK);
    }
    else
    {
      bk_error_printf(B, BK_ERR_ERR, "Mutex lock acquisition failed: %s.\n", strerror(errno));
      goto error;
    }
  }

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}
#endif /* BK_USING_PTHREADS */



/**
 * Create thread tracking state
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state
 *	@param flags fun for the future
 *	@return <i>NULL</i> on call failure, allocation failure, etc
 *	@return <br><i>allocated thread tracker</i> on success.
 */
struct bk_threadlist *bk_threadlist_create(bk_s B, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_threadlist *tlist = NULL;

  if (!BK_MALLOC(tlist))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate list container: %s\n", strerror(errno));
    BK_RETURN(B, NULL);
  }

  tlist->btl_flags = 0;

  if (!(tlist->btl_list = btl_create((dict_function)btl_oo_cmp, (dict_function)btl_ko_cmp, DICT_THREADED_SAFE, &btl_args)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create CLC: %s\n", btl_error_reason(tlist->btl_list, NULL));
    goto error;
  }

  BK_RETURN(B, tlist);

 error:
  if (tlist)
    bk_threadlist_destroy(B, tlist, 0);
  BK_RETURN(B, NULL);
}



/**
 * Destroy the thread tracking state
 *
 * THREADS: MT-SAFE
 *
 * @param B BAKA thread/global state
 * @param tlist Thread list
 */
void bk_threadlist_destroy(bk_s B, struct bk_threadlist *tlist, bk_flags flags)
{
  struct bk_threadnode *tnode;

  if (!tlist)
  {
    return;
  }

  if (tlist->btl_list)
  {
    DICT_NUKE(tlist->btl_list, btl, tnode, break, bk_threadnode_destroy(B, tnode, flags));
  }

  free(tlist);
  return;
}



/**
 * Create a thread tracking node
 *
 * THREADS: MT-SAFE
 *
 * @param B BAKA Thread/global state
 * @param threadname Name of thread
 * @param flags Fun for the future
 * @return <i>NULL</i> on call failure, allocation failure
 * @return <br><i>allocated node</i> on success
 */
struct bk_threadnode *bk_threadnode_create(bk_s B, const char *threadname, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_threadnode *tnode = NULL;

  if (!threadname)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!BK_CALLOC(tnode))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate node container: %s\n", strerror(errno));
    BK_RETURN(B, NULL);
  }

  if (!(tnode->btn_threadname = (const char *)strdup(threadname)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not duplicate thread name: %s\n", strerror(errno));
    goto error;
  }

  BK_RETURN(B, tnode);

 error:
  if (tnode)
    bk_threadnode_destroy(B, tnode, 0);
  BK_RETURN(B, NULL);
}



/**
 * Destroy a thread tracking node
 *
 * THREADS: MT-SAFE
 *
 * @param B BAKA Thread/global state
 * @param tnode Thread node
 * @param flags Fun for the future
 */
void bk_threadnode_destroy(bk_s B, struct bk_threadnode *tnode, bk_flags flags)
{
  if (!tnode)
  {
    return;
  }

  // Don't nuke self
  if (B == tnode->btn_B)
    return;

  if (tnode->btn_threadname)
    free((char *)tnode->btn_threadname);

  if (tnode->btn_B)
    bk_general_thread_destroy(tnode->btn_B);

  free(tnode);
  return;
}



#ifdef BK_USING_PTHREADS
/**
 * Spawn a new thread (internal, you probably want bk_general_thread_create)
 *
 * Set up new B, create thread, add to tracking list.
 *
 * In ``child'' disable cancellation, disable signals, install
 * tracking list cleanup handler, make detached, start user processing
 *
 * THREADS: MT-SAFE
 *
 * @param B BAKA Thread/global state
 * @param tlist Tracking thread list
 * @param threadname Name of thread for tracking purposes
 * @param start Function to call to start user processing
 * @param opaque Opaque data for function
 * @param flags Fun for the future
 * @return <i>NULL</i> on error
 * @return <br><i>thread id</i> on success
 */
pthread_t *bk_thread_create(bk_s B, struct bk_threadlist *tlist, const char *threadname, void *(*start)(bk_s B, void *opaque), void *opaque, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_threadnode *tnode = NULL;
  struct bk_threadcomm *tcomm = NULL;
  int ret;

  if (!start || !tlist || !threadname)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!BK_CALLOC(tcomm))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create tracking pass node: %s\n", strerror(errno));
    goto error;
  }
  tcomm->btc_start = start;
  tcomm->btc_opaque = opaque;

  if (!(tnode = bk_threadnode_create(B, threadname, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create tracking thread node\n");
    goto error;
  }

  if (!(tcomm->btc_B = tnode->btn_B = bk_general_thread_init(B, (char *)threadname)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not thread BAKA\n");
    goto error;
  }

  ret = btl_insert(tlist->btl_list, tnode);

  if (ret != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert tracking node into list: %s\n", btl_error_reason(tlist->btl_list, NULL));
    goto error;
  }

  if (pthread_create(&tnode->btn_thid, NULL, bk_thread_continue, tcomm) != 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create thread: %s\n", strerror(errno));
    goto error;
  }

  BK_RETURN(B, &tnode->btn_thid);

 error:
  if (tcomm)
    free(tcomm);

  if (tnode)
    bk_thread_tnode_done(B, tlist, tnode, 0);

  BK_RETURN(B, NULL);
}



/**
 * First pthread child -- pull stuff apart and re-exec user's first function
 *
 * Also, disable cancellation, disable signals, install
 * tracking list cleanup handler, make detached, start user processing
 *
 * THREADS: MT-SAFE
 *
 * @param opaque Data from pthread which is hopefully my tcomm
 * @return stuff from user function which is then ignored
 */
static void *bk_thread_continue(void *opaque)
{
  struct bk_threadcomm *tcomm = opaque;
  bk_s B;
  void *subopaque;
  void *(*start)(bk_s, void *);
  pthread_t myth;
  sigset_t mask;

  if (!tcomm)
    return(NULL);

  myth = pthread_self();

  // Detach so no-one needs to wait
  pthread_detach(myth);

  // Block all signals
  sigfillset(&mask);
  pthread_sigmask(SIG_BLOCK, &mask, NULL);

  // Block cancellation until asked
  pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

  B = tcomm->btc_B;
  subopaque = tcomm->btc_opaque;
  start = tcomm->btc_start;

  free(opaque);
  return (*start)(B, subopaque);
}



/**
 * Thread node finished
 *
 * THREADS: MT-SAFE
 *
 * @param B Baka thread/global state
 * @param tlist Thread tracking list
 * @param tnode Thread node
 * @param flags Fun for the future
 */
void bk_thread_tnode_done(bk_s B, struct bk_threadlist *tlist, struct bk_threadnode *tnode, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!tlist || !tnode)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  if (pthread_mutex_lock(&tlist->btl_lock))
    abort();

  if (btl_delete(tlist, tnode) != DICT_OK)
    bk_error_printf(B, BK_ERR_WARN, "Could not find tnode %p in list %p\n", tnode, tlist);

  if (pthread_mutex_unlock(&tlist->btl_lock))
    abort();

  bk_threadnode_destroy(B, tnode, 0);
  BK_VRETURN(B);
}
#endif /* BK_USING_PTHREADS */



/*
 *			W A R N I N G ! ! ! !
 *
 * Additional functions may be need, to search for threads by name, to
 * search for idle threads, to request thread cancellation, etc
 *
 * It is not clear exactly what functionality will be useful, so they
 * are left as an excercise to the reader.
 *
 */



static int btl_oo_cmp(struct bk_threadnode *a, struct bk_threadnode *b)
{
  int ret = strcmp(a->btn_threadname, b->btn_threadname);
  if (ret) return(ret);
  return (a-b);
}
static int btl_ko_cmp(const char *a, struct bk_threadnode *b)
{
  return strcmp(a, b->btn_threadname);
}
static ht_val btl_obj_hash(struct bk_threadnode *a)
{
  return bk_strhash(a->btn_threadname, BK_HASH_NOMODULUS);
}
static ht_val btl_key_hash(const char *a)
{
  return bk_strhash(a, BK_HASH_NOMODULUS);
}
