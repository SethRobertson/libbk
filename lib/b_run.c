#if !defined(lint) && !defined(__INSIGHT__)
#include "libbk_compiler.h"
UNUSED static const char libbk__rcsid[] = "$Id: b_run.c,v 1.76 2004/08/27 02:10:15 dupuy Exp $";
UNUSED static const char libbk__copyright[] = "Copyright (c) 2003";
UNUSED static const char libbk__contact[] = "<projectbaka@baka.org>";
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

/**
 * @file
 * All of the baka run public and private functions.
 */

#include <libbk.h>
#include "libbk_internal.h"


#define BK_RUN_GLOBAL_FLAG_ISLOCKED	0x10000	///< Run already locked by me



/**
 * File descriptor cancel information
 */
struct bk_fd_cancel
{
  int		bfc_fd;				///< The file descriptor in question.
  bk_flags	bfc_flags;			///< Everyone needs flags.
#define BK_FD_ADMIN_FLAG_IS_CANCELED		0x4 ///< This file descriptor has been canceled.
#define BK_FD_ADMIN_FLAG_IS_CLOSED		0x8 ///< This file descriptor has been closed.
};



/**
 * Information about a particular event which will be executed at some future point
 */
struct br_equeue
{
  struct timeval	bre_when;		///< Time to run event
  void			(*bre_event)(bk_s B, struct bk_run *run, void *opaque, const struct timeval *starttime, bk_flags flags); ///< Event to run
  void			*bre_opaque;		///< Data for opaque
  bk_flags		bre_flags;		///< BK_RUN_THREADREADY
};



/**
 * Opaque data for cron job event queue function.  Data containing information about
 * true user callback which is scheduled to run at certain interval.
 */
struct br_equeuecron
{
  time_t		brec_interval;		///< msec Interval timer
  void			(*brec_event)(bk_s B, struct bk_run *run, void *opaque, const struct timeval *starttime, bk_flags flags); ///< Event to run
  void		       *brec_opaque;		///< Data for opaque
  struct br_equeue     *brec_equeue;		///< Queued cron event (ie next instance to fire).
  bk_flags		brec_flags;		///< Flags are always useful
//#define BK_RUN_THREADREADY			0x10000 ///< Handler is prepared to run in a thread
#ifdef BK_USING_PTHREADS
  pthread_t		brec_userid;		///< Identifier of thread currently ``using'' this object
  pthread_cond_t	brec_cond;		///< Pthread condition for other threads to wait on
#endif /* BK_USING_PTHREADS */
};



/**
 * Data for bk_run_runevent to fire off a thread to run an event-queue job
 */
struct br_runevent
{
  struct bk_run		       *brre_run;	///< Run environment
  void (*brre_fun)(bk_s B, struct bk_run *run, void *opaque, const struct timeval *starttime, bk_flags flags); ///< Function to call
  void *brre_opaque;				///< Opaque data for function
  struct timeval	       *brre_starttime;	///< Timestamp to hopefully save cycles
  bk_flags		        brre_flags;	///< Flag information for eventq function
};



/**
 * Function to be called synchronously out of bk_run after an async
 * signal was received--implements safe ways of performing complex
 * functionality when signals are received.
 */
struct br_sighandler
{
  void			(*brs_handler)(bk_s B, struct bk_run *run, int signum, void *opaque); ///< Handler
  void			*brs_opaque;		///< Opaque data
};


/**
 * Association between a file descriptor (or handle) and a callback
 * provide the service. Note that when windows compatibility is
 * required, the fd must be replaced by a struct bk_iodescriptor
 */
struct bk_run_fdassoc
{
  int			brf_fd;			///< Fd we are handling
  bk_fd_handler_t	brf_handler;		///< Function to handle
  void		       *brf_opaque;		///< Opaque information
  bk_flags		brf_flags;		///< Handler flags
//#define BK_RUN_THREADREADY			0x10000 ///< Handler is prepared to run in a thread
#ifdef BK_USING_PTHREADS
  pthread_t		brf_userid;		///< Identifier of thread currently ``using'' this object
  pthread_cond_t	brf_cond;		///< Pthread condition for other threads to wait on
#endif /* BK_USING_PTHREADS */
};



/**
 * Data for bk_run_runfd to fire off a thread to run an fd job
 */
struct br_runfd
{
  struct bk_run		       *brrf_run;	///< Run environment
  int				brrf_fd;	///< File descriptor
  u_int				brrf_gottypes;	///< Type of activty
  bk_fd_handler_t		brrf_fun;	///< Function to call
  void			       *brrf_opaque;	///< Opaque data
  struct timeval	       *brrf_starttime;	///< Timestamp to hopefulyl save cycles
};



/**
 * All information known about events on the system.  Note that when
 * windows compatibility is required, the fd_sets must be supplemented
 * by a list of bk_iodescriptors obtaining the various read/write/xcpt
 * requirements since these may not be expressible in an fdset.
 */
struct bk_run
{
  fd_set		br_readset;		///< FDs interested in this operation
  fd_set		br_writeset;		///< FDs interested in this operation
  fd_set		br_xcptset;		///< FDs interested in this operation
  int			br_selectn;		///< Highest FD (+1) in fdsets
  dict_h		br_fdassoc;		///< FD to callback association
  dict_h		br_poll_funcs;		///< Poll functions
  dict_h		br_ondemand_funcs;	///< On demands functions
  dict_h		br_idle_funcs;		///< Idle tasks (nothing else to do)
  pq_h			br_equeue;		///< Event queue
  sigset_t		br_runsignals;		///< What signals we are handling with bk_run_signals
  volatile sig_atomic_t	br_signums[NSIG];	///< Number of signal events we have received
  struct br_sighandler	br_handlerlist[NSIG];	///< Handlers for signals
  bk_flags		br_flags;		///< General flags
#define BK_RUN_FLAG_RUN_OVER		0x001	///< bk_run_run should terminate
#define BK_RUN_FLAG_NEED_POLL		0x002	///< Execute poll list
#define BK_RUN_FLAG_CHECK_DEMAND	0x004	///< Check demand list
#define BK_RUN_FLAG_HAVE_IDLE		0x008	///< Run idle task
#define BK_RUN_FLAG_IN_DESTROY		0x010	///< In the middle of a destroy
#define BK_RUN_FLAG_ABORT_RUN_ONCE	0x020	///< Return now from run_once
#define BK_RUN_FLAG_DONT_BLOCK_RUN_ONCE	0x040	///< Don't block on current run once
#define BK_RUN_FLAG_FD_CANCEL		0x080	///< At least 1 fd on cancel list
#define BK_RUN_FLAG_FD_CLOSED		0x100	///< At least 1 fd on cancel list is closed
#define BK_RUN_FLAG_SIGNAL_THREAD	0x200	///< Only one thread should receive signals
  dict_h		br_canceled;		///< List of canceled descriptors.
#ifdef BK_USING_PTHREADS
  pthread_t		br_signalthread;	///< Specify thread to receive signals
  pthread_t		br_iothread;		///< Thread handling io for this struct
  pthread_mutex_t	br_lock;		///< Lock on run management
  int			br_runfd;		///< File descriptor for select interrupt
  int			br_selectcount;		///< Number of entries in select
#endif /* BK_USING_PTHREADS */
};


/**
 * Information about a polling or idle function known to the run environment
 */
struct bk_run_func
{
  void *	brfn_key;			///< Key for searching
  bk_run_f	brfn_fun;			///< Function to call
  void *	brfn_opaque;			///< User args
  bk_flags	brfn_flags;			///< Everyone needs flags
  dict_h	brfn_backptr;			///< Pointer to enclosing dll
#ifdef BK_USING_PTHREADS
  pthread_t		brfn_userid;		///< Identifier of thread currently ``using'' this object
  pthread_cond_t	brfn_cond;		///< Pthread condition for other threads to wait on
#endif /* BK_USING_PTHREADS */
};


/**
 * Information about an on-demand function known to the run environment
 */
struct bk_run_ondemand_func
{
  void *		brof_key;		///< Key for searching
  bk_run_on_demand_f	brof_fun;		///< Function to call
  void *		brof_opaque;		///< User args
  bk_flags		brof_flags;		///< Everyone needs flags
  dict_h		brof_backptr;		///< Pointer to enclosing dll
  volatile int *	brof_demand;		///< Trigger for execution
#ifdef BK_USING_PTHREADS
  pthread_t		brof_userid;		///< Identifier of thread currently ``using'' this object
  pthread_cond_t	brof_cond;		///< Pthread condition for other threads to wait on
#endif /* BK_USING_PTHREADS */
};



static int bk_run_event_comparator(struct br_equeue *a, struct br_equeue *b);
static void bk_run_event_cron(bk_s B, struct bk_run *run, void *opaque, const struct timeval *starttime, bk_flags flags);
static int bk_run_checkeventq(bk_s B, struct bk_run *run, struct timeval *starttime, struct timeval *delta, u_int *event_cntp);
static struct bk_run_func *brfn_alloc(bk_s B);
static void brfn_destroy(bk_s B, struct bk_run_func *brf);
static struct bk_run_ondemand_func *brof_alloc(bk_s B);
static void brof_destroy(bk_s B, struct bk_run_ondemand_func *brof);
#ifdef BK_USING_PTHREADS
static void *bk_run_runevent_thread(bk_s B, void *opaque);
#endif /* BK_USING_PTHREADS */
static void bk_run_runevent(bk_s B, struct bk_run *run, void (*fun)(bk_s B, struct bk_run *run, void *opaque, const struct timeval *starttime, bk_flags flags), void *opaque, struct timeval *starttime, bk_flags eventflags, bk_flags flags);
static void bk_run_runfd(bk_s B, struct bk_run *run, int fd, u_int gottypes, bk_fd_handler_t fun, void *opaque, struct timeval *starttime, bk_flags flags);
#ifdef BK_USING_PTHREADS
static int bk_run_select_changed_init(bk_s B, struct bk_run *run);
static void *bk_run_runfd_thread(bk_s B, void *opaque);
#endif /* BK_USING_PTHREADS */
static struct bk_run_fdassoc *brf_create(bk_s B, bk_flags flags);
static void brf_destroy(bk_s B, struct bk_run_fdassoc *brf);



/**
 * @name Defines: fdassoc_clc
 * File Descriptor association CLC definitions
 * to hide CLC choice.
 */
// @{
#define fdassoc_create(o,k,f,a)		ht_create((o),(k),(f),(a))
#define fdassoc_destroy(h)		ht_destroy(h)
#define fdassoc_insert(h,o)		ht_insert((h),(o))
#define fdassoc_insert_uniq(h,n,o)	ht_insert_uniq((h),(n),(o))
#define fdassoc_append(h,o)		ht_append((h),(o))
#define fdassoc_append_uniq(h,n,o)	ht_append_uniq((h),(n),(o))
#define fdassoc_search(h,k)		ht_search((h),(k))
#define fdassoc_delete(h,o)		ht_delete((h),(o))
#define fdassoc_minimum(h)		ht_minimum(h)
#define fdassoc_maximum(h)		ht_maximum(h)
#define fdassoc_successor(h,o)		ht_successor((h),(o))
#define fdassoc_predecessor(h,o)	ht_predecessor((h),(o))
#define fdassoc_iterate(h,d)		ht_iterate((h),(d))
#define fdassoc_nextobj(h,i)		ht_nextobj(h,i)
#define fdassoc_iterate_done(h,i)	ht_iterate_done(h,i)
#define fdassoc_error_reason(h,i)	ht_error_reason((h),(i))
static int fa_oo_cmp(struct bk_run_fdassoc *a, struct bk_run_fdassoc *b);
static int fa_ko_cmp(int *a, struct bk_run_fdassoc *b);
static ht_val fa_obj_hash(struct bk_run_fdassoc *a);
static ht_val fa_key_hash(int *a);
static struct ht_args fa_args = { 128, 1, (ht_func)fa_obj_hash, (ht_func)fa_key_hash };
// @}



/**
 * @name Defines: List of canceled descriptors This data structure allows
 * administrative shutdown of a descriptor. It's up to interested parties
 * to check if their descriptor is on the list.
 */
// @{
#define fd_cancel_create(o,k,f)	dll_create((o),(k),(f))
#define fd_cancel_destroy(h)		dll_destroy(h)
#define fd_cancel_insert(h,o)		dll_insert((h),(o))
#define fd_cancel_insert_uniq(h,n,o)	dll_insert_uniq((h),(n),(o))
#define fd_cancel_append(h,o)		dll_append((h),(o))
#define fd_cancel_append_uniq(h,n,o)	dll_append_uniq((h),(n),(o))
#define fd_cancel_search(h,k)		dll_search((h),(k))
#define fd_cancel_delete(h,o)		dll_delete((h),(o))
#define fd_cancel_minimum(h)		dll_minimum(h)
#define fd_cancel_maximum(h)		dll_maximum(h)
#define fd_cancel_successor(h,o)	dll_successor((h),(o))
#define fd_cancel_predecessor(h,o)	dll_predecessor((h),(o))
#define fd_cancel_iterate(h,d)	dll_iterate((h),(d))
#define fd_cancel_nextobj(h,i)	dll_nextobj(h,i)
#define fd_cancel_iterate_done(h,i)	dll_iterate_done(h,i)
#define fd_cancel_error_reason(h,i)	dll_error_reason((h),(i))
static int fd_cancel_oo_cmp(struct bk_fd_cancel *a, struct bk_fd_cancel *b);
static int fd_cancel_ko_cmp(int *a, struct bk_fd_cancel *b);
// @}



