#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_run.c,v 1.30 2002/09/26 22:04:38 lindauer Exp $";
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
 * All of the baka run public and private functions.
 */

#include <libbk.h>
#include "libbk_internal.h"


/**
 * File descriptor cancel information
 */
struct bk_fd_cancel
{
  int		bfc_fd;				///< The file descriptor in question.
  bk_flags	bfc_flags;			///< Everyone needs flags.
#define BK_FD_CANCEL_FLAG_IS_CANCELED	0x1	///< This file descriptor has been canceled.
};



/**
 * Information about a particular event which will be executed at some future point
 */
struct br_equeue
{
  struct timeval	bre_when;		///< Time to run event
  void			(*bre_event)(bk_s B, struct bk_run *run, void *opaque, const struct timeval *starttime, bk_flags flags); ///< Event to run
  void			*bre_opaque;		///< Data for opaque
};


/**
 * Opaque data for cron job event queue function.  Data containing information about
 * true user callback which is scheduled to run at certain interval.
 */
struct br_equeuecron
{
  time_t		brec_interval;		///< msec Interval timer
  void			(*brec_event)(bk_s B, struct bk_run *run, void *opaque, const struct timeval *starttime, bk_flags flags); ///< Event to run
  void			*brec_opaque;		///< Data for opaque
  struct br_equeue	*brec_equeue;		///< Queued cron event (ie next instance to fire).
};


/**
 * Function to be called syncronously out of bk_run after an async
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
 * provide the service. Note that when windows compatability is
 * required, the fd must be replaced by a struct bk_iodescriptor
 */
struct bk_run_fdassoc
{
  int			brf_fd;			///< Fd we are handling
  bk_fd_handler_t	brf_handler;		///< Function to handle
  void		       *brf_opaque;		///< Opaque information
};


/**
 * All information known about events on the system.  Note that when
 * windows compatability is required, the fd_sets must be supplimented
 * by a list of bk_iodescriptors obtaining the various read/write/xcpt
 * requirements since these may not be expressable in an fdset.
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
  dict_h		br_bnbios;		///< Blocked bnbios
  pq_h			*br_equeue;		///< Event queue
  volatile sig_atomic_t	br_signums[NSIG];	///< Number of signal events we have received
  struct br_sighandler	br_handlerlist[NSIG];	///< Handlers for signals
  bk_flags		br_flags;		///< General flags
#define BK_RUN_FLAG_RUN_OVER		0x01	///< bk_run_run should terminate
#define BK_RUN_FLAG_NEED_POLL		0x02	///< Execute poll list
#define BK_RUN_FLAG_CHECK_DEMAND	0x04	///< Check demand list
#define BK_RUN_FLAG_HAVE_IDLE		0x08	///< Run idle task
#define BK_RUN_FLAG_IN_DESTROY		0x10	///< In the middle of a destroy
#define BK_RUN_FLAG_ABORT_RUN_ONCE	0x20	///< Return now from run_once
#define BK_RUN_FLAG_DONT_BLOCK_RUN_ONCE	0x40	///< Don't block on current run once
#define BK_RUN_FLAG_FD_CANCEL		0x80	///< There exists at least 1 desc. on the cancel lsit.
  dict_h		br_canceled;		///< List of canceled descriptors.
};


/**
 * Information about a polling or idle function known to the run environment
// XXXX -- documentation failure. Need short descriptions of fields (-jtt)
 */
struct bk_run_func
{
  void *	brfn_key;
  int		(*brfn_fun)(bk_s B, struct bk_run *run, void *opaque, const struct timeval *starttime, struct timeval *delta, bk_flags flags);
  void *	brfn_opaque;
  bk_flags	brfn_flags;
  dict_h	brfn_backptr;
};


/**
 * Information about an on-demand function known to the run environment
 */
struct bk_run_ondemand_func
{
  void *		brof_key;		///< Key for searching.
  bk_run_on_demand_f	brof_fun;		///< Function to call.
  void *		brof_opaque;		///< User args.
  bk_flags		brof_flags;		///< Eveyone needs flags.
  dict_h		brof_backptr;		///< Ponter to enclosing dll.
  volatile int *	brof_demand;		///< Integer which controls execution decision.
};



static int bk_run_event_comparator(struct br_equeue *a, struct br_equeue *b);
static void bk_run_event_cron(bk_s B, struct bk_run *run, void *opaque, const struct timeval *starttime, bk_flags flags);
static int bk_run_checkeventq(bk_s B, struct bk_run *run, const struct timeval *starttime, struct timeval *delta);
static struct bk_run_func *brfn_alloc(bk_s B);
static void brfn_destroy(bk_s B, struct bk_run_func *brf);
static struct bk_run_ondemand_func *brof_alloc(bk_s B);
static void brof_destroy(bk_s B, struct bk_run_ondemand_func *brof);



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
 * @name Defines: List of canceled desciprtors This data structure allows
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
 * @name Defines: bnbiol_clc
 * bnbio list CLC definitions
 * to hide CLC choice.
 */
