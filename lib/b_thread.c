#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2002-2011";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2002-2011 The Authors. All rights reserved.
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
static int btl_ko_cmp(bk_s b, struct bk_threadnode *a);
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
#define BK_THREADLIST_LOCK_INIT			0x1
#define BK_THREADLIST_CV_INIT			0x2
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
#define BK_THREADNODE_FLAG_MAIN_THREAD		0x1
#define BK_THREADNODE_FLAG_CANCELED		0x2
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
 * Support for "recursive" locks. These are locks that protect a single
 * thread from deadlocking *itself*.
 */
struct bk_recursive_lock
{
#ifdef BK_USING_PTHREADS
  pthread_key_t		brl_ctr;		///< Key for thread-specific lock counter.
  pthread_mutex_t	brl_lock;		///< Actual lock
#endif /* BK_USING_PTHREADS */
  bk_flags		brl_flags;		///< Everone needs flags.
};



static u_int *ctrp_create(bk_s B, struct bk_recursive_lock *brl, bk_flags flags);
static void ctrp_destroy(bk_s B, struct bk_recursive_lock *brl, u_int *ctrp, bk_flags flags);
static const char *generate_main_thread_name(bk_s B, bk_flags flags);



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
  const char *main_thread_name = NULL;

  if (!tlist)
  {
    return;
  }

  if (tlist->btl_list)
  {
    if (!(main_thread_name = generate_main_thread_name(B, 0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not genenerate main thraed name\n");
    }
    else
    {
      for(tnode = btl_minimum(tlist->btl_list);
	  tnode;
	  tnode = btl_successor(tlist->btl_list, tnode))
      {
	if (BK_STREQ(tnode->btn_threadname, main_thread_name))
	{
	  if (btl_delete(tlist->btl_list, tnode) != DICT_OK)
	  {
	    bk_error_printf(B, BK_ERR_ERR, "Could not delete main thread node from list: %s\n", btl_error_reason(tlist->btl_list, NULL));
	  }
	  bk_threadnode_destroy(B, tnode, BK_THREADNODE_DESTROY_DESTROYSELF | BK_THREADNODE_DESTROY_DATA_ONLY);
	  break;
	}
      }
      free((char *)main_thread_name);
    }

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
 * Obtain the lock on the tlist.
 *
 *	@param B BAKA thread/global state.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_threadlist_lock(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_threadlist *btl = BK_GENERAL_TLIST(B);

  if (!btl)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

#ifdef BK_USING_PTHREADS
  if (pthread_mutex_lock(&(btl->btl_lock)) != 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not obtain thread list lock: %s\n", strerror(errno));
    BK_RETURN(B, -1);
  }
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B, 0);
}




/**
 * Obtain the lock on the tlist.
 *
 *	@param B BAKA thread/global state.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_threadlist_unlock(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_threadlist *btl = BK_GENERAL_TLIST(B);

  if (!btl)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

#ifdef BK_USING_PTHREADS
  if (pthread_mutex_unlock(&(btl->btl_lock)) != 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not release thread list lock: %s\n", strerror(errno));
    BK_RETURN(B, -1);
  }
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B, 0);
}




/**
 * Get the first entry in the tlist
 *
 *	@param B BAKA thread/global state.
 *	@return <i>NULL</i> on failure.<br>
 *	@return opaque <i>threadnode</i> on success.
 */
void *
bk_threadlist_minimum(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_threadlist *btl = BK_GENERAL_TLIST(B);

  if (!btl)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B, btl_minimum(btl->btl_list));
}



/**
 * Get the successor of a thread node in the thread list
 *
 *	@param B BAKA thread/global state.
 *	@return <i>NULL</i> on failure.<br>
 *	@return opaque <i>threadnode</i> on success.
 */
void *
bk_threadlist_successor(bk_s B, struct bk_threadnode *btn)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_threadlist *btl = BK_GENERAL_TLIST(B);

  if (!btl || !btn)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  while(btn = btl_successor(btl->btl_list, btn))
  {
    if (BK_FLAG_ISCLEAR(btn->btn_flags, BK_THREADNODE_FLAG_CANCELED))
      break;
  }

  BK_RETURN(B, btn);
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

  if (tnode->btn_B && BK_FLAG_ISCLEAR(flags, BK_THREADNODE_DESTROY_DATA_ONLY))
    bk_general_thread_destroy(tnode->btn_B);

  free(tnode);
  return;
}