/**
 * @name Defines: fdassoc_clc
 * baka-run function (poll, idle) association CLC definitions
 * to hide CLC choice.
 */
// @{
#define brfl_create(o,k,f)		dll_create((o),(k),(f))
#define brfl_destroy(h)			dll_destroy(h)
#define brfl_insert(h,o)		dll_insert((h),(o))
#define brfl_insert_uniq(h,n,o)		dll_insert_uniq((h),(n),(o))
#define brfl_append(h,o)		dll_append((h),(o))
#define brfl_append_uniq(h,n,o)		dll_append_uniq((h),(n),(o))
#define brfl_search(h,k)		dll_search((h),(k))
#define brfl_delete(h,o)		dll_delete((h),(o))
#define brfl_minimum(h)			dll_minimum(h)
#define brfl_maximum(h)			dll_maximum(h)
#define brfl_successor(h,o)		dll_successor((h),(o))
#define brfl_predecessor(h,o)		dll_predecessor((h),(o))
#define brfl_iterate(h,d)		dll_iterate((h),(d))
#define brfl_nextobj(h,i)		dll_nextobj(h,i)
#define brfl_iterate_done(h,i)		dll_iterate_done(h,i)
#define brfl_error_reason(h,i)		dll_error_reason((h),(i))
static int brfl_oo_cmp(struct bk_run_func *a, struct bk_run_func *b);
static int brfl_ko_cmp(void *a, struct bk_run_func *b);
// @}



/**
 * @name Defines: brofl_clc
 * baka run on-demand function association CLC definitions
 * to hide CLC choice.
 */
// @{
#define brofl_create(o,k,f)		dll_create((o),(k),(f))
#define brofl_destroy(h)		dll_destroy(h)
#define brofl_insert(h,o)		dll_insert((h),(o))
#define brofl_insert_uniq(h,n,o)	dll_insert_uniq((h),(n),(o))
#define brofl_append(h,o)		dll_append((h),(o))
#define brofl_append_uniq(h,n,o)	dll_append_uniq((h),(n),(o))
#define brofl_search(h,k)		dll_search((h),(k))
#define brofl_delete(h,o)		dll_delete((h),(o))
#define brofl_minimum(h)		dll_minimum(h)
#define brofl_maximum(h)		dll_maximum(h)
#define brofl_successor(h,o)		dll_successor((h),(o))
#define brofl_predecessor(h,o)		dll_predecessor((h),(o))
#define brofl_iterate(h,d)		dll_iterate((h),(d))
#define brofl_nextobj(h,i)		dll_nextobj(h,i)
#define brofl_iterate_done(h,i)		dll_iterate_done(h,i)
#define brofl_error_reason(h,i)		dll_error_reason((h),(i))
static int brofl_oo_cmp(struct bk_run_ondemand_func *a, struct bk_run_ondemand_func *b);
static int brofl_ko_cmp(void *a, struct bk_run_ondemand_func *b);
// @}



/**
 * @name Signal static variables
 *
 * Data which signal handlers running on the signal stack can modify.
 * This is used for synchronous signal handling (the signal handler simply
 * sets the beensignaled variable and the increments number of signals received
 * for this signal, and the baka run environment will call a handler
 * running in the normal context/stack as part of the baka run loop.
 */
// @{
static volatile sig_atomic_t		(*br_signums)[NSIG];
static volatile sig_atomic_t		br_beensignaled;
// @}



/**
 * Create and initialize the run environment.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state
 *	@param flags Flags for future expansion--saved through run structure.
 *	@return <i>NULL</i> on call failure, allocation failure, or other fatal error.
 *	@return <br><i>The</i> initialized baka run structure if successful.
 */
struct bk_run *bk_run_init(bk_s B, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_run *run;

  if (!(run = malloc(sizeof(*run))))
  {
    bk_error_printf(B, BK_ERR_ERR, "Cannot create run structure: %s\n",strerror(errno));
    BK_RETURN(B, NULL);
  }
  BK_ZERO(run);

#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISSET(flags, BK_RUN_WANT_SIGNALTHREAD))
  {
    run->br_signalthread = pthread_self();
    BK_FLAG_SET(run->br_flags, BK_RUN_FLAG_SIGNAL_THREAD);
  }

  run->br_runfd = -1;

  pthread_mutex_init(&run->br_lock, NULL);
#endif /* BK_USING_PTHREADS */

  run->br_flags = flags;

  br_signums = &run->br_signums;			// Initialize static signal array ptr
  br_beensignaled = 0;

  sigemptyset(&run->br_runsignals);

  if (!(run->br_fdassoc = fdassoc_create((dict_function)fa_oo_cmp, (dict_function)fa_ko_cmp, DICT_HT_STRICT_HINTS, &fa_args)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Cannot create fd association: %s\n",fdassoc_error_reason(NULL, NULL));
    goto error;
  }

  if (!(run->br_equeue = pq_create((pq_compfun)bk_run_event_comparator, PQ_NOFLAGS)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Cannot create event queue: %s\n",pq_error_reason(NULL, NULL));
    goto error;
  }

  if (!(run->br_poll_funcs = brfl_create((dict_function)brfl_oo_cmp,(dict_function)brfl_ko_cmp, DICT_UNORDERED)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create poll function list\n");
    goto error;
  }

  if (!(run->br_idle_funcs = brfl_create((dict_function)brfl_oo_cmp,(dict_function)brfl_ko_cmp, DICT_UNORDERED)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create idle function list\n");
    goto error;
  }

  if (!(run->br_ondemand_funcs = brfl_create((dict_function)brofl_oo_cmp,(dict_function)brofl_ko_cmp, DICT_UNORDERED)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create on demand function list\n");
    goto error;
  }

  if (!(run->br_canceled = fd_cancel_create((dict_function)fd_cancel_oo_cmp, (dict_function)fd_cancel_ko_cmp, DICT_UNORDERED)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create cancel list: %s\n", fd_cancel_error_reason(NULL, NULL));
    goto error;
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADREADY(B))
  {
    if ((run->br_runfd = bk_run_select_changed_init(B, run)) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create select interrupt fd\n");
      goto error;
    }
  }
#endif /* BK_USING_PTHREADS */


  BK_RETURN(B, run);

 error:
  if (run)
    bk_run_destroy(B, run);

  BK_RETURN(B, NULL);
}



/**
 * Destroy the baka run environment.
 *
 * THREADS: MT-SAFE (assuming different run)
 * THREADS: RE-ENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state
 *	@param run The baka run environment state
 */
void bk_run_destroy(bk_s B, struct bk_run *run)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int signum;
  struct bk_run_func *brfn;
  struct bk_run_ondemand_func *brof;
  struct timeval curtime;
  struct bk_fd_cancel *bfc;

  if (!run)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (BK_FLAG_ISSET(run->br_flags, BK_RUN_FLAG_IN_DESTROY))
    BK_VRETURN(B);

  BK_FLAG_SET(run->br_flags, BK_RUN_FLAG_IN_DESTROY);

  gettimeofday(&curtime,0);

  // Dequeue the events
  if (run->br_equeue)
  {
    struct br_equeue *cur;

    while (cur = pq_extract_head(run->br_equeue))
    {
      bk_run_runevent(B, run, cur->bre_event, cur->bre_opaque, &curtime, BK_RUN_DESTROY, cur->bre_flags);
      free(cur);
    }
  }

  // Destroy the fd association
  if (run->br_fdassoc)
  {
    struct bk_run_fdassoc *cur;

    while (cur = fdassoc_minimum(run->br_fdassoc))
    {
      // Get rid of event in list, which will also prevent double deletion
      if (fdassoc_delete(run->br_fdassoc, cur) != DICT_OK)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not delete descriptor %d from fdassoc list: %s\n", cur->brf_fd, fdassoc_error_reason(run->br_fdassoc, NULL));
	break;					// DLL hopelessly mangled
      }
      bk_run_runfd(B, run, cur->brf_fd, BK_RUN_DESTROY, cur->brf_handler, cur->brf_opaque, &curtime, cur->brf_flags);
      free(cur);
    }
  }

  // XXX Consider saving all the original sig handlers instead of just restoring default.
  // Reset signal handlers to their default actions
  for(signum = 1;signum < NSIG;signum++)
  {
    if (run->br_handlerlist[signum].brs_handler)
      bk_run_signal(B, run, signum, (void *)(SIG_IGN), NULL, 0);
  }

  while (brfn=brfl_minimum(run->br_poll_funcs))
  {
    (*brfn->brfn_fun)(B, run, brfn->brfn_opaque, &curtime, NULL, BK_RUN_DESTROY);
    brfn_destroy(B, brfn);
  }
  brfl_destroy(run->br_poll_funcs);

  while (brfn=brfl_minimum(run->br_idle_funcs))
  {
    (*brfn->brfn_fun)(B, run, brfn->brfn_opaque, &curtime, NULL, BK_RUN_DESTROY);
    brfn_destroy(B, brfn);
  }
  brfl_destroy(run->br_idle_funcs);

  while (brof=brofl_minimum(run->br_ondemand_funcs))
  {
    (*brof->brof_fun)(B, run, brof->brof_opaque, brof->brof_demand, &curtime, BK_RUN_DESTROY);
    brof_destroy(B, brof);
  }
  brofl_destroy(run->br_ondemand_funcs);

  while(bfc = fd_cancel_minimum(run->br_canceled))
  {
    bk_run_fd_cancel_unregister(B, run, bfc->bfc_fd, BK_FD_ADMIN_FLAG_WANT_ALL);
  }
  fd_cancel_destroy(run->br_canceled);

  br_signums = NULL;

#ifdef BK_USING_PTHREADS
  if (pthread_mutex_destroy(&run->br_lock) != 0)
    abort();

  if (run->br_runfd >= 0)
    close(run->br_runfd);
#endif /* BK_USING_PTHREADS */

  if (run->br_equeue)
    pq_destroy(run->br_equeue);

  if (run->br_fdassoc)
    fdassoc_destroy(run->br_fdassoc);

  free(run);

  BK_VRETURN(B);
}



/**
 * Set (or clear) a synchronous handler for some signal.
 *
 * Default is to interrupt system calls
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA thread/global state
 *	@param run The baka run environment state
 *	@param signum The signal number to install a synchronous signal handler for.
 *	@param handler The function to call during the run event loop if a signal was received.
 *	@param opaque The opaque data for the handler.
 *	@param flags Flags for future expansion.
 *	@return <i>-1</i> on call failure, system call failure, or other failure.
 *	@return <br><i>0</i> on success.
 */
int bk_run_signal(bk_s B, struct bk_run *run, int signum, void (*handler)(bk_s B, struct bk_run *run, int signum, void *opaque), void *opaque, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct sigaction act;
  sigset_t blockset;

  if (!run || signum >= NSIG)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  sigemptyset(&blockset);

  if (!handler || (ptr2uint_t)handler == (ptr2uint_t)SIG_IGN || (ptr2uint_t)handler == (ptr2uint_t)SIG_DFL)
  {							// Disabling signal
#ifdef __INSURE__
    if ((ptr2uint_t)handler == (ptr2uint_t)SIG_IGN)
      act.sa_handler = SIG_IGN;
    else
      act.sa_handler = (void *)handler;
#else /* __INSURE__ */    
    act.sa_handler = (void *)handler;
#endif /* __INSURE__ */
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;

    sigdelset(&run->br_runsignals, signum);

    sigaddset(&blockset, signum);
    sigprocmask(SIG_BLOCK, &blockset, NULL);
  }
  else
  {							// Enabling signal
    act.sa_handler = bk_run_signal_ihandler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;

    sigaddset(&run->br_runsignals, signum);
    sigprocmask(SIG_BLOCK, &run->br_runsignals, NULL);
  }

  // Decide on appropriate system call interrupt/restart semantic (default interrupt)
#ifdef SA_RESTART
  act.sa_flags |= BK_FLAG_ISSET(flags, BK_RUN_SIGNAL_RESTART)?SA_RESTART:0;
#endif /* SA_RESTART */
#ifdef SA_INTERRUPT
  act.sa_flags |= BK_FLAG_ISSET(flags, BK_RUN_SIGNAL_RESTART)?0:SA_INTERRUPT;
#endif /* SA_INTERRUPT */

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */
  run->br_handlerlist[signum].brs_handler = handler;	// Might be NULL
  run->br_handlerlist[signum].brs_opaque = opaque;	// Might be NULL
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&BkGlobalSignalLock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (BK_FLAG_ISSET(flags, BK_RUN_SIGNAL_CLEARPENDING))
    run->br_signums[signum] = 0;

  if (sigaction(signum, &act, NULL) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert signal: %s\n",strerror(errno));
    BK_RETURN(B, -1);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&BkGlobalSignalLock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  sigprocmask(SIG_UNBLOCK, &blockset, NULL);

  BK_RETURN(B, 0);
}



/**
 * Enqueue an event for future action.
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA thread/global state
 *	@param run The baka run environment state
 *	@param when The UTC timeval when the event handler should be called.
 *	@param event The handler to fire when the time comes (or we are destroyed).
 *	@param opaque The opaque data for the handler
 *	@param handle A copy-out parameter to allow someone to dequeue the event in the future
 *	@param flags Flags for the Future.
 *	@return <i><0</i> on call failure, allocation failure, or other error.
 *	@return <br><i>0</i> on success.
 */
int bk_run_enqueue(bk_s B, struct bk_run *run, struct timeval when, void (*event)(bk_s B, struct bk_run *run, void *opaque, const struct timeval *starttime, bk_flags flags), void *opaque, void **handle, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct br_equeue *new;

  if (!run || !event)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (BK_FLAG_ISSET(run->br_flags, BK_RUN_FLAG_IN_DESTROY))
  {
    bk_error_printf(B, BK_ERR_ERR, "Cannot enqueue event when we are in destroy\n");
    BK_RETURN(B, -1);
  }

  if (!(new = malloc(sizeof(*new))))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate event queue structure: %s\n",strerror(errno));
    BK_RETURN(B, -1);
  }

  new->bre_when = when;
  new->bre_event = event;
  new->bre_opaque = opaque;
  new->bre_flags = flags;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (pq_insert(run->br_equeue, new) != PQ_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert into event queue: %s\n",pq_error_reason(run->br_equeue, NULL));
    goto error;
  }

#ifdef BK_USING_PTHREADS
    if (run->br_selectcount)
    {
      bk_debug_printf_and(B, 64, "Asking for runrun rescheduling\n");
      bk_run_select_changed(B, run, BK_RUN_GLOBAL_FLAG_ISLOCKED);
    }

  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  // Save structure if requested for later CLC delete
  if (handle)
    *handle = new;

  BK_RETURN(B, 0);

 error:
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (new) free(new);
  BK_RETURN(B, -1);
}



/**
 * Enqueue an event for a future action
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA thread/global state
 *	@param run The baka run environment state
 *	@param msec The number of milliseconds until the event should fire
 *	@param event The handler to fire when the time comes (or we are destroyed).
 *	@param opaque The opaque data for the handler
 *	@param handle A copy-out parameter to allow someone to dequeue the event in the future
 *	@param flags Flags for the Future.
 *	@return <i>-1</i> on call failure, allocation failure, or other error.
 *	@return <br><i>0</i> on success.
 */
int bk_run_enqueue_delta(bk_s B, struct bk_run *run, time_t msec, void (*event)(bk_s B, struct bk_run *run, void *opaque, const struct timeval *starttime, bk_flags flags), void *opaque, void **handle, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct timeval tv, diff;

  if (!run || !event)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  diff.tv_sec = msec/1000;
  diff.tv_usec = (msec%1000)*1000;
  gettimeofday(&tv, NULL);

  BK_TV_ADD(&tv,&tv,&diff);

  BK_RETURN(B, bk_run_enqueue(B, run, tv, event, opaque, handle, flags));
}



/**
 * Set up a reoccurring event at a periodic interval
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA thread/global state
 *	@param run The baka run environment state
 *	@param msec The number of milliseconds until the event should fire
 *	@param event The handler to fire when the time comes (or we are destroyed).
 *	@param opaque The opaque data for the handler
 *	@param handle A copy-out parameter to allow someone to dequeue the event in the future
 *	@param flags Flags for the Future.
 *	@return <i><0</i> on call failure, allocation failure, or other error.
 *	@return <br><i>0</i> on success.
 */
int bk_run_enqueue_cron(bk_s B, struct bk_run *run, time_t msec, void (*event)(bk_s B, struct bk_run *run, void *opaque, const struct timeval *starttime, bk_flags flags), void *opaque, void **handle, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct br_equeuecron *brec;
  int ret;

  if (!run || !event)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!(brec = malloc(sizeof(*brec))))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate storage for cron event: %s\n",strerror(errno));
    BK_RETURN(B, -1);
  }

  brec->brec_interval = msec;
  brec->brec_event = event;
  brec->brec_opaque = opaque;
  brec->brec_flags = flags;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B))
  {
    BK_ZERO(&brec->brec_userid);		// Here's hoping zero is reserved
    if (pthread_cond_init(&brec->brec_cond, NULL) < 0)
      abort();
  }
#endif /* BK_USING_PTHREADS */

  ret = bk_run_enqueue_delta(B, run, brec->brec_interval, bk_run_event_cron, brec, ((void **)&brec->brec_equeue), flags);

  if (handle)
    *handle = brec;

  BK_RETURN(B, ret);
}