// @{
#define bnbiol_create(o,k,f)		dll_create((o),(k),(f))
#define bnbiol_destroy(h)		dll_destroy(h)
#define bnbiol_insert(h,o)		dll_insert((h),(o))
#define bnbiol_insert_uniq(h,n,o)	dll_insert_uniq((h),(n),(o))
#define bnbiol_append(h,o)		dll_append((h),(o))
#define bnbiol_append_uniq(h,n,o)	dll_append_uniq((h),(n),(o))
#define bnbiol_search(h,k)		dll_search((h),(k))
#define bnbiol_delete(h,o)		dll_delete((h),(o))
#define bnbiol_minimum(h)		dll_minimum(h)
#define bnbiol_maximum(h)		dll_maximum(h)
#define bnbiol_successor(h,o)		dll_successor((h),(o))
#define bnbiol_predecessor(h,o)		dll_predecessor((h),(o))
#define bnbiol_iterate(h,d)		dll_iterate((h),(d))
#define bnbiol_nextobj(h,i)		dll_nextobj(h,i)
#define bnbiol_iterate_done(h,i)	dll_iterate_done(h,i)
#define bnbiol_error_reason(h,i)	dll_error_reason((h),(i))
static int bnbiol_oo_cmp(struct bk_iohh_bnbio *a, struct bk_iohh_bnbio *b);
static int bnbiol_ko_cmp(void *a, struct bk_iohh_bnbio *b);
// @}



/**
 * @name Signal static variables
 *
 * Data which signal handlers running on the signal stack can modify.
 * This is used for syncronous signal handling (the signal handler simply
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
  memset(run, 0, sizeof(*run));

  run->br_flags = flags;

  br_signums = &run->br_signums;			// Initialize static signal array ptr
  br_beensignaled = 0;

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

  if (!(run->br_bnbios = bnbiol_create((dict_function)bnbiol_oo_cmp, (dict_function)bnbiol_ko_cmp, DICT_UNORDERED)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create bnbio list\n");
    goto error;
  }

  if (!(run->br_canceled = fd_cancel_create((dict_function)fd_cancel_oo_cmp, (dict_function)fd_cancel_ko_cmp, DICT_UNORDERED)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create bnbio list: %s\n", fd_cancel_error_reason(NULL, NULL));
    goto error;
  }


  BK_RETURN(B, run);

 error:
  if (run)
    bk_run_destroy(B, run);

  BK_RETURN(B, NULL);
}



/**
 * Destroy the baka run environment.
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

  // Destroy the fd association
  if (run->br_fdassoc)
  {
    struct bk_run_fdassoc *cur;

    while (cur = fdassoc_minimum(run->br_fdassoc))
    {
      (*cur->brf_handler)(B, run, -1, BK_RUN_DESTROY, cur->brf_opaque, &curtime);
      fdassoc_delete(run->br_fdassoc, cur);
      free(cur);
    }
    fdassoc_destroy(run->br_fdassoc);
  }

  // Dequeue the events
  if (run->br_equeue)
  {
    struct br_equeue *cur;

    while (cur = pq_extract_head(run->br_equeue))
    {
      (*cur->bre_event)(B, run, cur->bre_opaque, &curtime, BK_RUN_DESTROY);
      free(cur);
    }
    pq_destroy(run->br_equeue);
  }

  // XXX Consider saving all the original sig handlers instead of just restoring default.
  // Reset signal handlers to their default actions
  for(signum = 1;signum < NSIG;signum++)
  {
    if (run->br_handlerlist[signum].brs_handler)
      bk_run_signal(B, run, signum, (void *)SIG_IGN, NULL, 0);
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
    bk_run_fd_cancel_unregister(B, run, bfc->bfc_fd);
  }
  fd_cancel_destroy(run->br_canceled);
  
  // bnbios are destroyed elsewhere
  bnbiol_destroy(run->br_bnbios);

  br_signums = NULL;

  free(run);

  BK_VRETURN(B);
}



/**
 * Set (or clear) a synchronous handler for some signal.
 *
 * Default is to interrupt system calls
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

  if (!handler || (void *)handler == (void *)SIG_IGN || (void *)handler == (void *)SIG_DFL)
  {							// Disabling signal
    act.sa_handler = (void *)handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
  }
  else
  {							// Enabling signal
    act.sa_handler = bk_run_signal_ihandler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
  }

  // Decide on appropriate system call interrupt/restart semantic (default interrupt)
#ifdef SA_RESTART
  act.sa_flags |= BK_FLAG_ISSET(flags, BK_RUN_SIGNAL_RESTART)?SA_RESTART:0;
#endif /* SA_RESTART */
#ifdef SA_INTERRUPT
  act.sa_flags |= BK_FLAG_ISSET(flags, BK_RUN_SIGNAL_RESTART)?0:SA_INTERRUPT;
#endif /* SA_INTERRUPT */

  run->br_handlerlist[signum].brs_handler = handler;	// Might be NULL
  run->br_handlerlist[signum].brs_opaque = opaque;	// Might be NULL

  sigemptyset(&blockset);
  sigaddset(&blockset, signum);
  sigprocmask(SIG_BLOCK, &blockset, NULL);
  if (BK_FLAG_ISSET(flags, BK_RUN_SIGNAL_CLEARPENDING))
    run->br_signums[signum] = 0;

  if (sigaction(signum, &act, NULL) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert signal: %s\n",strerror(errno));
    BK_RETURN(B, -1);
  }
  sigprocmask(SIG_UNBLOCK, &blockset, NULL);

  BK_RETURN(B, 0);
}



