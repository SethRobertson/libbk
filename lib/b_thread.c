#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2002-2010";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2002-2010 The Authors. All rights reserved.
 *
 * This source code is licensed to you under the terms of the file
 * LICENSE.TXT in this release for further details.
 *
 * Send e-mail to <projectbaka@baka.org> for further information.
 *
 * - -Copyright BAKA- -
 */

#include <libbk.h>
#include "libbk_internal.h"

#ifdef HAVE_PTHREAD_NP_H
#include <pthread_np.h>				// for pthread_set_name_np
#endif



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
static int btl_ko_cmp(struct __bk_thread *b, struct bk_threadnode *a);
#ifdef ACTUALLY_USED
static ht_val btl_obj_hash(struct bk_threadnode *b);
static ht_val btl_key_hash(struct __bk_thread *b);
static const struct ht_args btl_args = { 512, 1, (ht_func)btl_obj_hash, (ht_func)btl_key_hash };
#endif // ACTUALLY_USED
// @}





/**
 * Thread management list
 *
 * <TRICKY>The btl_lock mutex is only needed to support bk_thread_kill_others
 * waiting for all other threads to terminate; it need only be acquired when
 * inserting/deleting threads to the list, or when evaluating the condition
 * "are there other threads on the list?" in bk_thread_kill_others.</TRICKY>
 */
struct bk_threadlist
{
  dict_h		btl_list;		///< List of known threads
  pthread_mutex_t	btl_lock;		///< Lock on thread list
  pthread_cond_t	btl_cv;			///< Condvar for kill_others
  bk_flags		btl_flags;		///< Initialization state flags
#define BK_THREADLIST_LOCK_INIT	0x1
#define BK_THREADLIST_CV_INIT	0x2
};



/**
 * Thread management node
 */
struct bk_threadnode
{
  // <TODO>remove btn_threadname, duplicates thread name stored in B</TODO>
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
  struct itimerval	btc_itimer;		///< Itimer for preservation of gprof
  bk_s			btc_B;			///< Baka environment for thread
};



#ifdef BK_USING_PTHREADS
static void *bk_thread_continue(void *opaque);
static void bk_thread_cleanup(void *opaque);
static void bk_thread_unlock(void *opaque);
#endif /* BK_USING_PTHREADS */



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
#ifdef BK_USING_PTHREADS
  int ret;
#endif /* BK_USING_PTHREADS */

  if (!bac)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, -1);
  }

#ifdef BK_USING_PTHREADS
  if ((ret = pthread_mutex_init(&bac->bac_lock, NULL)) != 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not init mutex (%d): %s\n", ret, strerror(errno));
    BK_RETURN(B, -1);
  }
#endif /* BK_USING_PTHREADS */

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
  int myresult;
#ifdef BK_USING_PTHREADS
  int ret;
#endif /* BK_USING_PTHREADS */

  if (!bac)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, -1);
  }

#ifdef BK_USING_PTHREADS
  if ((ret = pthread_mutex_lock(&bac->bac_lock)) != 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not lock mutex (%d): %s\n", ret, strerror(errno));
    BK_RETURN(B, -1);
  }
#endif /* BK_USING_PTHREADS */

  myresult = bac->bac_cntr += delta;

#ifdef BK_USING_PTHREADS
  if ((ret = pthread_mutex_unlock(&bac->bac_lock)) != 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not unlock mutex (%d): %s\n", ret, strerror(errno));
    BK_RETURN(B, -1);
  }
#endif /* BK_USING_PTHREADS */

  if (result)
    *result = myresult;

  BK_RETURN(B, 0);
}



#ifdef BK_USING_PTHREADS
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

#ifdef BK_USING_PTHREADS
  if (pthread_mutex_init(&tlist->btl_lock, NULL))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not init mutex: %s\n", strerror(errno));
    goto error;
  }
  BK_FLAG_SET(tlist->btl_flags, BK_THREADLIST_LOCK_INIT);

  if (pthread_cond_init(&tlist->btl_cv, NULL))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not init cond: %s\n", strerror(errno));
    goto error;
  }
  BK_FLAG_SET(tlist->btl_flags, BK_THREADLIST_CV_INIT);
#endif /* BK_USING_PTHREADS */

  // <TRICKY>DICT_THREADED_SAFE needed; see structure comment</TRICKY>
  if (!(tlist->btl_list = btl_create((dict_function)btl_oo_cmp, (dict_function)btl_ko_cmp, DICT_THREADED_SAFE|DICT_UNIQUE_KEYS, &btl_args)))
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