/**
 * Dequeue a normal event or a full "cron" job.
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA thread/global state
 *	@param run The baka run environment state
 *	@param handle The handle to dequeue the event in the future
 *	@param flags Flags for the Future.
 *	@return <i><0</i> on call failure, or other error.
 *	@return <br><i>0</i> on success.
 */
int bk_run_dequeue(bk_s B, struct bk_run *run, void *handle, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct br_equeuecron *brec = handle;
  struct br_equeue *bre = handle;

  if (!run || !handle)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (BK_FLAG_ISSET(flags, BK_RUN_DEQUEUE_CRON))
  {
    bre = brec->brec_equeue;
#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
      abort();

    if (BK_GENERAL_FLAG_ISTHREADON(B) && brec->brec_userid)
    {
      int isme = pthread_equal(brec->brec_userid, pthread_self());

      BK_ZERO(&brec->brec_userid);		// Mark as deleted

      // Wait for the current user to finish
      if (!isme)
	pthread_cond_wait(&brec->brec_cond, &run->br_lock);

      BK_RETURN(B, 0);				// Running thread handled everything
    }
    else
    {
      pthread_cond_destroy(&brec->brec_cond);
      free(brec);
    }

    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
      abort();
#else /* BK_USING_PTHREADS */
    free(brec);
#endif /* BK_USING_PTHREADS */
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */
  if (run->br_equeue)				// avoid segfault on shutdown
    pq_delete(run->br_equeue, bre);
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */


  free(bre);

  BK_RETURN(B, 0);
}



/**
 * Run the event loop until ``the end''.
 *
 * The end is defined as one of the standard functions failing, or
 * someone setting the BK_RUN_RUN_OVER flag.
 *
 *	@param B BAKA thread/global state
 *	@param run The baka run environment state
 *	@param flags Flags for the Future.
 *	@return <i><0</i> on call failure, or other error.
 *	@return <br><i>0</i> on success.
 */
int bk_run_run(bk_s B, struct bk_run *run, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret = 0;

  if (!run)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */
  BK_FLAG_CLEAR(run->br_flags, BK_RUN_FLAG_RUN_OVER); // Don't inherit previous setting
  run->br_iothread = pthread_self();
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  // <WARNING>We are assuming that br_flags will either have old or new value....</WARNING>
  while (BK_FLAG_ISCLEAR(run->br_flags, BK_RUN_FLAG_RUN_OVER))
  {
    if ((ret = bk_run_once(B, run, flags)) < 0)
      break;
  }

  BK_RETURN(B, ret);
}



/**
 * Run through all events once. Don't call from a callback or beware.
 *
 * THREADS: MT-SAFE (pending race condition vs test for user fun and calling user fun)
 *
 *	@param B BAKA thread/global state
 *	@param run The baka run environment state
 *	@param flags Flags for the Future.
 *	@return <i><0</i> on call failure, or other error.
 *	@return <br><i>0</i> on success.
 */
int bk_run_once(bk_s B, struct bk_run *run, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  static const fd_set zeroset;
  static const struct timeval tzero = {0, 0};
  fd_set readset, writeset, xcptset;
  struct timeval timenow, deltaevent, deltapoll, timeout;
  struct timeval *curtime = NULL;
  const struct timeval *selectarg = NULL;
  int ret;
  int x;
  int haderror = 0;
  int use_deltapoll, check_idle;
  u_int event_cnt;
  int isinselect = 0;
#ifdef BK_USING_PTHREADS
  int islocked = 0;
  int wantsignals = 0;
#else /* BK_USING_PTHREADS */
  int wantsignals = 1;
#endif /* BK_USING_PTHREADS */

  deltapoll.tv_sec = INT32_MAX;
  deltapoll.tv_usec = INT32_MAX;

  if (!run)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISCLEAR(run->br_flags, BK_RUN_FLAG_SIGNAL_THREAD) ||
      pthread_equal(run->br_signalthread, pthread_self()))
    wantsignals = 1;
#endif /* BK_USING_PTHREADS */

#define BK_RUN_ONCE_ABORT_CHECK()						\
  if (BK_FLAG_ISSET(run->br_flags, BK_RUN_FLAG_ABORT_RUN_ONCE))			\
    goto abort_run_once;							\
  if (wantsignals && sigprocmask(SIG_UNBLOCK, &run->br_runsignals, NULL) < 0 ||	\
      sigprocmask(SIG_BLOCK, &run->br_runsignals, NULL) < 0 ||			\
      br_beensignaled)								\
    goto beensignaled;

  /*
   * The purpose of the flag should be explained. libbk supports the idea
   * of "blocking" reads in an otherwise asynchronous environment. The
   * details of this are explained elsewhere (presumably b_ioh.c), but it
   * suffices here to point out (or remind) the reader that this fact means
   * that at any given moment we might have *two* bk_run_once()'s calls
   * active. One, more or less at the base of the stack, which is the
   * bk_run_once() loop that you expect to exist. The other occurs when
   * someone has invoked one of these "blocking" things. In general these
   * two invocations coexist without too much fuss, but there is one detail
   * for which we must account. Suppose by way of example that the "base"
   * (or expected) bk_run_once() has returned from select(2) with 3 fds
   * ready for operation. Let's call them 4, 5, and 6. While processing
   * descriptor 4 (the first in the list), the program invokes a "blocking"
   * read. bk_run_once() gets called again (remember the "base" version is
   * still in the process of completing). It will enter select(2) and since
   * neither file descriptor 5, or 6 have been processed, they will (still)
   * be ready and thus return. So let's claim they now both get fully dealt
   * with and that shortly thereafter whatever condition causes the
   * blocking bk_run_once() to return becomes true and we return all the
   * way back to the "base" bk_run_once() which now *continues*. Because
   * nothing has reset the fd sets which select(2) returned all that time
   * ago, it will go on to process descriptors 5 and 6. At *best* this is a
   * waste of time; and at worst this could cause really bizarre problems
   * (like double frees or some such). Thus we need to configure a way that
   * the blocking bk_run_once() can "communicate" back to the base
   * bk_run_once() that it (the blocking versions) has run and that any
   * state in the base version may be invalid. The base version's response
   * to this is very simple; just abort the current run and go around
   * again. It never *hurts* to do this, in general; it just wastes time
   * (though in this case, obviously, it doesn't waste time; it prevents
   * core dumps). The strategy we employ is childishly simple. Set a run
   * structure flag as you *leave* bk_run_once() which is reset immediately
   * upon entry. 99.999....9% of the time this is just one two-stage no-op,
   * but in the case of the blocking bk_run_once() this actually has some
   * effect. The flag is set by when the blocking version exits. So when
   * control is returned to the base version (in medias res) this flag is
   * set. You will notice that we check it many times in the function
   * (basically everywhere we also check for signals). As soon as the base
   * version notices that the flag is set, it immediately aborts the
   * current run (therefore will not attempt to process any more "dead"
   * state), returns, and clears the flag on the next entry. Whew -jtt
   */
  if (BK_FLAG_ISSET(run->br_flags, BK_RUN_FLAG_ABORT_RUN_ONCE))
  {
#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */
    BK_FLAG_CLEAR(run->br_flags, BK_RUN_FLAG_ABORT_RUN_ONCE);
#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */
  }

  bk_debug_printf_and(B,4,"Starting bk_run_once\n");

  check_idle = 0;
  use_deltapoll = 0;

  BK_RUN_ONCE_ABORT_CHECK();


  /*
   *      O N   D E M A N D   C H E C K
   */
  if (BK_FLAG_ISSET(run->br_flags, BK_RUN_FLAG_CHECK_DEMAND))
  {
    struct bk_run_ondemand_func *brof;
    dict_iter iter;

#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */

    iter = brfl_iterate(run->br_ondemand_funcs, DICT_FROM_START);
    while (brof = brfl_nextobj(run->br_ondemand_funcs, iter))
    {
      if (*brof->brof_demand)
      {
#ifdef BK_USING_PTHREADS
	if (BK_GENERAL_FLAG_ISTHREADON(B))
	{
	  if (brof->brof_userid)
	  {
	    bk_debug_printf_and(B, 1, "Cannot call ondemand function %p, locked by %d\n", brof->brof_fun, (int)brof->brof_userid);
	    continue;				// Someone already calling this function
	  }

	  brof->brof_userid = pthread_self();	// Who is has a soft-lock on this structure
	}

	if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
	  abort();
#endif /* BK_USING_PTHREADS */

	if (!curtime && BK_FLAG_ISSET(brof->brof_flags, BK_RUN_HANDLE_TIME))
	{
	  gettimeofday(&timenow, NULL);
	  curtime = &timenow;
	}
	if ((*brof->brof_fun)(B, run, brof->brof_opaque, brof->brof_demand,
			      curtime, 0) < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "On demand callback failed\n");
	  haderror = 1;
	}

#ifdef BK_USING_PTHREADS
	// Look again, we may have been deleted in the interim
	if (BK_GENERAL_FLAG_ISTHREADON(B) && (brof = brfl_search(run->br_ondemand_funcs, brof)))
	{
	  BK_ZERO(&brof->brof_userid);		// Here's hoping zero is reserved
	  pthread_cond_signal(&brof->brof_cond);
	}
#endif /* BK_USING_PTHREADS */

	if (haderror)
	  goto error;

#ifdef BK_USING_PTHREADS
	if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
	  abort();
#endif /* BK_USING_PTHREADS */
      }
    }
    brfl_iterate_done(run->br_ondemand_funcs, iter);
#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */
  }

  BK_RUN_ONCE_ABORT_CHECK();


  /*
   *      P O L L I N G   C H E C K
   */
  if (BK_FLAG_ISSET(run->br_flags, BK_RUN_FLAG_NEED_POLL))
  {
    struct bk_run_func *brfn;
    struct timeval tmp_deltapoll = deltapoll;
    dict_iter iter;

#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */

    iter = brfl_iterate(run->br_poll_funcs, DICT_FROM_START);
    while (brfn = brfl_nextobj(run->br_poll_funcs, iter))
    {
#ifdef BK_USING_PTHREADS
      if (BK_GENERAL_FLAG_ISTHREADON(B))
      {
	if (brfn->brfn_userid)
	{
	  bk_debug_printf_and(B, 1, "Cannot call poll function %p, locked by %d\n", brfn->brfn_fun, (int)brfn->brfn_userid);
	  continue;				// Someone already calling this function
	}

	brfn->brfn_userid = pthread_self();	// Who is has a soft-lock on this structure
      }

      if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
	abort();
#endif /* BK_USING_PTHREADS */

      if (!curtime && BK_FLAG_ISSET(brfn->brfn_flags, BK_RUN_HANDLE_TIME))
      {
	gettimeofday(&timenow, NULL);
	curtime = &timenow;
      }
      if ((ret = (*brfn->brfn_fun)(B, run, brfn->brfn_opaque, curtime,
				   &tmp_deltapoll, 0)) < 0)
      {
	bk_error_printf(B, BK_ERR_WARN, "Polling callback failed\n");
	haderror = 1;
      }

      if (ret == 1)
      {
	if (!use_deltapoll || (BK_TV_CMP(&tmp_deltapoll, &deltapoll) < 0))
	{
	  deltapoll=tmp_deltapoll;
	}
	use_deltapoll=1;
      }

#ifdef BK_USING_PTHREADS
      // Look again, we may have been deleted in the interim
      if (BK_GENERAL_FLAG_ISTHREADON(B) && (brfn = brfl_search(run->br_ondemand_funcs, brfn)))
      {
	BK_ZERO(&brfn->brfn_userid);  // Here's hoping zero is reserved
	pthread_cond_signal(&brfn->brfn_cond);
      }
#endif /* BK_USING_PTHREADS */

      if (haderror)
	goto error;

#ifdef BK_USING_PTHREADS
      if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
	abort();
#endif /* BK_USING_PTHREADS */
    }
    brfl_iterate_done(run->br_poll_funcs, iter);
#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */

  }

  BK_RUN_ONCE_ABORT_CHECK();

  /*
   * If this is the final run, or if we shouldn't block, then turn select(2)
   * into a poll.  This ensures that should we turn off select in an event
   * (the final event of the run), we don't block forever owing to lack of
   * descriptors in the select set.
   */
    if (BK_FLAG_ISSET(run->br_flags, BK_RUN_FLAG_RUN_OVER | BK_RUN_FLAG_DONT_BLOCK_RUN_ONCE) ||
	BK_FLAG_ISSET(flags, BK_RUN_ONCE_FLAG_DONT_BLOCK))
    {
      selectarg = &tzero;
    }


  /*
   *      E V E N T   Q U E U E
   */
  if (!curtime)
    timenow = tzero;
  if ((ret = bk_run_checkeventq(B, run, &timenow, &deltaevent, &event_cnt)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Event queue check failed\n");
    goto error;
  }
  if (timenow.tv_sec || timenow.tv_usec)	// checkeventq got time
    curtime = &timenow;

  // don't block in select if we handled any events; we've run "once"
  if (event_cnt)
    selectarg = &tzero;

  // Figure out the select argument
  if (!selectarg && ret == 0)
  {
    // there are no queued events, so either use deltapoll or ...
    if (use_deltapoll)
      selectarg = &deltapoll;
    else					// ... wait forever
      selectarg = NULL;
  }
  else if (!selectarg)
  {
    // use shorter delta of event and poll if there is a deltapoll or ...
    if (use_deltapoll && (BK_TV_CMP(&deltapoll, &deltaevent) < 0))
      selectarg = &deltapoll;
    else					// ... just use deltaevent
      selectarg = &deltaevent;
  }

  /*
   * If some caller has registered an idle task then ensure we do not block.
   */
  if (BK_FLAG_ISSET(run->br_flags, BK_RUN_FLAG_HAVE_IDLE))
  {
    selectarg = &tzero;
  }

  if (bk_debug_and(B, 4))
  {
    int f;
    int s = getdtablesize();
    char *p;
    struct bk_memx *bm=bk_memx_create(B, 1, 128, 128, 0);
    char scratch[1024];

    if (!bm)
      goto out;

    snprintf(scratch,1024, "Readset: ");
    if (!(p = bk_memx_get(B, bm, strlen(scratch), NULL, BK_MEMX_GETNEW)))
    {
      goto out;
    }
    memcpy(p,scratch,strlen(scratch));
    for (f = 0; f < s; f++)
    {
      if (FD_ISSET(f, &run->br_readset))
      {
	snprintf(scratch,1024, "%d ", f);
	if (!(p=bk_memx_get(B, bm, strlen(scratch), NULL, BK_MEMX_GETNEW)))
	{
	  goto out;
	}
	memcpy(p,scratch,strlen(scratch));
      }
    }

    snprintf(scratch,1024, " Writeset: ");
    if (!(p = bk_memx_get(B, bm, strlen(scratch), NULL, BK_MEMX_GETNEW)))
    {
      goto out;
    }
    memcpy(p,scratch,strlen(scratch));
    for (f = 0; f < s; f++)
    {
      if (FD_ISSET(f, &run->br_writeset))
      {
	snprintf(scratch,1024, "%d ", f);
	if (!(p = bk_memx_get(B, bm, strlen(scratch), NULL, BK_MEMX_GETNEW)))
	{
	  goto out;
	}
	memcpy(p,scratch,strlen(scratch));
      }
    }
    if (!(p=bk_memx_get(B, bm, 2, NULL, BK_MEMX_GETNEW)))
    {
      goto out;
    }

    memcpy(p,"\n",2);
    bk_debug_printf(B,"%s", (char *)bk_memx_get(B,bm,0,NULL,0));
  out:
    if (bm) bk_memx_destroy(B,bm,0);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();
  islocked = 1;

  run->br_selectcount++;
#endif /* BK_USING_PTHREADS */

  isinselect = 1;

  readset = run->br_readset;
  writeset = run->br_writeset;
  xcptset = run->br_xcptset;



  BK_RUN_ONCE_ABORT_CHECK();

  // check that we have anything to select for (we may not)
  if (selectarg == &tzero
      && !memcmp(&readset, &zeroset, sizeof(zeroset))
      && !memcmp(&writeset, &zeroset, sizeof(zeroset))
      && !memcmp(&xcptset, &zeroset, sizeof(zeroset)))
  {
    ret = 0;					// zero timeout, no fds

#ifdef BK_USING_PTHREADS
    run->br_selectcount--;
#endif /* BK_USING_PTHREADS */
    isinselect = 0;
  }
  else
  {
    // use a copy of timeout, since Linux (only) will update time left
    if (selectarg)
      timeout = *selectarg;

    /*
     * <BUG>Other thread could insert something into select/eventq etc
     * queues which would cause select to terminate--instead it will
     * be hanging out...</BUG>
     *
     * Note even if we stored away the thread_id and asked other
     * people modifying the foosets to signal this thread, there is
     * still a race condition between the time we release the lock and
     * the time we sufficiently enter select for a signal to interrupt
     * the system call--this all assumes of course that pthread
     * signals behave the same as normal system signals.
     */

#ifdef BK_USING_PTHREADS
    bk_debug_printf_and(B, 64, "Entering select %d, %d, %d\n", run->br_selectn, run->br_selectcount, getpid());

    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
      abort();
    islocked = 0;
#endif /* BK_USING_PTHREADS */

    /*
     * <KLUDGE>Linux does not have a working pselect(2) system call as
     * far as I can see.  glibc emulates it like we do below.  Right
     * now this is a race.  Probably not a HUGE race, but a race none
     * the less.  We could improve this by:
     *
     * Using longjmp out of the signal handler.  Yuk, and probably difficult to
     * coordinate.
     *
     * Writing a byte to the bk_run_select_changed socket to cause
     * select to exit (which preassumes that select is working, see
     * sysd bug 3434)</KLUDGE>
     */
    // Wait for I/O, signal, or timeout
    if (wantsignals)
      sigprocmask(SIG_UNBLOCK, &run->br_runsignals, NULL);

    if (!br_beensignaled)
      ret = select(run->br_selectn, &readset, &writeset, &xcptset, selectarg ? &timeout : NULL);

    if (wantsignals)
      sigprocmask(SIG_BLOCK, &run->br_runsignals, NULL);

#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
      abort();
    islocked = 1;
    run->br_selectcount--;
    isinselect = 0;
    bk_debug_printf_and(B, 64, "Select has returned with %d/%d/%d %d\n", ret, errno, run->br_selectcount, getpid());
#endif /* BK_USING_PTHREADS */

    if (ret < 0)
    {
      // <TODO>Handle badfd, getfdflags--withdraw and notify handler</TODO>
      if (errno != EINTR
#ifdef ERESTARTNOHAND
	  && errno != ERESTARTNOHAND
#endif /* ERESTARTNOHAND */
	  )
      {
	bk_error_printf(B, BK_ERR_ERR, "Select failed: %s\n", strerror(errno));
	goto error;
      }
    }

    BK_RUN_ONCE_ABORT_CHECK();

    /*
     * Time may have changed drastically during select, but it probably didn't.
     * Check selectarg and saved timeout to see if selectarg was updated
     * (Linux) or that we waited less than a second.  If selectarg was updated,
     * we can adjust without making a system call; otherwise, for waits of less
     * than a second (usually zero seconds), we accept the slight loss of
     * granularity.
     */
    if (curtime && selectarg && BK_TV_CMP(selectarg, &timeout))
    {
      // Linux occasionally rounds the timeleft upward; treat as zero elapsed
      if (BK_TV_CMP(&timeout, selectarg) < 0)
      {
	BK_TV_ADD(curtime, curtime, selectarg);
	BK_TV_SUB(curtime, curtime, &timeout);
      }
    }
    else if (!selectarg || selectarg->tv_sec)
    {
      // too much time may have gone by; we don't know any more
      curtime = NULL;
    }
  }

  // Are there any I/O events pending?
  if (ret > 0 )
  {
    for (x=0; ret > 0 && x < run->br_selectn; x++)
    {
      int type = 0;

#ifdef BK_USING_PTHREADS
      if (!islocked && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
	abort();
      islocked = 1;
#endif /* BK_USING_PTHREADS */
      if (FD_ISSET(x, &readset)
#ifdef BK_USING_PTHREADS
	  && FD_ISSET(x, &run->br_readset)
#endif /* BK_USING_PTHREADS */
	  )
	type |= BK_RUN_READREADY;
      if (FD_ISSET(x, &writeset)
#ifdef BK_USING_PTHREADS
	  && FD_ISSET(x, &run->br_writeset)
#endif /* BK_USING_PTHREADS */
	  )
	type |= BK_RUN_WRITEREADY;
      if (FD_ISSET(x, &xcptset)
#ifdef BK_USING_PTHREADS
	  && FD_ISSET(x, &run->br_xcptset)
#endif /* BK_USING_PTHREADS */
	  )
	type |= BK_RUN_XCPTREADY;

      if (type)
      {
	bk_debug_printf_and(B,1,"Activity detected on %d: type: %d\n", x, type);
      }

      if (type)
      {
	struct bk_run_fdassoc *curfd;

	ret--;

	if (!(curfd = fdassoc_search(run->br_fdassoc, &x)))
	{
	  bk_error_printf(B, BK_ERR_WARN, "Could not find fd %d in association, yet type is %x\n",x,type);
	  continue;
	}
#ifdef BK_USING_PTHREADS
	if (BK_GENERAL_FLAG_ISTHREADON(B))
	{
	  if (curfd->brf_userid)
	  {
	    bk_debug_printf_and(B, 1, "Cannot call fdassoc function %p, locked by %d\n", curfd->brf_handler, (int)curfd->brf_userid);
	    continue;				// Someone already calling this function
	  }

	  curfd->brf_userid = pthread_self();	// Who is has a soft-lock on this structure
	}

	if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
	  abort();
	islocked = 0;
#endif /* BK_USING_PTHREADS */

	if (!curtime && BK_FLAG_ISSET(curfd->brf_flags, BK_RUN_HANDLE_TIME))
	{
	  gettimeofday(&timenow, NULL);
	  curtime = &timenow;
	}
	bk_run_runfd(B, run, x, type, curfd->brf_handler, curfd->brf_opaque, curtime, curfd->brf_flags);

#ifdef BK_USING_PTHREADS
	if (!islocked && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
	  abort();
	islocked = 1;

	// Look again, we may have been deleted in the interim
	if (BK_GENERAL_FLAG_ISTHREADON(B))
	{
	  struct bk_run_fdassoc *oldfd = curfd;	// May be COPY_DANGLING

	  if (curfd = fdassoc_search(run->br_fdassoc, &x))
	  {
	    int isme = pthread_equal(curfd->brf_userid, pthread_self());

	    if (!isme)
	    {
	      bk_error_printf(B, BK_ERR_WARN, "UserID does not match (%d != %d), fdassoc %p, fd %d\n", (int)curfd, (int)pthread_self(), oldfd, x);
	    }

	    BK_ZERO(&curfd->brf_userid); // Here's hoping zero is reserved
	    pthread_cond_signal(&curfd->brf_cond);
	  }
	  else
	  {
	    bk_debug_printf_and(B, 64, "Could not clear brf_userid, fdassoc %p, fd %d, appears to have disappeared (may be normal)\n", oldfd, x);
	  }
	}
#endif /* BK_USING_PTHREADS */
      }

      BK_RUN_ONCE_ABORT_CHECK();
    }
#ifdef BK_USING_PTHREADS
    if (islocked && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
      abort();
    islocked = 0;
#endif /* BK_USING_PTHREADS */
  }
  else
  {
    // no I/O has occurred; check for idle task, but not if we had any events
    if (!event_cnt)
    {
      struct bk_run_func *brfn;
      dict_iter iter;

#ifdef BK_USING_PTHREADS
      if (!islocked && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
	abort();
      islocked = 1;
#endif /* BK_USING_PTHREADS */

      iter = brfl_iterate(run->br_idle_funcs, DICT_FROM_START);
      while (brfn = brfl_nextobj(run->br_idle_funcs, iter))
      {
#ifdef BK_USING_PTHREADS
	if (BK_GENERAL_FLAG_ISTHREADON(B))
	{
	  if (brfn->brfn_userid)
	  {
	    bk_debug_printf_and(B, 1, "Cannot call idle function %p, locked by %d\n", brfn->brfn_fun, (int)brfn->brfn_userid);
	    continue;				// Someone already calling this function
	  }

	  brfn->brfn_userid = pthread_self();	// Who is has a soft-lock on this structure
	}

	if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
	  abort();
	islocked = 0;
#endif /* BK_USING_PTHREADS */

	if (!curtime && BK_FLAG_ISSET(brfn->brfn_flags, BK_RUN_HANDLE_TIME))
	{
	  gettimeofday(&timenow, NULL);
	  curtime = &timenow;
	}
	if ((*brfn->brfn_fun)(B, run, brfn->brfn_opaque, curtime, NULL, 0)<0)
	{
	  bk_error_printf(B, BK_ERR_WARN, "Idle callback failed\n");
	  haderror = 1;
	}

#ifdef BK_USING_PTHREADS
	// Look again, we may have been deleted in the interim
	if (BK_GENERAL_FLAG_ISTHREADON(B) && (brfn = brfl_search(run->br_ondemand_funcs, brfn)))
	{
	  BK_ZERO(&brfn->brfn_userid); // Here's hoping zero is reserved
	  pthread_cond_signal(&brfn->brfn_cond);
	}
#endif /* BK_USING_PTHREADS */

	if (haderror)
	  goto error;

#ifdef BK_USING_PTHREADS
	if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
	  abort();
	islocked = 1;
#endif /* BK_USING_PTHREADS */
      }
      brfl_iterate_done(run->br_idle_funcs, iter);
#ifdef BK_USING_PTHREADS
      if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
	abort();
      islocked = 0;
#endif /* BK_USING_PTHREADS */
    }
  }

#undef BK_RUN_ONCE_ABORT_CHECK

  // check for synchronous signals to handle
 beensignaled:
#ifdef BK_USING_PTHREADS
  if (!islocked && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();
  islocked = 1;

  if (isinselect)
  {
    run->br_selectcount--;
    isinselect = 0;
  }

  if (islocked && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
  islocked = 0;
#endif /* BK_USING_PTHREADS */

  while (br_beensignaled)
  {
    br_beensignaled = 0;			// race condition--needs lock

    for (x=0; x<NSIG; x++)
    {

      while (run->br_signums[x] > 0)
      {
	run->br_signums[x]--;

	(*run->br_handlerlist[x].brs_handler)(B, run, x, run->br_handlerlist[x].brs_opaque);
      }
    }
  }

 abort_run_once:
#ifdef BK_USING_PTHREADS
  if (!islocked && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();
  islocked = 1;

  if (isinselect)
  {
    run->br_selectcount--;
    isinselect = 0;
  }
#endif /* BK_USING_PTHREADS */
  BK_FLAG_SET(run->br_flags, BK_RUN_FLAG_ABORT_RUN_ONCE);
  BK_FLAG_CLEAR(run->br_flags, BK_RUN_FLAG_DONT_BLOCK_RUN_ONCE);
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
  islocked = 0;
#endif /* BK_USING_PTHREADS */
  BK_RETURN(B, 0);

 error:
#ifdef BK_USING_PTHREADS
  if (!islocked && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();
  islocked = 1;
#endif /* BK_USING_PTHREADS */
  BK_FLAG_SET(run->br_flags, BK_RUN_FLAG_ABORT_RUN_ONCE);
  BK_FLAG_CLEAR(run->br_flags, BK_RUN_FLAG_DONT_BLOCK_RUN_ONCE);
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
  islocked = 0;
#endif /* BK_USING_PTHREADS */
  BK_RETURN(B,-1);
}



/**
 * Create a @a bk_run_fdassoc.
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new <i>brf</i> on success.
 */
static struct bk_run_fdassoc *
brf_create(bk_s B, bk_flags flags)
{
  struct bk_run_fdassoc *brf = NULL;
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!(BK_CALLOC(brf)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allcoate fd association struct: %s\n", strerror(errno));
    goto error;
  }

  BK_RETURN(B, brf);  

 error:
  if (brf)
    brf_destroy(B, brf);
  BK_RETURN(B, NULL);  
}



/**
 * Destroy a @a bk_run_fdassoc
 *
 *	@param B BAKA thread/global state.
 *	@param brf The @a brf to nuke
 */
static void
brf_destroy(bk_s B, struct bk_run_fdassoc *brf)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!brf)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

#ifdef BK_USING_PTHREADS
  if (pthread_cond_destroy(&brf->brf_cond))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not destroy fd association condition\n");
  }
#endif /* BK_USING_PTHREADS */

  free(brf);

  BK_VRETURN(B);  
}



/**
 * Specify the handler to take care of all fd activity.
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA thread/global state
 *	@param run The baka run environment state
 *	@param fd The file descriptor which should be monitored
 *	@param handler The function to call when activity is monitored on the fd
 *	@param opaque Opaque data to for handler
 *	@param wanttypes What types of activities you want notification on
 *	@param flags Flags for the Future.
 *	@return <i><0</i> on call failure, or other error.
 *	@return <br><i>0</i> on success.
 */
int bk_run_handle(bk_s B, struct bk_run *run, int fd, bk_fd_handler_t handler, void *opaque, u_int wanttypes, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_run_fdassoc *brf = NULL;

  if (!run || !handler)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!(brf = brf_create(B, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create fd association structure\n");
    goto error;
  }

  brf->brf_fd = fd;
  brf->brf_handler = handler;
  brf->brf_opaque = opaque;
  brf->brf_flags = flags;

#ifdef BK_USING_PTHREADS
  BK_ZERO(&brf->brf_userid);

  if (pthread_cond_init(&brf->brf_cond, NULL) < 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_SIMPLE_LOCK(B, &run->br_lock);

  if (fdassoc_insert(run->br_fdassoc, brf) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not associate fd %d: %s\n",fd,fdassoc_error_reason(run->br_fdassoc, NULL));
    goto error;
  }

  run->br_selectn = MAX(run->br_selectn,fd+1);

  BK_SIMPLE_UNLOCK(B, &run->br_lock);

  bk_run_setpref(B, run, fd, wanttypes, BK_RUN_WANTREAD|BK_RUN_WANTWRITE|BK_RUN_WANTXCPT, 0);

  bk_debug_printf_and(B,1,"Added fd: %d -- selectn now: %d\n", fd, run->br_selectn);

  BK_RETURN(B, 0);

 error:
  BK_SIMPLE_UNLOCK(B, &run->br_lock);

  if (brf)
    brf_destroy(B, brf);

  BK_RETURN(B, -1);
}



/**
 * Specify that we no longer wish bk_run to take care of file descriptors.
 * We do not close the fd.
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA thread/global state
 *	@param run The baka run environment state
 *	@param fd The file descriptor which should be monitored
 *	@param flags Flags for the Future.
 *	@return <i>-1</i> on call failure, or other error.
 *	@return <br><i>0</i> on success.
 */
int bk_run_close(bk_s B, struct bk_run *run, int fd, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_run_fdassoc *brf;
  struct timeval curtime;
  int ret = 0;

  if (!run)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  BK_SIMPLE_LOCK(B, &run->br_lock);
#ifdef BK_USING_PTHREADS
 again:
#endif /* BK_USING_PTHREADS */
  if (!(brf = fdassoc_search(run->br_fdassoc, &fd)))
  {
    bk_debug_printf_and(B,4,"Double close protection kicked in\n");
    bk_error_printf(B, BK_ERR_WARN, "Could not find fd %d in association while attempting to delete\n",fd);
    ret = 0;
    goto unlockexit;
  }

#ifdef BK_USING_PTHREADS
  // See if someone (other than myself) is currently using this function
  if (BK_GENERAL_FLAG_ISTHREADON(B) && brf->brf_userid && !pthread_equal(brf->brf_userid, pthread_self()))
  {
    // Wait for the current user to finish
    pthread_cond_wait(&brf->brf_cond, &run->br_lock);
    goto again;
  }
#endif /* BK_USING_PTHREADS */

  // Get rid of event in list, which will also prevent double deletion
  if (fdassoc_delete(run->br_fdassoc, brf) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not delete descriptor %d from fdassoc list: %s\n", fd, fdassoc_error_reason(run->br_fdassoc, NULL));
    ret = -1;
    goto unlockexit;
  }

  BK_SIMPLE_UNLOCK(B, &run->br_lock);

  // Optionally tell user handler that he will never be called again.
  gettimeofday(&curtime, NULL);
  bk_run_runfd(B, run, fd, BK_RUN_CLOSE, brf->brf_handler, brf->brf_opaque, &curtime, brf->brf_flags);

  brf_destroy(B, brf);

  BK_SIMPLE_LOCK(B, &run->br_lock);

  FD_CLR(fd, &run->br_readset);
  FD_CLR(fd, &run->br_writeset);
  FD_CLR(fd, &run->br_xcptset);

  // Check to see if we need to find out a new value for selectn
  if (run->br_selectn == fd+1)
  {
    run->br_selectn = 0;
    for(brf=fdassoc_minimum(run->br_fdassoc);brf;brf = fdassoc_successor(run->br_fdassoc, brf))
    {
      run->br_selectn = MAX(run->br_selectn,brf->brf_fd + 1);
    }
  }

  bk_debug_printf_and(B,1,"Closed fd: %d -- selectn now: %d\n", fd, run->br_selectn);

 unlockexit:
  BK_SIMPLE_UNLOCK(B, &run->br_lock);

  BK_RETURN(B, ret);
}



/**
 * Find out what read/write/xcpt desires are current for a given fd.
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA thread/global state
 *	@param run The baka run environment state
 *	@param fd The file descriptor which should be monitored
 *	@param flags Flags for the Future.
 *	@return <i>(u_int)-1</i> on error
 *	@return <br><i>BK_RUN_WANT* bitmap</i> on success
 */
u_int bk_run_getpref(bk_s B, struct bk_run *run, int fd, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  u_int type = 0;

  if (!run || fd < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  BK_SIMPLE_LOCK(B, &run->br_lock);

  if (FD_ISSET(fd, &run->br_readset))
    type |= BK_RUN_WANTREAD;
  if (FD_ISSET(fd, &run->br_writeset))
    type |= BK_RUN_WANTWRITE;
  if (FD_ISSET(fd, &run->br_xcptset))
    type |= BK_RUN_WANTXCPT;

  BK_SIMPLE_UNLOCK(B, &run->br_lock);

  BK_RETURN(B, type);
}



/**
 * Find out what read/write/xcpt desires are current for a given fd.
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA thread/global state
 *	@param run The baka run environment state
 *	@param fd The file descriptor which should be monitored
 *	@param whattypes What preferences the user wishes to get notification on
 *	@param wantmask What preferences the user is attempting to set
 *	@param flags Flags for the Future.
 *	@return <i>-1</i> on error
 *	@return <br><i>0</i> on success
 */
int bk_run_setpref(bk_s B, struct bk_run *run, int fd, u_int wanttypes, u_int wantmask, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  u_int oldtype = 0;
  u_int origtype = 0;

  if (!run || fd < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  BK_SIMPLE_LOCK(B, &run->br_lock);

  // Do we only want to modify one (or two) flags?
  if (wantmask)
  {
    if (FD_ISSET(fd, &run->br_readset))
      oldtype |= BK_RUN_WANTREAD;
    if (FD_ISSET(fd, &run->br_writeset))
      oldtype |= BK_RUN_WANTWRITE;
    if (FD_ISSET(fd, &run->br_xcptset))
      oldtype |= BK_RUN_WANTXCPT;

    oldtype &= ~wantmask;
  }
  origtype = oldtype;
  oldtype |= wanttypes;


  FD_CLR(fd, &run->br_readset);
  FD_CLR(fd, &run->br_writeset);
  FD_CLR(fd, &run->br_xcptset);

  if (BK_FLAG_ISSET(oldtype, BK_RUN_WANTREAD))
    FD_SET(fd, &run->br_readset);
  if (BK_FLAG_ISSET(oldtype, BK_RUN_WANTWRITE))
    FD_SET(fd, &run->br_writeset);
  if (BK_FLAG_ISSET(oldtype, BK_RUN_WANTXCPT))
    FD_SET(fd, &run->br_xcptset);

  if (oldtype != origtype)
  {
    bk_debug_printf_and(B, 64, "Modified select set for fd %d, now %ld/%ld/%ld\n", fd, (long)FD_ISSET(fd, &run->br_readset), (long)FD_ISSET(fd, &run->br_writeset), (long)FD_ISSET(fd, &run->br_xcptset));

#ifdef BK_USING_PTHREADS
    if (run->br_selectcount)
    {
      bk_debug_printf_and(B, 64, "Asking for runrun rescheduling\n");
      bk_run_select_changed(B, run, BK_RUN_GLOBAL_FLAG_ISLOCKED);
    }
#endif /* BK_USING_PTHREADS */
  }

  BK_SIMPLE_UNLOCK(B, &run->br_lock);

  BK_RETURN(B, 0);
}



/**
 * Event priority queue comparison routines--CLC PQ thing
 *
 *	@param a First event queue event
 *	@param b Second event queue event
 *	@return <i>sort order</i>
 */
static int bk_run_event_comparator(struct br_equeue *a, struct br_equeue *b)
{
  if (BK_TV_CMP(&a->bre_when,&b->bre_when) <= 0)
    return(1);
  return(0);
}



/**
 * Signal handler for synchronous signal--glue between OS and libbk
 *
 * <TODO>Worry about locking!</TODO>
 * <TODO>Autoconf for multiple signal prototypes</TODO>
 *
 *	@param signum signal number that was received
 */
void bk_run_signal_ihandler(int signum)
{
  if (br_signums)
  {
    br_beensignaled = 1;
    (*br_signums)[signum]++;
  }
}



/**
 * The internal event which implements cron functionality on top of
 * the normal once-only event queue methodology
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state
 *	@param run Run environment handle
 *	@param opaque Event cron structure handle
 *	@param starttime The time when this global-event queue run was started
 *	@param flags BK_RUN_DESTROY when this event is being called for the last time.
 */
static void bk_run_event_cron(bk_s B, struct bk_run *run, void *opaque, const struct timeval *starttime, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct br_equeuecron *brec = opaque;
  struct timeval addtv;

  if (!run || !opaque)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  addtv.tv_sec = brec->brec_interval / 1000;
  addtv.tv_usec = brec->brec_interval % 1000;
  BK_TV_ADD(&addtv,&addtv,starttime);

  if (BK_FLAG_ISCLEAR(flags,BK_RUN_DESTROY))
    bk_run_enqueue(B, run, addtv, bk_run_event_cron, brec, ((void **)&brec->brec_equeue), brec->brec_flags);

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B))
  {
    if (brec->brec_userid)
    {
      bk_debug_printf_and(B, 1, "Cannot call cron %p, locked by %d\n", brec->brec_event, (int)brec->brec_userid);
      BK_VRETURN(B);			// Someone already calling this function
    }

    brec->brec_userid = pthread_self();	// Who is has a soft-lock on this structure
  }
#endif /* BK_USING_PTHREADS */

  (*brec->brec_event)(B, run, brec->brec_opaque, starttime, flags);
#ifdef BK_USING_PTHREADS
  bk_debug_printf_and(B, 16, "Function %p (locked by %d) returned\n", brec->brec_event, (int)brec->brec_userid);
#else /* BK_USING_PTHREADS */
  bk_debug_printf_and(B, 16, "Function %p (locked but threads not defined) returned\n", brec->brec_event);
#endif /* BK_USING_PTHREADS */

#ifdef BK_USING_PTHREADS
  // Special handling due to partial deletion
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();

  if (BK_GENERAL_FLAG_ISTHREADON(B) && BK_FLAG_ISSET(brec->brec_flags, BK_RUN_THREADREADY))
  {
    if (!brec->brec_userid)
    {
      // Someone (partially) deleted us...finish up
      pthread_cond_broadcast(&brec->brec_cond);
      pq_delete(run->br_equeue, brec->brec_equeue);
      free(brec->brec_equeue);
      pthread_cond_destroy(&brec->brec_cond);
      free(brec);
    }
    else
    {
      bk_debug_printf_and(B, 16, "Function %p (locked by %d) zapping lock\n", brec->brec_event, (int)brec->brec_userid);
      BK_ZERO(&brec->brec_userid);		// Here's hoping zero is reserved
      bk_debug_printf_and(B, 16, "Function %p (locked by %d) zapped lock\n", brec->brec_event, (int)brec->brec_userid);
    }
  }

  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */


  BK_VRETURN(B);
}



/**
 * Execute an event queue job, possibly creating a new thread
 *
 * THREADS: MT-SAFE
 *
 * @param B BAKA Thread/global state
 * @param run Run environment handle
 * @param fun Function to call
 * @param opaque Opaque data for function
 * @param starttime Start time
 * @param eventflags DESTROY and other such stuff
 * @param flags BK_RUN_THREADREADY to fork a new thread
 */
static void bk_run_runevent(bk_s B, struct bk_run *run, void (*fun)(bk_s B, struct bk_run *run, void *opaque, const struct timeval *starttime, bk_flags flags), void *opaque, struct timeval *starttime, bk_flags eventflags, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!run || !fun)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISSET(flags, BK_RUN_THREADREADY) && BK_GENERAL_FLAG_ISTHREADREADY(B) && !eventflags)
  {
    struct br_runevent *brre;

    if (!BK_MALLOC(brre))
    {
      bk_error_printf(B, BK_ERR_ERR, "Cannot malloc space for runevent thread: %s\n", strerror(errno));
      BK_VRETURN(B);
    }

    brre->brre_run = run;
    brre->brre_fun = fun;
    brre->brre_opaque = opaque;
    brre->brre_starttime = starttime;
    brre->brre_flags = eventflags;

    if (!bk_general_thread_create(B, "bk_run.eventq", bk_run_runevent_thread, brre, 0))
    {
      bk_error_printf(B, BK_ERR_ERR, "Cannot fire off thread for runevent\n");
      free(brre);
      BK_VRETURN(B);
    }
  }
  else
#endif /* BK_USING_PTHREADS */
    (*fun)(B, run, opaque, starttime, eventflags);

  BK_VRETURN(B);
}



#ifdef BK_USING_PTHREADS
/**
 * Function as thread to call event queue function
 *
 * @param B BAKA Thread/global state
 * @param opaque Opaque data
 */
static void *bk_run_runevent_thread(bk_s B, void *opaque)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct br_runevent *brre = opaque;

  if (!brre)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  (*brre->brre_fun)(B, brre->brre_run, brre->brre_opaque, brre->brre_starttime, brre->brre_flags);
  free(brre);

  BK_RETURN(B, NULL);
}
#endif /* BK_USING_PTHREADS */



/**
 * Execute an fd job, possibly creating a new thread
 *
 * THREADS: MT-SAFE
 *
 * @param B BAKA Thread/global state
 * @param run Run environment handle
 * @param fd fd to process
 * @param gottypes Type of activity
 * @param fun Function to call
 * @param opaque Opaque data for function
 * @param starttime Start time
 * @param flags BK_RUN_THREADREADY to fork a new thread
 */
static void bk_run_runfd(bk_s B, struct bk_run *run, int fd, u_int gottypes, bk_fd_handler_t fun, void *opaque, struct timeval *starttime, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!run || !fun)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISSET(flags, BK_RUN_THREADREADY) && BK_GENERAL_FLAG_ISTHREADREADY(B))
  {
    struct br_runfd *brrf;

    if (!BK_MALLOC(brrf))
    {
      bk_error_printf(B, BK_ERR_ERR, "Cannot malloc space for runfd thread: %s\n", strerror(errno));
      BK_VRETURN(B);
    }

    brrf->brrf_run = run;
    brrf->brrf_fd = fd;
    brrf->brrf_gottypes = gottypes;
    brrf->brrf_fun = fun;
    brrf->brrf_opaque = opaque;
    brrf->brrf_starttime = starttime;

    if (!bk_general_thread_create(B, "bk_run.fd", bk_run_runfd_thread, brrf, 0))
    {
      bk_error_printf(B, BK_ERR_ERR, "Cannot fire off thread for runfd\n");
      free(brrf);
      BK_VRETURN(B);
    }
  }
  else
#endif /* BK_USING_PTHREADS */
    (*fun)(B, run, fd, gottypes, opaque, starttime);

  BK_VRETURN(B);
}



#ifdef BK_USING_PTHREADS
/**
 * Function as thread to call fd function
 *
 * @param B BAKA Thread/global state
 * @param opaque Opaque data
 */
static void *bk_run_runfd_thread(bk_s B, void *opaque)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct br_runfd *brrf = opaque;

  if (!brrf)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  (*brrf->brrf_fun)(B, brrf->brrf_run, brrf->brrf_fd, brrf->brrf_gottypes, brrf->brrf_opaque, brrf->brrf_starttime);

  free(brrf);
  BK_RETURN(B, NULL);
}
#endif /* BK_USING_PTHREADS */



/**
 * Execute all pending event queue events.
 *
 * In addition to executing the events, returns indication of whether more
 * events are still scheduled, and if so, copies out time to next event via
 * delta.  If any events were processed, it updates the starttime to account
 * for any delay that caused.
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA Thread/global state
 *	@param run Run environment handle
 *	@param starttime The time when this global-event queue run was started
 *	(may be epoch, in which case this will call gettimeofday if needed)
 *	@param delta The time to the next event
 *	@return <i>-1</i> on call failure
 *	@return <br><i>0</i> if there is no next event
 *	@return <br><i>1</i> if there is a next event
 *	@return <br>copy-out <i>delta</i>, iff there is a next event
 *	@return <br>copy-out <i>starttime</i>, iff events were processed
 */
static int bk_run_checkeventq(bk_s B, struct bk_run *run, struct timeval *starttime, struct timeval *delta, u_int *event_cntp)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct br_equeue *top;
  int event_cnt = 0;
  int timeset;

  if (!run || !starttime || !delta)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, -1);
  }

  timeset = starttime->tv_sec || starttime->tv_usec;

  if (event_cntp)
    *event_cntp = 0;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */
  while (top = pq_head(run->br_equeue))
  {
    if (!timeset)				// can't defer any longer
    {
      gettimeofday(starttime, NULL);
      timeset = 1;
    }

    if (BK_TV_CMP(&top->bre_when, starttime) > 0)
      break;

    top = pq_extract_head(run->br_equeue);

#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */

    bk_run_runevent(B, run, top->bre_event, top->bre_opaque, starttime, 0, top->bre_flags);
    free(top);
    event_cnt++;

#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */
  }
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (event_cntp)
    *event_cntp = event_cnt;

  if (!top)
    BK_RETURN(B,0);

  // iff we handled any events, get (and update) actual time for more accuracy
  if (event_cnt)
    gettimeofday(starttime, NULL);

  BK_TV_SUB(delta, &top->bre_when, starttime);
  if (delta->tv_sec < 0 || delta->tv_usec < 0)
  {
    delta->tv_sec = 0;
    delta->tv_usec = 0;
  }

  BK_RETURN(B, 1);
}