/**
 * Enqueue an event for future action.
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

  if (!(new = malloc(sizeof(*new))))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate event queue structure: %s\n",strerror(errno));
    BK_RETURN(B, -1);
  }

  new->bre_when = when;
  new->bre_event = event;
  new->bre_opaque = opaque;

  if (pq_insert(run->br_equeue, new) != PQ_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert into event queue: %s\n",pq_error_reason(run->br_equeue, NULL));
    goto error;
  }

  // Save structure if requested for later CLC delete
  if (handle)
    *handle = new;

  BK_RETURN(B, 0);

 error:
  if (new) free(new);
  BK_RETURN(B, -1);
}



/**
 * Enqueue an event for a future action
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

  BK_RETURN(B, bk_run_enqueue(B, run, tv, event, opaque, handle, 0));
}



/**
 * Set up a reoccurring event at a periodic interval
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

  ret = bk_run_enqueue_delta(B, run, brec->brec_interval, bk_run_event_cron, brec, ((void **)&brec->brec_equeue), 0);

  if (handle)
    *handle = brec;

  BK_RETURN(B, ret);
}



/**
 * Dequeue a normal event or a full "cron" job.
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
    free(brec);
  }

  pq_delete(run->br_equeue, bre);
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

  BK_FLAG_CLEAR(run->br_flags, BK_RUN_FLAG_RUN_OVER); // Don't inherit previous setting

  while (BK_FLAG_ISCLEAR(run->br_flags, BK_RUN_FLAG_RUN_OVER))
  {
    if ((ret = bk_run_once(B, run, flags)) < 0)
      break;
  }

  BK_RETURN(B, ret);
}



//<TODO> add DON'T_BLOCK flag (treat much like RUN_OVER </TODO>
/**
 * Run through all events once. Don't call from a callback or beware.
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
  fd_set readset, writeset, xcptset;
  struct timeval curtime, deltaevent, deltapoll;
  struct timeval *selectarg;
  int ret;
  int x;
  int use_deltapoll, check_idle;	
  struct timeval zero;

  deltapoll.tv_sec = INT32_MAX;
  deltapoll.tv_usec = INT32_MAX;

  if (!run)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  /*
   * The purpose of the flag should be explained. libbaka supports the idea
   * of "blocking" reads in an otherwise asynchronous environment. The
   * details of this are explained elsewhere (presumably b_ioh.c), but it
   * suffices here to point out (or remind) the reader that this fact means
   * that at any given momment we might have *two* bk_run_once()'s calls
   * active. One, more or less at the base of the stack, which is the
   * bk_run_once() loop that you expect to exist. The other occurs when
   * someone has invoked one of these "blocking" things. In general these
   * two invocations coexist without too much fuss, but there is one detail
   * for which we must account. Suppose by way of exaple that the "base"
   * (or expected) bk_run_once() has returned from select(2) with 3 fd's
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
   * waste of time; and at worst this could cause really bizzare problems
   * (like double frees or somesuch). Thus we need to configue a way that
   * the blocking bk_run_once() can "communicate" back to the base
   * bk_run_once() that it (the blocking versions) has run and that any
   * state in the base version may be invalid. The base version's response
   * to this is very simple; just abort the current run and go around
   * again. It never *hurts* to do this, in general; it just wastes time
   * (though in this case, obviously, it doesn't waste time; it prevents
   * core dumps). The strategy we employ is childishly simple. Set a run
   * structure flag as you *leave* bk_run_once() which is reset immediately
   * upon entry. 99.999....9% of the time this is just one two-stage noop,
   * but in the case of the blocking bk_run_once() this actually has some
   * effect. The flag is set by when the blocking version exits. So when
   * control is returned to the base version (in medias res) this flag is
   * set. You will notice that we check it many times in the function
   * (basically everywhere we also check for signals). As soon as the base
   * version notices that the flag is set, it immediately aborts the
   * current run (therefore will not attempt to process any more "dead"
   * state), returns, and clears the flag on the next entry. Whew -jtt
   */
  BK_FLAG_CLEAR(run->br_flags, BK_RUN_FLAG_ABORT_RUN_ONCE);

  zero.tv_sec = 0;
  zero.tv_usec = 0;

  bk_debug_printf_and(B,4,"Starting bk_run_once\n");

  gettimeofday(&curtime, NULL);

  check_idle=0;
  use_deltapoll=0;

  if (BK_FLAG_ISSET(run->br_flags, BK_RUN_FLAG_ABORT_RUN_ONCE)) goto abort_run_once;
  if (br_beensignaled) goto beensignaled;

  if (BK_FLAG_ISSET(run->br_flags, BK_RUN_FLAG_CHECK_DEMAND))
  {
    struct bk_run_ondemand_func *brof;
    for(brof=brfl_minimum(run->br_ondemand_funcs);
	brof;
	brof=brfl_successor(run->br_ondemand_funcs,brof))
    {
      if (*brof->brof_demand && (*brof->brof_fun)(B, run, brof->brof_opaque, brof->brof_demand, &curtime, 0) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "The on demand procedure failed severely.\n");
	goto error;
      }
    }
  }

  if (BK_FLAG_ISSET(run->br_flags, BK_RUN_FLAG_ABORT_RUN_ONCE)) goto abort_run_once;
  if (br_beensignaled) goto beensignaled;

  // Run polling activities
  if (BK_FLAG_ISSET(run->br_flags,BK_RUN_FLAG_NEED_POLL))
  {
    struct bk_run_func *brfn;
    struct timeval tmp_deltapoll = deltapoll;

    for(brfn=brfl_minimum(run->br_poll_funcs);
	brfn;
	brfn=brfl_successor(run->br_poll_funcs,brfn))
    {
      if ((ret=(*brfn->brfn_fun)(B, run, brfn->brfn_opaque, &curtime, &tmp_deltapoll,0)) < 0)
      {
	bk_error_printf(B, BK_ERR_WARN, "The polling procedure failed severely.\n");
	goto error;
      }

      if (ret == 1)
      {
	if (!use_deltapoll || (BK_TV_CMP(&tmp_deltapoll, &deltapoll) < 0))
	{
	  deltapoll=tmp_deltapoll;
	}
	use_deltapoll=1;
      }
    }
  }

  if (BK_FLAG_ISSET(run->br_flags, BK_RUN_FLAG_ABORT_RUN_ONCE)) goto abort_run_once;
  if (br_beensignaled) goto beensignaled;

  // Check for event queue
  if ((ret = bk_run_checkeventq(B, run, &curtime, &deltaevent)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "The event queue checking procedure failed severely.\n");
    goto error;
  }

  // Figure out the select argument
  if (ret == 0)
  {
    /* There are no queued events, so either use deltapoll or .... */
    if (use_deltapoll)
    {
      selectarg = &deltapoll;
    }
    else
    {
      /* ... wait forever */
      selectarg = NULL;
    }
  }
  else
  {
    /* 
     * Use the shorter delta (between event and poll) if there's a deltapoll
     * at to be concerned about or ...
     */
    if (use_deltapoll && (BK_TV_CMP(&deltapoll, &deltaevent) < 0))
    {
      selectarg = &deltapoll;
    }
    else
    {
      /* ... just use deltaevent */
      selectarg = &deltaevent;
    }
  }

  /* 
   * If some caller has registered and idle task then check to see *if*
   * we would have blocked in select(2) for at least *some* amount of time.
   * If so, rewrite the timeout to { 0,0 } (poll), but note that idle
   * tasks should run.
   */
  if (BK_FLAG_ISSET(run->br_flags, BK_RUN_FLAG_HAVE_IDLE) &&
      (!selectarg || selectarg->tv_sec > 0 || selectarg->tv_usec > 0))
  {
    selectarg = &zero;
    check_idle=1;
  }

  /* 
   * If this is the final run then turn select(2) into a poll.  This
   * ensures that should we turn off select in an event (the final event of
   * the run), we don't block forever owing to lack of descriptors in the
   * select set.
   */
  if (BK_FLAG_ISSET(run->br_flags, BK_RUN_FLAG_RUN_OVER | BK_RUN_FLAG_DONT_BLOCK_RUN_ONCE) || 
      BK_FLAG_ISSET(flags, BK_RUN_ONCE_FLAG_DONT_BLOCK))
  {
    selectarg = &zero; 
  }

  if (bk_debug_and(B,4))
  {
    int f;
    int s=getdtablesize();
    char *p;
    struct bk_memx *bm=bk_memx_create(B, 1, 128, 128, 0);
    char scratch[1024];

    if (!bm)
      goto out;

    snprintf(scratch,1024, "Readset: ");
    if (!(p=bk_memx_get(B, bm, strlen(scratch), NULL, BK_MEMX_GETNEW)))
    {
      goto out;
    }
    memcpy(p,scratch,strlen(scratch));
    for(f=0;f<s;f++)
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
    if (!(p=bk_memx_get(B, bm, 2, NULL, BK_MEMX_GETNEW)))
    {
      goto out;
    }
    memcpy(p,"\n",2);
    bk_debug_printf(B,"%s", (char *)bk_memx_get(B,bm,0,NULL,0));
  out:
    if (bm) bk_memx_destroy(B,bm,0);
  }

  readset = run->br_readset;
  writeset = run->br_writeset;
  xcptset = run->br_xcptset;

  if (BK_FLAG_ISSET(run->br_flags, BK_RUN_FLAG_ABORT_RUN_ONCE)) goto abort_run_once;
  if (br_beensignaled) goto beensignaled;

  // Wait for I/O or timeout
  if ((ret = select(run->br_selectn, &readset, &writeset, &xcptset, selectarg)) < 0)
  {
    // XXX-Handle badfd, somehow, getfdflags--withdraw and notify handler
    if (errno != EINTR)
    {
      bk_error_printf(B, BK_ERR_ERR, "Select failed severely: %s\n", strerror(errno));
      goto error;
    }
  }

  // Time may have changed drasticly during select
  gettimeofday(&curtime, NULL);

  if (BK_FLAG_ISSET(run->br_flags, BK_RUN_FLAG_ABORT_RUN_ONCE)) goto abort_run_once;
  if (br_beensignaled) goto beensignaled;

  // Are there any I/O events pending?
  if (ret > 0 )
  {
    for (x=0; ret > 0 && x < run->br_selectn; x++)
    {
      int type = 0;

      if (FD_ISSET(x, &readset))
	type |= BK_RUN_READREADY;
      if (FD_ISSET(x, &writeset))
	type |= BK_RUN_WRITEREADY;
      if (FD_ISSET(x, &xcptset))
	type |= BK_RUN_XCPTREADY;

      bk_debug_printf_and(B,1,"Activity detected on %d: type: %d\n", x, type);

      if (type)
      {
	struct bk_run_fdassoc *curfd;

	ret--;

	if (!(curfd = fdassoc_search(run->br_fdassoc, &x)))
	{
	  bk_error_printf(B, BK_ERR_WARN, "Could not find fd %d in association, yet type is %x\n",x,type);
	  continue;
	}
	(*curfd->brf_handler)(B, run, x, type, curfd->brf_opaque, &curtime);
      }
      if (BK_FLAG_ISSET(run->br_flags, BK_RUN_FLAG_ABORT_RUN_ONCE)) goto abort_run_once;
      if (br_beensignaled) goto beensignaled;
    }
  }
  else 
  {
    /* No I/O has occured. Check for idle task */
    if (check_idle)
    {
      struct bk_run_func *brfn;
      for(brfn=brfl_minimum(run->br_idle_funcs);
	  brfn;
	  brfn=brfl_successor(run->br_idle_funcs,brfn))
      {
	if ((*brfn->brfn_fun)(B, run, brfn->brfn_opaque, &curtime, NULL, 0)<0)
	{
	  bk_error_printf(B, BK_ERR_WARN, "The polling procedure failed severely.\n");
	  goto error;
	}
      }
    }
  }

  // Check for synchronous signals to handle
 beensignaled:
  while (br_beensignaled)
  {
    br_beensignaled = 0;				// Race condition--needs locking

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
  BK_FLAG_SET(run->br_flags, BK_RUN_FLAG_ABORT_RUN_ONCE);
  BK_FLAG_CLEAR(run->br_flags, BK_RUN_FLAG_DONT_BLOCK_RUN_ONCE);
  BK_RETURN(B, 0);

 error:
  BK_FLAG_SET(run->br_flags, BK_RUN_FLAG_ABORT_RUN_ONCE);
  BK_FLAG_CLEAR(run->br_flags, BK_RUN_FLAG_DONT_BLOCK_RUN_ONCE);
  BK_RETURN(B,-1);
  
}