#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISSET(tlist->btl_flags, BK_THREADLIST_CV_INIT))
    pthread_cond_destroy(&tlist->btl_cv);
  if (BK_FLAG_ISSET(tlist->btl_flags, BK_THREADLIST_LOCK_INIT))
    pthread_mutex_destroy(&tlist->btl_lock);
#endif /* BK_USING_PTHREADS */

  free(tlist);
  return;
}



/**
 * Create a thread tracking node
 *
 * <TODO>replace threadname argument with new_B argument, since threadname
 * already stored in (new) B</TODO>
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
    bk_threadnode_destroy(B, tnode, BK_THREADNODE_DESTROY_DESTROYSELF);
  BK_RETURN(B, NULL);
}



/**
 * Destroy a thread tracking node
 *
 * THREADS: MT-SAFE
 *
 * @param B BAKA Thread/global state
 * @param tnode Thread node
 * @param flags BK_THREADNODE_DESTROY_DESTROYSELF
 */
void bk_threadnode_destroy(bk_s B, struct bk_threadnode *tnode, bk_flags flags)
{
  if (!tnode)
  {
    return;
  }

  // Don't nuke self
  if (BK_FLAG_ISCLEAR(flags, BK_THREADNODE_DESTROY_DESTROYSELF) && B == tnode->btn_B)
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
  BK_ENTRY_VOLATILE(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_threadnode * volatile tnode = NULL;
  struct bk_threadcomm * volatile tcomm = NULL;
  pthread_attr_t attr;
  int ret;

  if (!start || !tlist || !threadname)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  // set up thread attributes with PTHREAD_CREATE_DETACHED default
  if ((ret = pthread_attr_init(&attr)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not init thread attributes: %s\n",
		    strerror(ret));
    BK_RETURN(B, NULL);				// *not* goto error
  }
  if ((ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set detached attribute: %s\n",
		    strerror(ret));
    goto error;
  }

  if (!BK_CALLOC(tcomm))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create tracking pass node: %s\n",
		    strerror(errno));
    goto error;
  }
  tcomm->btc_start = start;
  tcomm->btc_opaque = opaque;
  getitimer(ITIMER_PROF, &tcomm->btc_itimer);
  if (!(tcomm->btc_B = bk_general_thread_init(B, threadname)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not thread BAKA\n");
    goto error;
  }

  if (!(tnode = bk_threadnode_create(B, threadname, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create tracking thread node\n");
    goto error;
  }
  tnode->btn_B = tcomm->btc_B;

  /*
   * <TRICKY>We hold this lock until after pthread_create has completed tnode
   * (by setting tnode->btn_thid).
   *
   * To avoid a race condition where we create a new thread after this thread
   * has been cancelled by bk_thread_kill_others, check for cancellation once
   * tlist->btl_lock is held; the cleanup handler makes sure that this thread
   * will release the lock if cancelled.
   *
   * Note that we do not make any attempt to clean up other resources on
   * cancel; the assumption is that an exec or exit is coming very soon and it
   * isn't worth the trouble - we only need to prevent deadlock.</TRICKY>
   */
  if (pthread_mutex_lock(&tlist->btl_lock))
    abort();

  pthread_cleanup_push(bk_thread_unlock, &tlist->btl_lock);
  pthread_testcancel();
  pthread_cleanup_pop(0);			// don't unlock yet

  ret = btl_insert(tlist->btl_list, tnode);

  if (ret != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert tracking node into list: %s\n", btl_error_reason(tlist->btl_list, NULL));
    goto error;
  }

  if (pthread_create(&tnode->btn_thid, &attr, bk_thread_continue, tcomm) != 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create thread: %s\n", strerror(errno));
    goto error;
  }

  if (pthread_mutex_unlock(&tlist->btl_lock))
    abort();

  BK_RETURN(B, &tnode->btn_thid);

 error:
  if (tnode)
  {
    if (pthread_mutex_unlock(&tlist->btl_lock))
      abort();
    bk_thread_tnode_done(B, tlist, tnode, 0);
  }
  else if (tcomm && tcomm->btc_B)
    bk_general_thread_destroy(tcomm->btc_B);

  if (tcomm)
    free(tcomm);

  pthread_attr_destroy(&attr);

  BK_RETURN(B, NULL);
}



/**
 * First pthread child -- pull stuff apart and re-exec user's first function
 *
 * Also, disable cancellation, disable signals, install tracking list cleanup
 * handler, make detached, start user processing
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
  sigset_t mask;

  if (!tcomm)
  {
    return(NULL);

    // <TRICKY>Work around gcc bug</TRICKY>
    subopaque = &subopaque;
  }

  // Block all signals except SIGCONT (to allow interrupting select calls)
  sigfillset(&mask);
  sigdelset(&mask, SIGCONT);
  pthread_sigmask(SIG_BLOCK, &mask, NULL);

  // Defer cancel until cancel point (per POSIX, this is default - be paranoid)
  pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

  B = tcomm->btc_B;
  subopaque = tcomm->btc_opaque;
  start = tcomm->btc_start;
  setitimer(ITIMER_PROF, &tcomm->btc_itimer, NULL);

  free(opaque);

#ifdef HAVE_PTHREAD_SET_NAME_NP
  // what the hell, might as well...
  pthread_set_name_np(pthread_self(), (char *)BK_BT_THREADNAME(B));
#endif

  pthread_cleanup_push(bk_thread_cleanup, B);
  subopaque = (*start)(B, subopaque);
  pthread_cleanup_pop(1);

  return subopaque;
}



/**
 * Tracking list cleanup handler.
 *
 * THREADS: MT-SAFE
 *
 * @param opaque Data from pthread which is hopefully my B
 */
static void bk_thread_cleanup(void *opaque)
{
  bk_s B = opaque;
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_threadlist *tlist;
  struct bk_threadnode *tnode;

  if (!(tlist = BK_GENERAL_TLIST(B)))
  {
    bk_error_printf(B, BK_ERR_WARN, "Cannot find thread list for thread %s\n",
		    BK_BT_THREADNAME(B));
    BK_VRETURN(B);
  }

  if (!(tnode = btl_search(tlist->btl_list, B)))
  {
    bk_error_printf(B, BK_ERR_WARN, "Cannot find thread node for thread %s\n",
		    BK_BT_THREADNAME(B));
  }
  else
  {
     bk_thread_tnode_done(B, tlist, tnode, 0);
  }


  return;
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
  if (!tlist || !tnode)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    return;
  }

  if (pthread_mutex_lock(&tlist->btl_lock))
    abort();

  if (btl_delete(tlist->btl_list, tnode) != DICT_OK)
    bk_error_printf(B, BK_ERR_WARN, "Could not delete %s from thread list\n",
		    BK_BT_THREADNAME(tnode->btn_B));

  // as good as dead; tell bk_thread_kill_others caller to keep going
  pthread_cond_signal(&tlist->btl_cv);

  if (pthread_mutex_unlock(&tlist->btl_lock))
    abort();

  bk_threadnode_destroy(B, tnode, BK_THREADNODE_DESTROY_DESTROYSELF);
}



/**
 * Kill all other threads.
 *
 * THREADS: MT-SAFE
 *
 * This should be called prior to any call to exit/exec, and is abused by
 * bk_general_destroy to prevent concurrent access to bk_general by terminating
 * rival threads with extreme prejudice.
 *
 * @param B BAKA Thread/global state
 * @param flags Flags for the future (avoid pthread_kill_other_threads_np()?)
 */
void bk_thread_kill_others(bk_s B, bk_flags flags)
{
  BK_ENTRY_VOLATILE(B, __FUNCTION__, __FILE__, "libbk");

#ifdef HAVE_PTHREAD_KILL_OTHER_THREADS_NP
  /*
   * <WARNING bugid="1281">
   * Using pthread_kill_other_threads_np is only necessary on Linux (or any
   * platform that doesn't comply with the POSIX requirement that exec/exit
   * terminate all threads - fortunately, Linux is unique in that respect).
   *
   * Even on Linux, this should really be called *after* iterating through the
   * thread list, not *instead of*, since it prevents thread cancellation
   * handlers cancellation etc. etc. but I just want to compile on *BSD without
   * giving anyone a "false sense of security."  So we'll let the users on *BSD
   * find out if this code works, unless a Linux user hangs first because of a
   * mutex held by another thread killed by pthread_kill_other_threads_np.
   * </WARNING>
   */
  pthread_kill_other_threads_np();
#else
  struct bk_threadlist *tlist;
  struct bk_threadnode *tnode;
  dict_iter iter;

  if ((tlist = BK_GENERAL_TLIST(B)))
  {
    /*
     * <TRICKY>If multiple threads call this at the same time, we need to
     * make sure that only one of them cancels the other threads; all the
     * others should be cancelled by the one that won the race.
     *
     * We do this by checking for cancellation once tlist->btl_lock is held;
     * the cleanup handler makes sure that this thread will release the lock if
     * it lost the race and was cancelled.</TRICKY>
     */

    if (pthread_mutex_lock(&tlist->btl_lock))
      abort();

    pthread_cleanup_push(bk_thread_unlock, &tlist->btl_lock);
    pthread_testcancel();
    pthread_cleanup_pop(0);			// don't unlock yet

    iter = btl_iterate(tlist->btl_list, DICT_FROM_START);
    while ((tnode = btl_nextobj(tlist->btl_list, iter)))
    {
      if (!pthread_equal(tnode->btn_thid, pthread_self()))
	pthread_cancel(tnode->btn_thid);
    }
    btl_iterate_done(tlist->btl_list, iter);

    /*
     * We have cancelled all existing baka threads; now we have to wait for
     * them to reach cancellation points and take themselves off the thread
     * list.  (No new threads should be created, since bk_thread_create has a
     * cancellation point that will prevent it from creating a new thread if
     * the existing one is cancelled).
     */
    do
    {
      tnode = btl_minimum(tlist->btl_list);

      // is this thread the only one on the list?
      if (!tnode || (pthread_equal(tnode->btn_thid, pthread_self()) &&
		     !(btl_successor(tlist->btl_list, tnode))))
	break;

      // not yet - wait and see
      if (pthread_cond_wait(&tlist->btl_cv, &tlist->btl_lock))
	abort();

    } while (1);

    if (pthread_mutex_unlock(&tlist->btl_lock))
      abort();
  }
#endif

  BK_VRETURN(B);
}



/**
 * Race condition handler for bk_thread_kill_others.
 *
 * Unlock mutex if this bk_thread_kill_others caller loses the race to another.
 *
 * This function assumes that tlist->btl_lock is already held, and will release
 * that lock before exiting; this is done to simplify error handling in
 * bk_thread_create().
 *
 * THREADS: MT-SAFE (as long as tlist->btl_lock is held when called)
 *
 * @param opaque Data from pthread which is hopefully tlist->btl_lock
 */
static void bk_thread_unlock(void *opaque)
{
  if (pthread_mutex_unlock(opaque))
    abort();
}



/**
 * Monitor a (dereferenced) pointer in a very busy loop to see when it
 * changes, then print a message.
 *
 * @param B Baka thread/global enviornment
 * @param opaque Information about what we are monitoring
 */
void *bk_monitor_memory_thread(bk_s B, void *opaque)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  bk_vptr *monitor = opaque;
  bk_vptr copy;

  if (!monitor)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  copy.len = monitor->len;
  BK_MALLOC_LEN(copy.ptr, copy.len);
 again:
  memcpy(copy.ptr, monitor->ptr, copy.len);
  bk_error_printf(B, BK_ERR_NOTICE, "Monitoring pointer %p/%d\n", monitor->ptr, monitor->len);

  while (1)
  {
    if (memcmp(copy.ptr, monitor->ptr, copy.len))
    {
      bk_error_printf(B, BK_ERR_NOTICE, "Monitored pointer %p/%d has changed value!\n", monitor->ptr, monitor->len);
      goto again;
    }
    // Think about a usleep here....
  }

  // notreached
  BK_RETURN(B, NULL);
}