/**
 * Add a function for bk_run polling.
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA thread/global state
 *	@param run bk_run structure pointer
 *	@param fun function to call
 *	@param opaque data for fun call
 *	@param handle handle to use to remove this fun
 *	@return <i>-1</i> on failure
 *	@return <br><i>0</i> on success
 */
int
bk_run_poll_add(bk_s B, struct bk_run *run, int (*fun)(bk_s B, struct bk_run *run, void *opaque, const struct timeval *starttime, struct timeval *delta, bk_flags flags), void *opaque, void **handle, int flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_run_func *brfn=NULL;

  if (!fun)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (handle)
    *handle = NULL;

  if (!(brfn = brfn_alloc(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate brf: %s\n", strerror(errno));
    BK_RETURN(B, -1);
  }

  brfn->brfn_fun = fun;
  brfn->brfn_opaque = opaque;
  brfn->brfn_flags = flags;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (brfl_insert(run->br_poll_funcs, brfn) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert brf in dll\n");
    goto error;
  }

  brfn->brfn_backptr = run->br_poll_funcs;
  BK_FLAG_SET(run->br_flags, BK_RUN_FLAG_NEED_POLL);

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (handle)
    *handle=brfn->brfn_key;
  BK_RETURN(B,0);

 error:
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (brfn)
    brfn_destroy(B,brfn);
  BK_RETURN(B,-1);
}



/**
 * Remove a function from the bk_run poll list
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA thread/global state
 *	@param run bk_run structure
 *	@param handle polling function to remove
 *	@ret <i>-1</i> on call failure<br>
 *	@ret <i>0</i> if object (possibly already) was removed
 */
int
bk_run_poll_remove(bk_s B, struct bk_run *run, void *handle)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_run_func *brf=NULL;

  if (!run || !handle)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();
 again:
#endif /* BK_USING_PTHREADS */

  if (!(brf=brfl_search(run->br_poll_funcs, handle)))
  {
    bk_error_printf(B, BK_ERR_WARN, "Handle %p not found in delete\n", handle);
    goto notfound;
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B))
  {
    // See if someone (other than myself) is currently using this function
    if (brf->brfn_userid && !pthread_equal(brf->brfn_userid, pthread_self()))
    {
      // Wait for the current user to finish
      pthread_cond_wait(&brf->brfn_cond, &run->br_lock);
      goto again;
    }
  }
#endif /* BK_USING_PTHREADS */

  brfn_destroy(B, brf);

  if (!brfl_minimum(run->br_poll_funcs))
  {
    BK_FLAG_CLEAR(run->br_flags,BK_RUN_FLAG_NEED_POLL);
  }

 notfound:
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B, 0);
}