/**
 * Specify the handler to take care of all fd activity.
 *
 *	@param B BAKA thread/global state 
 *	@param run The baka run environment state
 *	@param fd The file descriptor which should be monitored
 *	@param handler The function to call when activity is monitored on the fd
 *	@param wanttypes What types of activities you want notification on
 *	@param flags Flags for the Future.
 *	@return <i><0</i> on call failure, or other error.
 *	@return <br><i>0</i> on success.
 */
struct bk_run;
int bk_run_handle(bk_s B, struct bk_run *run, int fd, bk_fd_handler_t handler, void *opaque, u_int wanttypes, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_run_fdassoc *new = NULL;

  if (!run || !handler)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!(new = malloc(sizeof(*new))))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate fd association structure: %s\n",strerror(errno));
    BK_RETURN(B, -1);
  }

  new->brf_fd = fd;
  new->brf_handler = handler;
  new->brf_opaque = opaque;

  if (fdassoc_insert(run->br_fdassoc, new) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not associate fd %d: %s\n",fd,fdassoc_error_reason(run->br_fdassoc, NULL));
    goto error;
  }

  if (BK_FLAG_ISSET(wanttypes, BK_RUN_WANTREAD))
  {
    FD_SET(fd, &run->br_readset);
  }
  else
  {
    FD_CLR(fd, &run->br_readset);
  }

  if (BK_FLAG_ISSET(wanttypes, BK_RUN_WANTWRITE))
  {
    FD_SET(fd, &run->br_writeset);
  }
  else
  {
    FD_CLR(fd, &run->br_writeset);
  }

  if (BK_FLAG_ISSET(wanttypes, BK_RUN_WANTXCPT))
  {
    FD_SET(fd, &run->br_xcptset);
  }
  else
  {
    FD_CLR(fd, &run->br_xcptset);
  }

  run->br_selectn = MAX(run->br_selectn,fd+1);

  bk_debug_printf_and(B,1,"Added fd: %d -- selectn now: %d\n", fd, run->br_selectn);

  BK_RETURN(B, 0);

 error:
  if (new)
  {
    free(new);
  }
  BK_RETURN(B, -1);
}