/**
 * Monitor a (dereferenced) pointer in a very busy loop to see when it
 * changes, then print a message.
 *
 * @param B Baka thread/global enviornment
 * @param opaque Information about what we are monitoring
 */
void *bk_monitor_int_thread(bk_s B, void *opaque)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  volatile u_int *monitor = opaque;
  u_int save;

  if (!monitor)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

 again:
  save = *monitor;
  bk_error_printf(B, BK_ERR_NOTICE, "Monitoring pointer %p/%d\n", monitor, *monitor);

  while (1)
  {
    if (*monitor != save)
    {
      bk_error_printf(B, BK_ERR_NOTICE, "Monitored pointer %p/%d has changed value!\n", monitor, *monitor);
      goto again;
    }
    // Think about a usleep here....
  }

  // notreached
  BK_RETURN(B, NULL);
}

#endif /* BK_USING_PTHREADS */



/*
 *			W A R N I N G ! ! ! !
 *
 * Additional functions may be need, to search for threads by name, to
 * search for idle threads, to request thread cancellation, etc
 *
 * It is not clear exactly what functionality will be useful, so they
 * are left as an exercise to the reader.
 *
 */



static int btl_oo_cmp(struct bk_threadnode *a, struct bk_threadnode *b)
{
  int ret = a->btn_B - b->btn_B;
  if (ret) return(ret);
  // DICT_UNIQUE_KEYS should prevent this from ever executing, but just in case
  return (a - b);
}
// (note a and b reversed from usual, because 'B' cannot be 'a', only 'b' :-)
static int btl_ko_cmp(struct __bk_thread *b, struct bk_threadnode *a)
{
  return b - a->btn_B;
}
#ifdef ACTUALLY_USED
static ht_val btl_obj_hash(struct bk_threadnode *a)
{
  return (ht_val) a->btn_B;
}
static ht_val btl_key_hash(struct __bk_thread *b)
{
  return (ht_val) b;
}
#endif // ACTUALLY_USED