/**
 * Add a function to bk_run idle task.
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA thread/global state
 *	@param run bk_run structure pointer
 *	@param fun function to call
 *	@param opaque data for fun call
 *	@param handle handle to use to remove this fun
 *	@return <i>-1</i> on failure
 *	@return <br><i>0</i> on success
 *
 *	<TODO>consolidate with poll_add</TODO>
 */
int
bk_run_idle_add(bk_s B, struct bk_run *run, int (*fun)(bk_s B, struct bk_run *run, void *opaque, const struct timeval *starttime, struct timeval *delta, bk_flags flags), void *opaque, void **handle, int flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_run_func *brfn = NULL;

  if (!fun)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (handle)
    *handle = NULL;

  if (!(brfn = brfn_alloc(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate brf\n");
    BK_RETURN(B, -1);
  }

  brfn->brfn_fun = fun;
  brfn->brfn_opaque = opaque;
  brfn->brfn_flags = flags;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (brfl_insert(run->br_idle_funcs, brfn) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert brf in dll\n");
    goto error;
  }

  brfn->brfn_backptr = run->br_idle_funcs;
  BK_FLAG_SET(run->br_flags, BK_RUN_FLAG_HAVE_IDLE);

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (handle)
    *handle=brfn->brfn_key;
  BK_RETURN(B, 0);

 error:
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (brfn)
    brfn_destroy(B, brfn);
  BK_RETURN(B, -1);
}



/**
 * Remove a function from the bk_run idle list
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA thread/global state
 *	@param run bk_run structure
 *	@param handle idle function to remove
XXX - consolidate with poll_add
 */
int
bk_run_idle_remove(bk_s B, struct bk_run *run, void *handle)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_run_func *brfn=NULL;

  if (!run || !handle)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();
 again:
#endif /* BK_USING_PTHREADS */

  if (!(brfn=brfl_search(run->br_idle_funcs, handle)))
  {
    bk_error_printf(B, BK_ERR_WARN, "Handle %p not found in delete\n", handle);
    goto notfound;
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B))
  {
    // See if someone (other than myself) is currently using this function
    if (brfn->brfn_userid && !pthread_equal(brfn->brfn_userid, pthread_self()))
    {
      // Wait for the current user to finish
      pthread_cond_wait(&brfn->brfn_cond, &run->br_lock);
      goto again;
    }
  }
#endif /* BK_USING_PTHREADS */

  brfn_destroy(B, brfn);

  if (!brfl_minimum(run->br_idle_funcs))
  {
    BK_FLAG_CLEAR(run->br_flags,BK_RUN_FLAG_HAVE_IDLE);
  }

 notfound:
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B, 0);
}