/**
 * Specify that we no longer wish bk_run to take care of file descriptors.
 * We do not close the fd.
 *
 *	@param B BAKA thread/global state 
 *	@param run The baka run environment state
 *	@param fd The file descriptor which should be monitored
 *	@param flags Flags for the Future.
 *	@return <i><0</i> on call failure, or other error.
 *	@return <br><i>0</i> on success.
 */
int bk_run_close(bk_s B, struct bk_run *run, int fd, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_run_fdassoc *curfda;
  struct timeval curtime;

  if (!run)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!(curfda = fdassoc_search(run->br_fdassoc, &fd)))
  {
    bk_debug_printf_and(B,4,"Double close protection kicked in\n");
    bk_error_printf(B, BK_ERR_WARN, "Could not find fd %d in association while attempting to delete\n",fd);
    BK_RETURN(B, 0);
  }

  // Get rid of event in list, which will also prevent double deletion
  if (fdassoc_delete(run->br_fdassoc, curfda) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not delete descriptor %d from fdassoc list: %s\n", fd, fdassoc_error_reason(run->br_fdassoc, NULL));
    BK_RETURN(B,-1);
  }

  // Optionally tell user handler that he will never be called again.
  gettimeofday(&curtime, NULL);
  (*curfda->brf_handler)(B, run, fd, BK_RUN_CLOSE, curfda->brf_opaque, &curtime);

  free(curfda);

  FD_CLR(fd, &run->br_readset);
  FD_CLR(fd, &run->br_writeset);
  FD_CLR(fd, &run->br_xcptset);

  // Check to see if we need to find out a new value for selectn
  if (run->br_selectn == fd+1)
  {
    run->br_selectn = 0;
    for(curfda=fdassoc_minimum(run->br_fdassoc);curfda;curfda = fdassoc_successor(run->br_fdassoc, curfda))
    {
      run->br_selectn = MAX(run->br_selectn,curfda->brf_fd + 1);
    }
  }

  bk_debug_printf_and(B,1,"Closed fd: %d -- selectn now: %d\n", fd, run->br_selectn);
  BK_RETURN(B, 0);
}