/**
 * Get a thread name.
 *
 *	@param B BAKA thread/global state.
 *	@param bt Flags for future use.
 *	@return <i>thread name</i> on success.
 */
const char *
bk_threadnode_name(bk_s B, void *btn)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  BK_RETURN(B, ((struct bk_threadnode *)btn)->btn_threadname);
}



/**
 * Get a thread id
 *
 *	@param B BAKA thread/global state.
 *	@param bt Flags for future use.
 *	@return <i>thread id</i> on success.
 */
pthread_t
bk_threadnode_threadid(bk_s B, void *btn)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  BK_RETURN(B, ((struct bk_threadnode *)btn)->btn_thid);
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
 * Note on the special 'first' thread: When pthread_create() is called for
 * the first time, the original "heavyweight" process is divided into *2*
 * threads. In the original libbk API there was no accounting for the
 * second of these 2 threads -- and, still today, there remains no way to
 * name it. So this code creates accounting threadnode for this unnamable
 * thread and sets the name of the thread to either
 * "argv[0].BK_THREAD_MAIN_THREAD_NAME" or the value set in the
 * configuration file.
 *
 * THREADS: MT-SAFE
 *
 * @param B BAKA Thread/global state
 * @param tlist Tracking thread list
 * @param threadname Name of thread for tracking purposes
 * @param start Function to call to start user processing
 * @param opaque Opaque data for function
 * @param flags BK_THREAD_CREATE_FLAG_JOIN
 * @return <i>NULL</i> on error
 * @return <br><i>thread id</i> on success
 */