/**
 * Allocate a brf
 *
 *	@param B BAKA Thread/global state
 *	@return <i>NULL</i> on allocation failure
 *	@return <i>function handle</i> on success
 */
static struct bk_run_func *
brfn_alloc(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_run_func *brfn;

  if (!BK_MALLOC(brfn))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate brf: %s\n", strerror(errno));
    BK_RETURN(B, NULL);
  }
  BK_ZERO(brfn);

  brfn->brfn_key=brfn;

#ifdef BK_USING_PTHREADS
  if (pthread_cond_init(&brfn->brfn_cond, NULL) < 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B,brfn);
}




/**
 * Destroy a brf
 *
 *	@param B BAKA Thread/global state
 *	@param brfn Baka run function handle
 */
static void
brfn_destroy(bk_s B, struct bk_run_func *brfn)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!brfn)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (brfn->brfn_backptr)
  {
    brfl_delete(brfn->brfn_backptr,brfn);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B))
    pthread_cond_broadcast(&brfn->brfn_cond);
#endif /* BK_USING_PTHREADS */

  free(brfn);
  BK_VRETURN(B);
}



/**
 * Add a function to bk_run on demand list
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA thread/global state
 *	@param run bk_run structure pointer
 *	@param fun function to call
 *	@param opaque data for fun call
 *	@param demand pointer to int which controls whether function will run
 *	@param handle handle to use to remove this fun
 *	@param flags everyone needs flags
 *	@return <i>-1</i> on failure
 *	@return <br><i>0</i> on success
 */