/**
 * Find out what read/write/xcpt desires are current for a given fd.
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

  if (FD_ISSET(fd, &run->br_readset))
    type |= BK_RUN_WANTREAD;
  if (FD_ISSET(fd, &run->br_writeset))
    type |= BK_RUN_WANTWRITE;
  if (FD_ISSET(fd, &run->br_xcptset))
    type |= BK_RUN_WANTXCPT;

  BK_RETURN(B, type);
}



/**
 * Find out what read/write/xcpt desires are current for a given fd.
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

  if (!run || fd < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

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
  return(BK_TV_CMP(&a->bre_when,&b->bre_when));
}



/**
 * Signal handler for synchronous signal--glue beween OS and libbk
 *
 * <TODO>Worry about locking!</TODO>
 * <TODO>Autoconf for multiple signal prototypes</TODO>
 *
 *	@param signnum signal number that was received
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
    bk_run_enqueue(B, run, addtv, bk_run_event_cron, brec, ((void **)&brec->brec_equeue), 0);

  (*brec->brec_event)(B, run, brec->brec_opaque, starttime, flags);

  BK_VRETURN(B);
}



/**
 * Execute all pending event queue events, return delta time to next event via *delta.
 *
 *	@param B BAKA Thread/global state
 *	@param run Run environment handle
 *	@param startime The time when this global-event queue run was started
 *	@param delta The time to the next event
 *	@return <i>-1</i> on call failure
 *	@return <br><i>0</i> if there is no next event
 *	@return <br><i>1</i> if there is a next event
 *	@return <br>copy-out <i>delta</i> is time to next event, if there is a next event
 */
static int bk_run_checkeventq(bk_s B, struct bk_run *run, const struct timeval *starttime, struct timeval *delta)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct br_equeue *top;
  struct timeval curtime;

  if (!run || !delta)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, -1);
  }

  while (top = pq_head(run->br_equeue))
  {
    if (BK_TV_CMP(&top->bre_when, starttime) > 0)
      break;  

    top = pq_extract_head(run->br_equeue);

    (*top->bre_event)(B, run, top->bre_opaque, starttime, 0);
    free(top);
  }

  if (!top)
    BK_RETURN(B,0);    

  /* Use the actual time to next event to allow for more accurate events */
  gettimeofday(&curtime, NULL);

  BK_TV_SUB(delta,&top->bre_when,&curtime);
  if (delta->tv_sec < 0 || delta->tv_usec < 0)
  {
    delta->tv_sec = 0;
    delta->tv_usec = 0;
  }

  BK_RETURN(B, 1);
}



/** 
 * Add a function for bk_run polling.
 *	@param B BAKA thread/global state 
 * 	@param run bk_run structure pointer
 *	@param fun function to call
 * 	@param opaque data for fun call
 * 	@param handle handle to use to remove this fun
 * 	@return <i>-1</i> on failure
 * 	@return <br><i>0</i> on success
 */