pthread_t *bk_thread_create(bk_s B, struct bk_threadlist *tlist, const char *threadname, void *(*start)(bk_s B, void *opaque), void *opaque, bk_flags flags)
{
  BK_ENTRY_VOLATILE(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_threadnode * volatile tnode = NULL;
  struct bk_threadnode *tbtn;
  struct bk_threadcomm * volatile tcomm = NULL;
  pthread_attr_t attr;
  int joinstate = PTHREAD_CREATE_DETACHED;
  int ret;
  int attr_initialized = 0;
  int threadnode_cnt = 0;
  const char *main_thread_name = NULL;
  pthread_t *tidp = NULL;

  /*
   * 'start' is required *except* when BK_THREAD_CREATE_FLAG_MAIN_THREAD is
   * set. In that case 'start' must be NULL (and the converse is true as
   * well).
   */
  if (!tlist || !threadname ||
      (!start && BK_FLAG_ISCLEAR(flags, BK_THREAD_CREATE_FLAG_MAIN_THREAD)) ||
      (start && BK_FLAG_ISSET(flags, BK_THREAD_CREATE_FLAG_MAIN_THREAD)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!(tnode = bk_threadnode_create(B, threadname, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create tracking thread node\n");
    goto error;
  }

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

  if (BK_FLAG_ISCLEAR(flags, BK_THREAD_CREATE_FLAG_MAIN_THREAD))
  {
    /*
     * This tcomm and attr code used to happen before the mutex was locked
     * (and reasonably so). It has been moved so as to make it clearer what
     * work needs to happen when the BK_THREAD_CREATE_FLAG_MAIN_THREAD is
     * not set (as will be the case on the second and all subsequent calls
     * to bk_thread_create()) and when it is (the first call to
     * bk_thread_create()). So the assumption is: that this function is
     * called sufficiently infrequently that spending a few extra moments
     * locking the btl_list is not going to have any measurable impact on
     * performance.
     */
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
    tnode->btn_B = tcomm->btc_B;

    // set up thread attributes with PTHREAD_CREATE_DETACHED default
    if ((ret = pthread_attr_init(&attr)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not init thread attributes: %s\n",
		      strerror(ret));
      goto error;
    }
    attr_initialized = 1;

    if (BK_FLAG_ISSET(flags, BK_THREAD_CREATE_FLAG_JOIN))
    {
      joinstate = PTHREAD_CREATE_JOINABLE;
    }

    if ((ret = pthread_attr_setdetachstate(&attr, joinstate)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not set join/detached attribute: %s\n",
		      strerror(ret));
      goto error;
    }

    // Normal bk_thread_create() call, so create new thread
    if (pthread_create(&tnode->btn_thid, &attr, bk_thread_continue, tcomm) != 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create thread: %s\n", strerror(errno));
      goto error;
    }

    if (pthread_attr_destroy(&attr) != 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not destroy attr: %s\n", strerror(errno));
      goto error;
    }
    attr_initialized = 0;
  }
  else
  {
    /*
     * We are in the midst of defining the special "first" thread, which,
     * conveniently, is the very thread we're in (you may need to stare at
     * the code for a bit to convince yourself of this), so we avoid the
     * call to pthread_create() and populate the new threadnode's threadid
     * with pthread_self().
     */
    BK_FLAG_SET(tnode->btn_flags, BK_THREADNODE_FLAG_MAIN_THREAD);
    tnode->btn_thid = pthread_self();
    tnode->btn_B = B;
  }

  if (btl_insert(tlist->btl_list, tnode) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert tracking node into list: %s\n", btl_error_reason(tlist->btl_list, NULL));
    goto error;
  }

  tidp = &(tnode->btn_thid); // Save the return value before NULLing tnode
  tnode = NULL;

  if (pthread_mutex_unlock(&tlist->btl_lock))
    abort();

  /*
   * If there is only one threadnode in the list than this is the very
   * first call to bk_thread_create() so we also need to create the special
   * threadnode for the "MAIN" thread.
   */
  for(tbtn = btl_minimum(tlist->btl_list); tbtn; tbtn = btl_successor(tlist->btl_list,tbtn))
  {
    threadnode_cnt++;
  }

  if (threadnode_cnt == 1)
  {
    if (!(main_thread_name = generate_main_thread_name(B, 0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not generate main thrad name\n");
      goto error;
    }

    if (!(bk_thread_create(B, tlist, main_thread_name, NULL, NULL, flags | BK_THREAD_CREATE_FLAG_MAIN_THREAD)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create threadnode for the special __MAIN__ thread\n");
      goto error;
    }

    free((char *)main_thread_name);
    main_thread_name = NULL;
  }

  BK_RETURN(B, tidp);

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

  if (attr_initialized)
    pthread_attr_destroy(&attr);

  if (main_thread_name)
    free((char *)main_thread_name);

  BK_RETURN(B, NULL);
}



/**
 * Generate the name of the special "MAIN" thread (aka the mother of all
 * threads). Returns a string which much be freed by the caller.
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static const char *
generate_main_thread_name(bk_s B, bk_flags flags)
{
  BK_ENTRY_VOLATILE(B, __FUNCTION__, __FILE__, "libbk");
  char *conf_name;
  char *main_thread_name = NULL;

  if ((conf_name = BK_GWD(B, "pthread.main_thread.name", NULL)))
  {
    if (!(main_thread_name = strdup(conf_name)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not strdup main thread name from conf: %s\n", strerror(errno));
      goto error;
    }
  }
  else
  {
    if (!(main_thread_name = bk_string_alloc_sprintf(B, 0, 0, "%s.%s", BK_GENERAL_PROGRAM(B), BK_THREAD_MAIN_THREAD_NAME)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create main thread name: %s\n", strerror(errno));
      goto error;
    }
  }

  BK_RETURN(B, main_thread_name);

 error:
  if (main_thread_name)
    free(main_thread_name);
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

  // Turn on cacellability
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

  B = tcomm->btc_B;
  subopaque = tcomm->btc_opaque;
  start = tcomm->btc_start;
  setitimer(ITIMER_PROF, &tcomm->btc_itimer, NULL);

  pthread_getcpuclockid(pthread_self(), &(BK_BT_CPU_CLOCK(B)));

  free(opaque);

#ifdef HAVE_PTHREAD_SET_NAME_NP
  // what the hell, might as well...
  pthread_set_name_np(pthread_self(), (char *)BK_BT_THREADNAME(B));
#endif

  pthread_cleanup_push(bk_thread_cleanup, B);
  subopaque = (*start)(B, subopaque);
  bk_thread_cancel(B, pthread_self(), 0);
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
  //BK_VRETURN(B);
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
  return;
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
#else /* HAVE_PTHREAD_KILL_OTHER_THREADS_NP */
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
      if (BK_FLAG_ISCLEAR(tnode->btn_flags, BK_THREADNODE_FLAG_CANCELED) &&
	  !pthread_equal(tnode->btn_thid, pthread_self()))
      {
	bk_thread_cancel(B, tnode->btn_thid, 0);
      }
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
#endif /* HAVE_PTHREAD_KILL_OTHER_THREADS_NP */

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



/**
 * Cancel a thread. Thread cancellation is tricky because you don't really
 * know when it's going to happen (assuming you use
 * PTHREAD_CANCEL_DEFERRED, which is the default "cancel type"). The
 * problem is that there is empirical evidence that (at least in linux's
 * pthread implementation) a thread which is cancelled *immediately*
 * becomes toxic (ie core will dump) if someone else tries a pthread
 * operation using said thread's ID, *but* it may be some time before the
 * actual cleanup callback runs. Therefore there is a period of time in
 * which a cancelled thread sits in the Baka thread list just waiting to
 * trip up unsuspecting functions (eg:
 * stats_thread_cpu_time_register()). So we mark cancelled threads as such
 * so that other functions have some hope of ignoring thread ID's which
 * will cause core dumps if referenced.
 *
 *	@param B BAKA thread/global state.
 *	@param tid The thread ID to cancel
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_thread_cancel(bk_s B, pthread_t tid, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int err;
  struct bk_threadnode *btn = NULL;
  struct bk_threadlist *btl = BK_GENERAL_TLIST(B);

  if (!btl)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  for (btn = btl_minimum(btl->btl_list);
       btn;
       btn = btl_successor(btl->btl_list, btn))
  {
    if (pthread_equal(btn->btn_thid, tid))
    {
      if (BK_FLAG_ISSET(btn->btn_flags, BK_THREADNODE_FLAG_CANCELED))
      {
	// We have already cancelled this thread. Don't do it again.
	BK_RETURN(B, 0);
      }

      BK_FLAG_SET(btn->btn_flags, BK_THREADNODE_FLAG_CANCELED);
      break;
    }
  }

  if (!pthread_equal(tid, pthread_self()) && ((err = pthread_cancel(tid)) != 0))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not cancel thread: %s\n", strerror(err));
    goto error;
  }

  BK_RETURN(B, 0);

 error:
  if (btn)
    BK_FLAG_CLEAR(btn->btn_flags, BK_THREADNODE_FLAG_CANCELED);

  BK_RETURN(B, -1);
}




/**
 * Destructor function for the counter in the recursive lock feature. This is not a BAKA API.
 *
 */
static void
recursive_lock_ctr_destroy(void *buf)
{
  free(buf); // This function is only called if buf is non-NULL, so no need to check
  return;
}



#define BK_BRL_FLAG_KEY_CREATED		0x1
#define BK_BRL_FLAG_MUTEX_CREATED	0x2
#define BK_BRL_FLAG_CTRP_CREATED	0x4
/**
 * Create a recursive lock.
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
bk_recursive_lock_h
bk_recursive_lock_create(bk_s B, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_recursive_lock *brl = NULL;
  int err;

  if (!(BK_CALLOC(brl)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not calloc brl: %s\n", strerror(errno));
    goto error;
  }

  bk_debug_printf_and(B,256,"Creating recursive lock: %p\n", brl);

  if ((err = pthread_key_create(&(brl->brl_ctr), recursive_lock_ctr_destroy)) != 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create the pthread key for the recursive lock counter: %s\n", strerror(err));
    goto error;
  }

  bk_debug_printf_and(B,256,"Created key: %p\n", brl);

  BK_FLAG_SET(brl->brl_flags, BK_BRL_FLAG_KEY_CREATED);

  if ((err = pthread_mutex_init(&(brl->brl_lock), NULL)) != 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create the mutex for the recursive lock: %s\n", strerror(err));
    goto error;
  }

  bk_debug_printf_and(B,256,"Created the mutex: %p\n", brl);
  BK_FLAG_SET(brl->brl_flags, BK_BRL_FLAG_MUTEX_CREATED);


  bk_debug_printf_and(B,256,"Recursive lock created: %p\n", brl);
  BK_RETURN(B, brl);

 error:
  if (brl)
    bk_recursive_lock_destroy(B, brl);

  BK_RETURN(B, NULL);
}



/**
 * Destroy a recursive lock
 *
 *	@param B BAKA thread/global state.
 *	@param recursive_lock The recursive lock to destroy.
 */
void
bk_recursive_lock_destroy(bk_s B, bk_recursive_lock_h recursive_lock)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_recursive_lock *brl = (struct bk_recursive_lock *)recursive_lock;

  if (!brl)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  bk_debug_printf_and(B,256,"Destroying recursive lock: %p\n", brl);

  if (BK_FLAG_ISSET(brl->brl_flags, BK_BRL_FLAG_MUTEX_CREATED))
  {
    pthread_mutex_destroy(&(brl->brl_lock));
    bk_debug_printf_and(B,256,"Destroyed mutex: %p\n", brl);
  }

  if (BK_FLAG_ISSET(brl->brl_flags, BK_BRL_FLAG_KEY_CREATED))
  {
    pthread_key_delete(brl->brl_ctr);
    bk_debug_printf_and(B,256,"Destroyed key: %p\n", brl);
  }

  free(brl);
  bk_debug_printf_and(B,256,"Destroyed recursive lock\n");

  BK_VRETURN(B);
}



/**
 * Grab a recusive lock. Yes, the function name is annoying, but
 * bk_recursive_lock_lock() would be really noisome, no?
 *
 *	@param B BAKA thread/global state.
 *	@param recursive_lock The recursive lock to grab.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_recursive_lock_grab(bk_s B, bk_recursive_lock_h recursive_lock, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_recursive_lock *brl = (struct bk_recursive_lock *)recursive_lock;
  u_int *ctrp = NULL;
  int err;

  if (!brl)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  bk_debug_printf_and(B,256,"Grabbing recursive lock: %p: %lu\n", brl, (unsigned long)(pthread_self()));

  if ((!(ctrp = pthread_getspecific(brl->brl_ctr))) &&
      (!(ctrp = ctrp_create(B, brl, 0))))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create and initialize recursive lock counter\n");
    goto error;
  }

  if (!*ctrp)
  {
    bk_debug_printf_and(B,256,"Recursive lock not currently held: %p: %lu\n", brl, (unsigned long)(pthread_self()));
    if ((err = pthread_mutex_lock(&(brl->brl_lock))) != 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not obtain (recursive) lock: %s\n", strerror(err));
      goto error;
    }
  }

  bk_debug_printf_and(B,256,"Recursive lock cnt: %u->%u, %p: %lu\n", *ctrp, *ctrp+1, brl, (unsigned long)(pthread_self()));

  (*ctrp)++;

  BK_RETURN(B, 0);

 error:

  BK_RETURN(B, -1);
}

/**
 * Release a recursive lock
 *
 *	@param B BAKA thread/global state.
 *	@param recursive_lock The recursive lock to grab.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_recursive_lock_release(bk_s B, bk_recursive_lock_h recursive_lock, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_recursive_lock *brl = (struct bk_recursive_lock *)recursive_lock;
  u_int *ctrp = NULL;
  int err;

  if (!brl)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  bk_debug_printf_and(B,256,"Releasing recursive lock: %p: %lu\n", brl, (unsigned long)(pthread_self()));

  if (!(ctrp = pthread_getspecific(brl->brl_ctr)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not extract counter from recursive lock we claim to hold\n");
    goto error;
  }

  if (!*ctrp)
  {
    /*
     * The debug message contains useful developer info not relavent to the
     * error message, so we need both.
     */
    bk_debug_printf_and(B,256,"YIKES attempt to unlock unheld lock: %p: %lu\n", brl, (unsigned long)(pthread_self()));
    bk_error_printf(B, BK_ERR_ERR, "Attempt to unlock unheld recursive lock\n");
    goto error;
  }

  bk_debug_printf_and(B,256,"Recursive lock cnt: %u->%u, %p: %lu\n", *ctrp, *ctrp-1, brl, (unsigned long)(pthread_self()));

  (*ctrp)--;

  if (!*ctrp)
  {
    ctrp_destroy(B, brl, ctrp, 0);
    ctrp = NULL;

    if ((err = pthread_mutex_unlock(&(brl->brl_lock))) != 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not unlock mutex: %s\n", strerror(err));
      goto error;
    }
    bk_debug_printf_and(B,256,"Lock is now unheld: %p\n", brl);
  }

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}



/**
 * Create the counter for a recursive lokc.
 *
 *	@param B BAKA thread/global state.
 *	@param brl The recursive lock.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new <i>ctrp</i> on success.
 */
static u_int *
ctrp_create(bk_s B, struct bk_recursive_lock *brl, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int err;
  u_int *ctrp = NULL;

  if (!brl)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!(BK_CALLOC(ctrp)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not calloc ctrp: %s\n", strerror(errno));
    goto error;
  }

  bk_debug_printf_and(B,256,"Created the thread specific counter: %p\n", ctrp);

  if ((err = pthread_setspecific(brl->brl_ctr, ctrp)) != 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set the data for the recusive lock counter: %s\n", strerror(err));
    goto error;
  }

  bk_debug_printf_and(B,256,"Registered the thread specific counter: %p\n", ctrp);

  BK_RETURN(B, ctrp);

 error:
  if (ctrp)
    free(ctrp);
  BK_RETURN(B, NULL);

}




/**
 * Destroy the counter of a recusive lock
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static void
ctrp_destroy(bk_s B, struct bk_recursive_lock *brl, u_int *ctrp, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int err;

  if (!brl || !ctrp)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  free(ctrp);

  if ((err = pthread_setspecific(brl->brl_ctr, NULL)) != 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not NULL the data for the recusive lock counter: %s\n", strerror(err));
  }
  BK_VRETURN(B);
}


#else /* BK_USING_PTHREADS */

/*
 * The following are versions of the recursive lock functions for use when
 * pthreads are not in use. They always "succeed" save for argument
 * checking because arg checking is a generally useful thing during
 * development. See the pthread-enabled versions (immediately above) for
 * details on these functions.
 *
 */
bk_recursive_lock_h
bk_recursive_lock_create(bk_s B, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  /*
   * We need to return something here that is both non-NULL and (I hope)
   * won't be considered "wild" to Insure.
   */
  BK_RETURN(B, (bk_recursive_lock_h)bk_recursive_lock_create);
}

void
bk_recursive_lock_destroy(bk_s B, bk_recursive_lock_h recursive_lock)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  if (!recursive_lock)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }
  BK_VRETURN(B);
}

int
bk_recursive_lock_grab(bk_s B, bk_recursive_lock_h recursive_lock, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  if (!recursive_lock)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  BK_RETURN(B, 0);
}

int
bk_recursive_lock_release(bk_s B, bk_recursive_lock_h recursive_lock, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  if (!recursive_lock)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  BK_RETURN(B, 0);
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
  if ((u_int64_t)a->btn_B > (u_int64_t)b->btn_B)
  {
    return(1);
  }
  if ((u_int64_t)a->btn_B < (u_int64_t)b->btn_B)
  {
    return(-1);
  }
  return(0);

#if 0
  if (ret) return(ret);
  // DICT_UNIQUE_KEYS should prevent this from ever executing, but just in case
  return (a - b);
#endif
}
// (note a and b reversed from usual, because 'B' cannot be 'a', only 'b' :-)
static int btl_ko_cmp(bk_s b, struct bk_threadnode *a)
{
  if ((u_int64_t)b > (u_int64_t)a->btn_B)
  {
    return(1);
  }
  if ((u_int64_t)b < (u_int64_t)a->btn_B)
  {
    return(-1);
  }
  return(0);
#if 0
  return(b - a->btn_B);
#endif
}
#ifdef ACTUALLY_USED
static ht_val btl_obj_hash(struct bk_threadnode *a)
{
  return (ht_val) a->btn_B;
}
static ht_val btl_key_hash(bk_s b)
{
  return (ht_val) b;
}
#endif // ACTUALLY_USED