int
bk_run_on_demand_add(bk_s B, struct bk_run *run, bk_run_on_demand_f fun, void *opaque, volatile int *demand, void **handle, int flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_run_ondemand_func *brof = NULL;

  if (!fun)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (handle)
    *handle = NULL;

  if (!(brof = brof_alloc(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate brof\n");
    BK_RETURN(B, -1);
  }

  brof->brof_fun = fun;
  brof->brof_opaque = opaque;
  brof->brof_demand = demand;
  brof->brof_flags = flags;
  brof->brof_key = brof;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (brfl_insert(run->br_ondemand_funcs, brof) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert brof in dll\n");
    goto error;
  }

  brof->brof_backptr = run->br_ondemand_funcs;
  BK_FLAG_SET(run->br_flags, BK_RUN_FLAG_CHECK_DEMAND);

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (handle)
    *handle = brof->brof_key;
  BK_RETURN(B, 0);

 error:
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (brof)
    brof_destroy(B, brof);
  BK_RETURN(B, -1);
}



/**
 * Remove a function from the bk_run poll list
 *
 * THREADS: THREAD-REENTRANT (may block if some other thread is executing the function you are removing)
 *
 *	@param B BAKA Thread/global state
 *	@param run bk_run structure
 *	@param hand on demand function to remove
 */
int
bk_run_on_demand_remove(bk_s B, struct bk_run *run, void *handle)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_run_ondemand_func *brof=NULL;

  if (!run || !handle)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();
 again:
#endif /* BK_USING_PTHREADS */

  if (!(brof=brfl_search(run->br_ondemand_funcs, handle)))
  {
    bk_error_printf(B, BK_ERR_WARN, "Handle %p not found in delete\n", handle);
    goto notfound;
  }

#ifdef BK_USING_PTHREADS
  // See if someone (other than myself) is currently using this function
  if (BK_GENERAL_FLAG_ISTHREADON(B) && brof->brof_userid && !pthread_equal(brof->brof_userid, pthread_self()))
  {
    // Wait for the current user to finish
    pthread_cond_wait(&brof->brof_cond, &run->br_lock);
    goto again;
  }
#endif /* BK_USING_PTHREADS */

  brof_destroy(B, brof);

  if (!brfl_minimum(run->br_ondemand_funcs))
  {
    BK_FLAG_CLEAR(run->br_flags,BK_RUN_FLAG_CHECK_DEMAND);
  }

 notfound:
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B, 0);
}






/**
 * Allocate a brf -- on-demand function structure
 *
 *	@param B BAKA Thread/global state
 *	@return <i>NULL</i> on allocation failure
 *	@return <br><i>baka on-demand function</i> on success
 */
static struct bk_run_ondemand_func *
brof_alloc(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_run_ondemand_func *brof;

  if (!BK_MALLOC(brof))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate brf: %s\n", strerror(errno));
    BK_RETURN(B, NULL);
  }
  BK_ZERO(brof);

#ifdef BK_USING_PTHREADS
  if (pthread_cond_init(&brof->brof_cond, NULL) < 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B,brof);
}



/**
 * Destroy a brf -- on-demand function
 *
 *	@param B BAKA Thread/global state
 *	@param brof BAKA run on-demand function structure
 */
static void
brof_destroy(bk_s B, struct bk_run_ondemand_func *brof)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!brof)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (brof->brof_backptr)
  {
    brfl_delete(brof->brof_backptr,brof);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B))
    pthread_cond_broadcast(&brof->brof_cond);
#endif /* BK_USING_PTHREADS */

  free(brof);
  BK_VRETURN(B);
}



/**
 * Turn of the run environment
 *
 * @param B Baka thread/global environment
 * @param run Run Environment
 * @return <i>-1</i> on call failure
 * @return <br><i>0</i> on success
 */
int
bk_run_set_run_over(bk_s B, struct bk_run *run)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!run)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_FLAG_SET(run->br_flags, BK_RUN_FLAG_RUN_OVER);
  bk_run_select_changed(B, run, BK_RUN_GLOBAL_FLAG_ISLOCKED);

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B,0);
}



/**
 * Set the BK_RUN_FLAG_DONT_BLOCK_RUN_ONCE flag in the run structure. This
 * flag behaves <b>exactly</b> like the @a bk_run_once() flag
 * BK_RUN_ONCE_FLAG_DONT_BLOCK. The only difference is that, owing to the
 * fact that it is set in the run structure and not an argument flag, it is
 * available to asynchronous events. It is cleared (only) by @a
 * bk_run_once(), so the only public interface is the one to set it (since
 * there's no reference count, you may not clear
 * BK_RUN_FLAG_DONT_BLOCK_RUN_ONCE if you decide that you set in in error
 * because you cannot know that someone else has not <b>also</b> set it).
 *
 * The blocking described has nothing to do with threads.
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA thread/global state.
 *	@param run The @a bk_run structure in which to set flag.
 */
void
bk_run_set_dont_block_run_once(bk_s B, struct bk_run *run)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!run)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_FLAG_SET(run->br_flags, BK_RUN_FLAG_DONT_BLOCK_RUN_ONCE);

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_VRETURN(B);
}