int
bk_run_poll_add(bk_s B, struct bk_run *run, int (*fun)(bk_s B, struct bk_run *run, void *opaque, const struct timeval *starttime, struct timeval *delta, bk_flags flags), void *opaque, void **handle)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_run_func *brfn=NULL;

  if (!fun)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (handle) *handle=NULL;

  if (!(brfn = brfn_alloc(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate brf: %s\n", strerror(errno));
    goto error;
  }
  
  brfn->brfn_fun=fun;
  brfn->brfn_opaque=opaque;
  brfn->brfn_flags=0;

  if (brfl_insert(run->br_poll_funcs, brfn) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert brf in dll\n");
    goto error;
  }
  
  brfn->brfn_backptr=run->br_poll_funcs;
  BK_FLAG_SET(run->br_flags,BK_RUN_FLAG_NEED_POLL);
  
  if (handle) *handle=brfn->brfn_key;
  BK_RETURN(B,0);

 error:
  if (brfn) brfn_destroy(B,brfn);
  BK_RETURN(B,-1);
}



/**
 * Remove a function from the bk_run poll list
 *	@param B BAKA thread/global state 
 *	@param run bk_run structure
 *	@param handle polling function to remove
XXX - document return
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
  
  if (!(brf=brfl_search(run->br_poll_funcs, handle)))
  {
    bk_error_printf(B, BK_ERR_WARN, "Handle %p not found in delete\n", handle);
    BK_RETURN(B, 0);
  }

  // XXX - Delete handle and free it here

  if (!brfl_minimum(run->br_poll_funcs))
  {
    BK_FLAG_CLEAR(run->br_flags,BK_RUN_FLAG_NEED_POLL);
  }
  BK_RETURN(B, 0);
}



/** 
 * Add a function to bk_run idle task
 *	@param B BAKA thread/global state 
 * 	@param run bk_run structure pointer
 *	@param fun function to call
 * 	@param opaque data for fun call
 * 	@param handle handle to use to remove this fun
 * 	@return <i>-1</i> on failure
 * 	@return <br><i>0</i> on success
XXX - consolidate with poll_add
 */
int
bk_run_idle_add(bk_s B, struct bk_run *run, int (*fun)(bk_s B, struct bk_run *run, void *opaque, const struct timeval *starttime, struct timeval *delta, bk_flags flags), void *opaque, void **handle)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_run_func *brfn=NULL;

  if (!fun)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (handle) *handle=NULL;

  if (!(brfn = brfn_alloc(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate brf: %s\n", strerror(errno));
    goto error;
  }
  
  brfn->brfn_fun=fun;
  brfn->brfn_opaque=opaque;
  brfn->brfn_flags=0;

  if (brfl_insert(run->br_idle_funcs, brfn) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert brf in dll\n");
    goto error;
  }
  
  brfn->brfn_backptr=run->br_idle_funcs;
  BK_FLAG_SET(run->br_flags,BK_RUN_FLAG_HAVE_IDLE);
  
  if (handle) *handle=brfn->brfn_key;
  BK_RETURN(B,0);

 error:
  if (brfn) brfn_destroy(B,brfn);
  BK_RETURN(B,-1);
}



/**
 * Remove a function from the bk_run poll list
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
  
  if (!(brfn=brfl_search(run->br_idle_funcs, handle)))
  {
    bk_error_printf(B, BK_ERR_WARN, "Handle %p not found in delete\n", handle);
    BK_RETURN(B, 0);
  }

  if (!brfl_minimum(run->br_idle_funcs))
  {
    BK_FLAG_CLEAR(run->br_flags,BK_RUN_FLAG_HAVE_IDLE);
  }
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
  free(brfn);
  BK_VRETURN(B);
}



/** 
 * Add a function to bk_run on demand list
 *	@param B BAKA thread/global state 
 * 	@param run bk_run structure pointer
 *	@param fun function to call
 * 	@param opaque data for fun call
 * 	@param demand pointer to int which controls whether function will run
 * 	@param handle handle to use to remove this fun
 * 	@return <i>-1</i> on failure
 * 	@return <br><i>0</i> on success
 */
int
bk_run_on_demand_add(bk_s B, struct bk_run *run, bk_run_on_demand_f fun, void *opaque, volatile int *demand, void **handle)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_run_ondemand_func *brof=NULL;

  if (!fun)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  if (handle) *handle=NULL;

  if (!(brof=brof_alloc(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate brf: %s\n", strerror(errno));
    goto error;
  }
  
  brof->brof_fun=fun;
  brof->brof_opaque=opaque;
  brof->brof_demand=demand;
  brof->brof_flags=0;
  brof->brof_key = brof;

  if (brfl_insert(run->br_ondemand_funcs, brof) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert brf in dll\n");
    goto error;
  }
  
  brof->brof_backptr=run->br_ondemand_funcs;
  BK_FLAG_SET(run->br_flags,BK_RUN_FLAG_CHECK_DEMAND);
  
  if (handle) *handle=brof->brof_key;
  BK_RETURN(B,0);

 error:
  if (brof) brof_destroy(B,brof);
  BK_RETURN(B,-1);
}



/**
 * Remove a function from the bk_run poll list
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
  
  if (!(brof=brfl_search(run->br_ondemand_funcs, handle)))
  {
    bk_error_printf(B, BK_ERR_WARN, "Handle %p not found in delete\n", handle);
    BK_RETURN(B, 0);
  }
  
  brof_destroy(B, brof);

  if (!brfl_minimum(run->br_ondemand_funcs))
  {
    BK_FLAG_CLEAR(run->br_flags,BK_RUN_FLAG_CHECK_DEMAND);
  }
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
  free(brof);
  BK_VRETURN(B);
}



/** 
 * Turn of the run enviornment
XXX documentation failure
XXX naming failure _bk_run_run_run_finished
XXX naming failure _bk_run_lola_run
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

  BK_FLAG_SET(run->br_flags, BK_RUN_FLAG_RUN_OVER);
  BK_RETURN(B,0);
}



/**
 * Set the BK_RUN_FLAG_DONT_BLOCK_RUN_ONCE flag in the run structure. This
 * flag behaves <b>exactly</b> like the @a bk_run_once() flag
 * BK_RUN_ONCE_FLAG_DONT_BLOCK. The only difference is that, owing to the
 * fact that it is set in the run structure and not an argument flag, it is
 * available to asychronous events. It is cleared (only) by @a
 * bk_run_once(), so the only public interface is the one to set it (since
 * there's no reference count, you may not clear
 * BK_RUN_FLAG_DONT_BLOCK_RUN_ONCE if you decide that you set in in error
 * because you cannot know that someone else has not <b>also</b> set it).
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


  BK_FLAG_SET(run->br_flags, BK_RUN_FLAG_DONT_BLOCK_RUN_ONCE);
  BK_VRETURN(B);  
}




/**
 * Add a descriptor to the cancel list.
 *
 *	@param B BAKA thread/global state.
 *	@param run The @a bk_run structure to use.
 *	@param fd The descriptor to add.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_run_fd_cancel_register(bk_s B, struct bk_run *run, int fd)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_fd_cancel *bfc = NULL;

  if (!run)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  if (!(BK_CALLOC(bfc)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate space to store fd on cancel list: %s\n", strerror(errno));
    goto error;
  }

  bfc->bfc_fd = fd;

  if (fd_cancel_insert(run->br_canceled, bfc) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert fd on cancel list: %s\n", fd_cancel_error_reason(run->br_canceled, NULL));
    goto error;
  }
  
  BK_RETURN(B,0);  

 error:
  if (bfc)
    bk_run_fd_cancel_unregister(B, run, fd);
  BK_RETURN(B,-1);  
}



/**
 * Unregister a descriptor on the cancel list.
 *
 *	@param B BAKA thread/global state.
 *	@param run The @a bk_run to use.
 *	@param fd The descriptor to unregister.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_run_fd_cancel_unregister(bk_s B, struct bk_run *run, int fd)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_fd_cancel *bfc = NULL;
  int at_least_one_is_canceled = 0;


  if (!run)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  // It's common not to have the fd registered.
  if (!(bfc = fd_cancel_search(run->br_canceled, &fd)))
    BK_RETURN(B,0);    

  if (fd_cancel_delete(run->br_canceled, bfc) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not delete descriptor from cancel list: %s\n", fd_cancel_error_reason(run->br_canceled, NULL));
    goto error;
  }

  free(bfc); 
  bfc = NULL;

  for(bfc = fd_cancel_minimum(run->br_canceled);
      bfc;
      bfc = fd_cancel_successor(run->br_canceled, bfc))
  {
    if (BK_FLAG_ISSET(bfc->bfc_flags, BK_FD_CANCEL_FLAG_IS_CANCELED))
      at_least_one_is_canceled = 1;
  }

  if (!at_least_one_is_canceled)
    BK_FLAG_CLEAR(run->br_flags, BK_RUN_FLAG_FD_CANCEL);

  BK_RETURN(B,0);  
  
 error:
  if (bfc)
    free(bfc);

  BK_RETURN(B,-1);  
}





/**
 * Cancel a filed descriptor.
 *
 *	@param B BAKA thread/global state.
 *	@param run The run structure to use.
 *	@param fd The file desc. to cancel.
 *	@param flags Flags for future use.
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
  
  
  if (!(bfc = fd_cancel_search(run->br_canceled, &fd)))
  {
    bk_error_printf(B, BK_ERR_ERR, "could not locate fd: %d to cancel\n", fd);
    goto error;
  }

  BK_FLAG_SET(bfc->bfc_flags, BK_FD_CANCEL_FLAG_IS_CANCELED);
  BK_FLAG_SET(run->br_flags, BK_RUN_FLAG_FD_CANCEL);

  BK_RETURN(B,0);  
  
 error:
  BK_RETURN(B,1);  
}



/**
 * Check if the given desciptor has been canceled.
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
  
  if (!run)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  /*
   * The BK_RUN_FLAG_FD_CANCEL flag is set only when there are one or
   * more descriptors on the list. Since the overwhelming majority of the
   * time the list will be empty, but, if it's checked at all, will be
   * checked *many* times, this flag prevents us from doing the somewhat
   * expensive and unnecessary fd_cancel_search() operation.
   */
  if (BK_FLAG_ISCLEAR(run->br_flags, BK_RUN_FLAG_FD_CANCEL) || 
      !(bfc = fd_cancel_search(run->br_canceled, &fd)) ||
      BK_FLAG_ISCLEAR(bfc->bfc_flags, BK_FD_CANCEL_FLAG_IS_CANCELED))
    BK_RETURN(B,0); 


  BK_RETURN(B,1);
}



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
 * baka bnbio list CLC routines
 */
static int bnbiol_oo_cmp(struct bk_iohh_bnbio *a, struct bk_iohh_bnbio *b)
{
  return (a - b);
}
static int bnbiol_ko_cmp(void *a, struct bk_iohh_bnbio *b)
{
  return (((struct bk_iohh_bnbio*) a) - b);
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