/**
 * Add a descriptor to the cancel list.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param run The @a bk_run structure to use.
 *	@param fd The descriptor to add.
 *	@param flags indicating callers choice of cancel options
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_run_fd_cancel_register(bk_s B, struct bk_run *run, int fd, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_fd_cancel *bfc = NULL;

  if (!run || !flags)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!(BK_CALLOC(bfc)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate space to store fd on cancel list: %s\n", strerror(errno));
    BK_RETURN(B, -1);
  }

  bfc->bfc_fd = fd;
  bfc->bfc_flags = flags;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (fd_cancel_insert(run->br_canceled, bfc) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert fd on cancel list: %s\n", fd_cancel_error_reason(run->br_canceled, NULL));
    goto error;
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B,0);

 error:
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (bfc)
    bk_run_fd_cancel_unregister(B, run, fd, flags);
  BK_RETURN(B,-1);
}



/**
 * Unregister a descriptor on the cancel list.
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA thread/global state.
 *	@param run The @a bk_run to use.
 *	@param fd The descriptor to unregister.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_run_fd_cancel_unregister(bk_s B, struct bk_run *run, int fd, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_fd_cancel *bfc = NULL;

  if (!run || !flags)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  // It's common not to have the fd registered.
  if (!(bfc = fd_cancel_search(run->br_canceled, &fd)))
    goto notfound;

  // Clear the flags the user requested
  BK_FLAG_CLEAR(bfc->bfc_flags, flags);

  // If any are still set, don't delete this.
  if (BK_FLAG_ISSET(bfc->bfc_flags, BK_FD_ADMIN_FLAG_WANT_ALL))
    goto notfound;

  if (fd_cancel_delete(run->br_canceled, bfc) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not delete descriptor from cancel list: %s\n", fd_cancel_error_reason(run->br_canceled, NULL));
    goto error;
  }


  free(bfc);
  bfc = NULL;

  BK_FLAG_CLEAR(run->br_flags, BK_RUN_FLAG_FD_CANCEL);
  BK_FLAG_CLEAR(run->br_flags, BK_RUN_FLAG_FD_CLOSED);

  for(bfc = fd_cancel_minimum(run->br_canceled);
      bfc;
      bfc = fd_cancel_successor(run->br_canceled, bfc))
  {
    if (BK_FLAG_ISSET(bfc->bfc_flags, BK_FD_ADMIN_FLAG_IS_CANCELED))
      BK_FLAG_CLEAR(run->br_flags, BK_RUN_FLAG_FD_CANCEL);

    if (BK_FLAG_ISSET(bfc->bfc_flags, BK_FD_ADMIN_FLAG_IS_CLOSED))
      BK_FLAG_CLEAR(run->br_flags, BK_RUN_FLAG_FD_CLOSED);
  }

 notfound:
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B,0);

 error:
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (bfc)
    free(bfc);

  BK_RETURN(B,-1);
}





/**
 * Cancel a file descriptor.
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA thread/global state.
 *	@param run The run structure to use.
 *	@param fd The file descriptor to cancel.
 *	@param flags MUST BE either BK_FD_ADMIN_FLAG_CANCEL or _CLOSE
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_run_fd_cancel(bk_s B, struct bk_run *run, int fd, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_fd_cancel *bfc = NULL;

  if (!run)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  bk_error_printf(B, BK_ERR_NOTICE, "Cancelling fd %d with flags %x\n", fd, flags);

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (!(bfc = fd_cancel_search(run->br_canceled, &fd)))
  {
    bk_error_printf(B, BK_ERR_ERR, "could not locate fd: %d to cancel\n", fd);
    goto error;
  }
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */


  if (BK_FLAG_ISCLEAR(bfc->bfc_flags, flags))
  {
    // <TODO> What's the best behavior here? jtt thinks warn and ignore </TODO>
    bk_error_printf(B, BK_ERR_ERR, "Ignoring cancel request for unregistered fd %d\n", fd);
    BK_RETURN(B,0);
  }

  if (BK_FLAG_ISSET(flags, BK_FD_ADMIN_FLAG_CANCEL))
  {
    bk_debug_printf_and(B,1,"FD: %d canceled\n", fd);
    BK_FLAG_SET(bfc->bfc_flags, BK_FD_ADMIN_FLAG_IS_CANCELED);
    BK_FLAG_SET(run->br_flags, BK_RUN_FLAG_FD_CANCEL);
  }

  if (BK_FLAG_ISSET(flags, BK_FD_ADMIN_FLAG_CLOSE))
  {
    bk_debug_printf_and(B,1,"FD: %d closed\n", fd);
    BK_FLAG_SET(bfc->bfc_flags, BK_FD_ADMIN_FLAG_IS_CLOSED);
    BK_FLAG_SET(run->br_flags, BK_RUN_FLAG_FD_CLOSED);
  }

  BK_RETURN(B,0);

 error:
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B,1);
}



/**
 * Check if the given descriptor has been canceled.
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA thread/global state.
 *	@param run The @a bk_run structure to use.
 *	@param fd The descriptor to search for.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success and @a fd is not on the list.
 *	@return <i>1</i> on success and @a fd is on the list.
 */
int
bk_run_fd_is_canceled(bk_s B, struct bk_run *run, int fd)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_fd_cancel *bfc = NULL;
  int ret = 1;

  if (!run)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  /*
   * The BK_RUN_FLAG_FD_CANCEL flag is set only when there are one or
   * more descriptors on the list. Since the overwhelming majority of the
   * time the list will be empty, but, if it's checked at all, will be
   * checked *many* times, this flag prevents us from doing the somewhat
   * expensive and unnecessary fd_cancel_search() operation.
   */
  if (BK_FLAG_ISCLEAR(run->br_flags, BK_RUN_FLAG_FD_CANCEL) ||
      !(bfc = fd_cancel_search(run->br_canceled, &fd)) ||
      BK_FLAG_ISCLEAR(bfc->bfc_flags, BK_FD_ADMIN_FLAG_IS_CANCELED))
    ret = 0;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B, ret);
}



/**
 * Check if the given descriptor has been administratively closed
 *
 *	@param B BAKA thread/global state.
 *	@param run The @a bk_run structure to use.
 *	@param fd The descriptor to search for.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success and @a fd is not on the list.
 *	@return <i>1</i> on success and @a fd is on the list.
 */
int
bk_run_fd_is_closed(bk_s B, struct bk_run *run, int fd)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_fd_cancel *bfc = NULL;
  int ret = 1;

  if (!run)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  /*
   * The BK_RUN_FLAG_FD_CLOSED flag is set only when there are one or
   * more descriptors on the list. Since the overwhelming majority of the
   * time the list will be empty, but, if it's checked at all, will be
   * checked *many* times, this flag prevents us from doing the somewhat
   * expensive and unnecessary fd_cancel_search() operation.
   */
  if (BK_FLAG_ISCLEAR(run->br_flags, BK_RUN_FLAG_FD_CLOSED) ||
      !(bfc = fd_cancel_search(run->br_canceled, &fd)) ||
      BK_FLAG_ISCLEAR(bfc->bfc_flags, BK_FD_ADMIN_FLAG_IS_CLOSED))
    ret = 0;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B, ret);
}



/**
 * The discard handler--not optimized for large buffers to be thrown away.
 *
 * @param B BAKA thread/global state
 * @param run The run environment
 * @param flags Fun for the future
 */
void bk_run_handler_discard(bk_s B, struct bk_run *run, int fd, u_int gottypes, void *opaque, const struct timeval *starttime)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char buf[64];

  if (!run)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (BK_FLAG_ISSET(gottypes, BK_RUN_READREADY))
  {
    int ret;

    // Ignore errors
#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */

#ifdef BK_USING_PTHREADS
    if (run->br_selectcount > 0)
    {
      bk_debug_printf_and(B, 64, "Someone is still in select--leaving interrupting byte on fd\n");
    }
    else
#endif /* BK_USING_PTHREADS */
    {
      ret = read(fd, buf, sizeof(buf));
      bk_debug_printf_and(B, 64, "Read %d to interrupt select\n", ret);
    }
#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */
  }
  else
  {
    bk_debug_printf_and(B, 64, "Other interrupt select activity: %x\n", gottypes);
  }

  BK_VRETURN(B);
}



/**
 * Verify that select will not block after some changes we just made
 *
 * @param B BAKA thread/global state
 * @param run The run environment
 * @param flags Fun for the future
 */
void bk_run_select_changed(bk_s B, struct bk_run *run, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!run)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B))
  {
    if (BK_FLAG_ISCLEAR(flags, BK_RUN_GLOBAL_FLAG_ISLOCKED) && pthread_mutex_lock(&run->br_lock) != 0)
      abort();

    // Ignore errors
    if (run->br_selectcount > 0)
    {
      int ret = write(run->br_runfd, "A", 1);
      bk_debug_printf_and(B, 64, "Writing %d to interrupt select\n", ret);
    }

    if (BK_FLAG_ISCLEAR(flags, BK_RUN_GLOBAL_FLAG_ISLOCKED) && pthread_mutex_unlock(&run->br_lock) != 0)
      abort();
  }
#endif /* BK_USING_PTHREADS */

  BK_VRETURN(B);
}



#ifdef BK_USING_PTHREADS
/**
 * Returns true if current (possibly only) thread is responsible for i/o on
 * this run environment, and therefore should call bk_run_once rather than
 * doing a (timed) condwait for the io thread to complete the i/o.
 *
 * @param B BAKA thread/global state
 * @param run The run environment
 */
int bk_run_on_iothread(bk_s B, struct bk_run *run)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int its_me = 1;				// in single-threaded case

  if (!run)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&run->br_lock) != 0)
    abort();

  its_me = pthread_equal(pthread_self(), run->br_iothread);

  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&run->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B, its_me);
}



/**
 * Create a loopback file descriptor
 *
 * @param B BAKA thread/global state
 * @return <i>-1</i> on error
 * @return <br><i>non-negative</i> on success
 */
static int bk_run_select_changed_init(bk_s B, struct bk_run *run)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct sockaddr_in originalsin;
  int fd;
  int len = sizeof(originalsin);

  if ((fd = socket(AF_INET, SOCK_DGRAM, PF_UNSPEC)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create loopback socket: %s\n", strerror(errno));
    goto error;
  }

  memset(&originalsin, 0, sizeof(originalsin));
  originalsin.sin_family = AF_INET;
  originalsin.sin_port = 0;

  if (bind(fd, (struct sockaddr *)&originalsin, sizeof(originalsin)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not bind to loopback socket: %s\n", strerror(errno));
    goto error;
  }

  if (getsockname(fd, (struct sockaddr *)&originalsin, &len) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not find name of loopback socket: %s\n", strerror(errno));
    goto error;
  }

  if (connect(fd, (struct sockaddr *)&originalsin, len) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not connect to loopback socket: %s\n", strerror(errno));
    goto error;
  }

  if (bk_run_handle(B, run, fd, bk_run_handler_discard, NULL, BK_RUN_WANTREAD, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not register loopback socket: %s\n", strerror(errno));
    goto error;
  }

  bk_debug_printf_and(B, 64, "Loopback fd %d\n", fd);

  if (0)
  {
  error:
    if (fd >= 0)
      close(fd);
    fd = -1;
  }

  BK_RETURN(B, fd);
}
#endif /* BK_USING_PTHREADS */



/*
 * fd association CLC routines
 */
static int fa_oo_cmp(struct bk_run_fdassoc *a, struct bk_run_fdassoc *b)
{
  return(a->brf_fd - b->brf_fd);
}
static int fa_ko_cmp(int *a, struct bk_run_fdassoc *b)
{
  return(*a - b->brf_fd);
}
static ht_val fa_obj_hash(struct bk_run_fdassoc *a)
{
  return(a->brf_fd);
}
static ht_val fa_key_hash(int *a)
{
  return(*a);
}



/*
 * baka run function list CLC routines
 */
static int brfl_oo_cmp(struct bk_run_func *a, struct bk_run_func *b)
{
  return ((char *)(a->brfn_key) - (char *)(b->brfn_key));
}
static int brfl_ko_cmp(void *a, struct bk_run_func *b)
{
  return ((char *)a)-((char *)b->brfn_key);
}

/*
 * baka run on-demand function list CLC routines
 */
static int brofl_oo_cmp(struct bk_run_ondemand_func *a, struct bk_run_ondemand_func *b)
{
  return ((char *)(a->brof_key) - (char *)(b->brof_key));
}
static int brofl_ko_cmp(void *a, struct bk_run_ondemand_func *b)
{
  return ((char *)a)-((char *)b->brof_key);
}

/*
 * Cancel list routines.
 */
static int fd_cancel_oo_cmp(struct bk_fd_cancel *a, struct bk_fd_cancel *b)
{
  return (a->bfc_fd - b->bfc_fd);
}

static int fd_cancel_ko_cmp(int *a, struct bk_fd_cancel *b)
{
  return (*a - b->bfc_fd);
}
