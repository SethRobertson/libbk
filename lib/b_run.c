#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_run.c,v 1.4 2001/11/06 00:41:54 seth Exp $";
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

/**
 * @file
 * All of the baka run public and private functions.
 */

#include <libbk.h>
#include "libbk_internal.h"



static int bk_run_event_comparator(struct br_equeue *a, struct br_equeue *b);
static void bk_run_signal_ihandler(int signum);
static void bk_run_event_cron(bk_s B, struct bk_run *run, void *opaque, struct timeval starttime, bk_flags flags);
static int bk_run_checkeventq(bk_s B, struct bk_run *run, struct timeval starttime, struct timeval *delta);
static struct bk_run_func *brfn_alloc(bk_s B);
static void brfn_destroy(bk_s B, struct bk_run_func *brf);
static struct bk_run_ondemand_func *brof_alloc(bk_s B);
static void brof_destroy(bk_s B, struct bk_run_ondemand_func *brof);



/**
 * @group fdassocclc File Descriptor assocations CLC definitions.
 * CLC indirection to hide fd association CLC choice. 
 * @{
 */
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
/*@}*/

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



/*
 * Static.  Yes, well, when you have signals which have to affect
 * system state, you have no choice but to use a static or global.
 *
 * This points to the bk_run number of signal received array.
 */
static volatile u_int8_t	(*br_signums)[NSIG];
static volatile int		br_beensignaled;



/**
 * Create and initialize the run environment.
 *	@param B BAKA thread/global state 
 *	@param flags Flags for future expansion--saved through run structure.
 *	@return NULL on call failure, allocation failure, or other fatal error.
 *	@return The initialized baka run structure if successful.
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

  if (!run)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  gettimeofday(&curtime,0);

  // Destroy the fd association
  if (run->br_fdassoc)
  {
    struct bk_run_fdassoc *cur;

    while (cur = fdassoc_minimum(run->br_fdassoc))
    {
      (*cur->brf_handler)(B, run, -1, BK_RUN_DESTROY, cur->brf_opaque, curtime);
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
      (*cur->bre_event)(B, run, cur->bre_opaque, curtime, BK_RUN_DESTROY);
      free(cur);
    }
  }

  // Reset signal handlers to their default actions
  for(signum = 0;signum < NSIG;signum++)
  {
    bk_run_signal(B, run, signum, NULL, NULL, 0);
  }

  while (brfn=brfl_minimum(run->br_poll_funcs))
  {
    (*brfn->brfn_fun)(B, run, brfn->brfn_opaque, curtime, NULL, BK_RUN_DESTROY);
    brfn_destroy(B, brfn);
  }
  brfl_destroy(run->br_poll_funcs);

  while (brfn=brfl_minimum(run->br_idle_funcs))
  {
    (*brfn->brfn_fun)(B, run, brfn->brfn_opaque, curtime, NULL, BK_RUN_DESTROY);
    brfn_destroy(B, brfn);
  }
  brfl_destroy(run->br_idle_funcs);

  while (brof=brfl_minimum(run->br_ondemand_funcs))
  {
    (*brof->brof_fun)(B, run, brof->brof_opaque, brof->brof_demand, curtime, BK_RUN_DESTROY);
    brof_destroy(B, brof);
  }
  brofl_destroy(run->br_ondemand_funcs);

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
 *	@return <0 on call failure, system call failure, or other failure.
 *	@return 0 on success.
 */
int bk_run_signal(bk_s B, struct bk_run *run, int signum, void (*handler)(bk_s B, struct bk_run *run, int signum, void *opaque, struct timeval starttime), void *opaque, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct sigaction act, oact;

  if (!run || signum >= NSIG)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,-1);
  }

  if (!handler || (void *)handler == (void *)SIG_IGN || (void *)handler == (void *)SIG_DFL)
  {							// Disabling signal
    act.sa_handler = ((void *)handler==(void *)SIG_IGN)?(void *)SIG_IGN:(void *)SIG_DFL;
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

  if (sigaction(signum, &act, &oact) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert signal: %s\n",strerror(errno));
    BK_RETURN(B, -1);
  }

  run->br_handlerlist[signum].brs_handler = handler;	// Might be NULL
  run->br_handlerlist[signum].brs_opaque = opaque;	// Might be NULL

  if (BK_FLAG_ISSET(flags, BK_RUN_SIGNAL_CLEARPENDING))
    run->br_signums[signum] = 0;

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
 *	@return <0 on call failure, allocation failure, or other error.
 *	@return 0 on success.
 */
int bk_run_enqueue(bk_s B, struct bk_run *run, struct timeval when, void (*event)(bk_s B, struct bk_run *run, void *opaque, struct timeval starttime, bk_flags flags), void *opaque, void **handle, bk_flags flags)
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
 * Enqueue an event for a future action.
 *
 *	@param B BAKA thread/global state 
 *	@param run The baka run environment state
 *	@param usec The number of microseconds until the event should fire
 *	@param event The handler to fire when the time comes (or we are destroyed).
 *	@param opaque The opaque data for the handler
 *	@param handle A copy-out parameter to allow someone to dequeue the event in the future
 *	@param flags Flags for the Future.
 *	@return <0 on call failure, allocation failure, or other error.
 *	@return 0 on success.
 */
int bk_run_enqueue_delta(bk_s B, struct bk_run *run, time_t usec, void (*event)(bk_s B, struct bk_run *run, void *opaque, struct timeval starttime, bk_flags flags), void *opaque, void **handle, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct timeval tv, diff;

  if (!run || !event)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  diff.tv_sec = 0;
  diff.tv_usec = usec;
  gettimeofday(&tv, NULL);

  BK_TV_ADD(&tv,&tv,&diff);

  BK_RETURN(B, bk_run_enqueue(B, run, tv, event, opaque, handle, 0));
}



/**
 * Set up a reoccurring event at a periodic interval.
 *
 *	@param B BAKA thread/global state 
 *	@param run The baka run environment state
 *	@param usec The number of microseconds until the event should fire
 *	@param event The handler to fire when the time comes (or we are destroyed).
 *	@param opaque The opaque data for the handler
 *	@param handle A copy-out parameter to allow someone to dequeue the event in the future
 *	@param flags Flags for the Future.
 *	@return <0 on call failure, allocation failure, or other error.
 *	@return 0 on success.
 */
int bk_run_enqueue_cron(bk_s B, struct bk_run *run, time_t usec, void (*event)(bk_s B, struct bk_run *run, void *opaque, struct timeval starttime, bk_flags flags), void *opaque, void **handle, bk_flags flags)
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

  brec->brec_interval = usec;
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
 *	@param handle A copy-out parameter to allow someone to dequeue the event in the future
 *	@param flags Flags for the Future.
 *	@return <0 on call failure, or other error.
 *	@return 0 on success.
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
 *	@return <0 on call failure, or other error.
 *	@return 0 on success.
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

  while (BK_FLAG_ISCLEAR(run->br_flags, BK_RUN_FLAG_RUN_OVER))
  {
    if ((ret = bk_run_once(B, run, flags)) < 0)
      break;
  }

  BK_RETURN(B, ret);
}



/**
 * Run through all events once.
 *
 *	@param B BAKA thread/global state 
 *	@param run The baka run environment state
 *	@param flags Flags for the Future.
 *	@return <0 on call failure, or other error.
 *	@return 0 on success.
 */
int bk_run_once(bk_s B, struct bk_run *run, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  fd_set readset, writeset, xcptset;
  struct timeval curtime, deltaevent, zero, deltapoll;
  struct timeval *selectarg;
  int ret;
  int x;
  int use_deltapoll, check_idle;

  if (!run)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  zero.tv_sec=0;
  zero.tv_usec=0;

  gettimeofday(&curtime, NULL);

  check_idle=0;
  use_deltapoll=0;

  if (br_beensignaled) goto beensignaled;

  if (BK_FLAG_ISSET(run->br_flags, BK_RUN_FLAG_CHECK_DEMAND))
  {
    struct bk_run_ondemand_func *brof;
    for(brof=brfl_minimum(run->br_ondemand_funcs);
	brof;
	brof=brfl_successor(run->br_ondemand_funcs,brof))
    {
      if (*brof->brof_demand && (*brof->brof_fun)(B, run, brof->brof_opaque, brof->brof_demand, curtime, 0)<0)
      {
	bk_error_printf(B, BK_ERR_WARN, "The on demand procedure failed severely.\n");
	BK_RETURN(B, -1);
      }
    }
  }

  if (br_beensignaled) goto beensignaled;

  // Run polling activities
  if (BK_FLAG_ISSET(run->br_flags,BK_RUN_FLAG_NEED_POLL))
  {
    struct bk_run_func *brfn;
    struct timeval tmp_deltapoll;
    for(brfn=brfl_minimum(run->br_poll_funcs);
	brfn;
	brfn=brfl_successor(run->br_poll_funcs,brfn))
    {
      if ((ret=(*brfn->brfn_fun)(B, run, brfn->brfn_opaque, curtime, &tmp_deltapoll,0))<0)
      {
	bk_error_printf(B, BK_ERR_WARN, "The polling procedure failed severely.\n");
	BK_RETURN(B, -1);
      }

      if (ret == 1)
      {
	if (!use_deltapoll || (BK_TV2F(&tmp_deltapoll) < BK_TV2F(&deltapoll)))
	{
	  deltapoll=tmp_deltapoll;
	}
	use_deltapoll=1;
      }
    }
  }

  if (br_beensignaled) goto beensignaled;

  // Check for event queue
  if ((ret = bk_run_checkeventq(B, run, curtime, &deltaevent)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "The event queue checking procedure failed severely.\n");
    BK_RETURN(B, -1);
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
    if (use_deltapoll && BK_TV2F(&deltapoll) < BK_TV2F(&deltaevent))
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
      (!selectarg || selectarg->tv_sec != 0 || selectarg->tv_usec != 0))
  {
    selectarg->tv_sec = selectarg->tv_usec = 0;
    check_idle=1;
  }

  readset = run->br_readset;
  writeset = run->br_writeset;
  xcptset = run->br_xcptset;

  if (br_beensignaled) goto beensignaled;

  // Wait for I/O or timeout
  if ((ret = select(run->br_selectn, &readset, &writeset, &xcptset, selectarg)) < 0)
  {
    if (errno != EINTR)
    {
      bk_error_printf(B, BK_ERR_ERR, "Select failed severely: %s\n", strerror(errno));
      BK_RETURN(B, -1);
    }
  }

  // Time may have changed drasticly during select
  gettimeofday(&curtime, NULL);

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

      if (type)
      {
	struct bk_run_fdassoc *curfd;

	ret--;

	if (!(curfd = fdassoc_search(run->br_fdassoc, &x)))
	{
	  bk_error_printf(B, BK_ERR_WARN, "Could not find fd %d in association, yet type is %x\n",x,type);
	  continue;
	}
	(*curfd->brf_handler)(B, run, x, type, curfd->brf_opaque, curtime);
      }
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
	if ((*brfn->brfn_fun)(B, run, brfn->brfn_opaque, curtime, NULL, 0)<0)
	{
	  bk_error_printf(B, BK_ERR_WARN, "The polling procedure failed severely.\n");
	  BK_RETURN(B, -1);
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

	(*run->br_handlerlist[x].brs_handler)(B, run, x, run->br_handlerlist[x].brs_opaque, curtime);
      }
    }
  }

  BK_RETURN(B, 0);
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
 *	@return <0 on call failure, or other error.
 *	@return 0 on success.
 */
int bk_run_handle(bk_s B, struct bk_run *run, int fd, void (*handler)(bk_s B, struct bk_run *run, u_int fd, u_int gottypes, void *opaque, struct timeval starttime), void *opaque, u_int wanttypes, bk_flags flags)
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

  BK_RETURN(B, 0);

 error:
  if (new)
  {
    free(new);
  }
  BK_RETURN(B, -1);
}



/**
 * Specify that we no longer wish bk_run to take care of file descriptors
 *
 *	@param B BAKA thread/global state 
 *	@param run The baka run environment state
 *	@param fd The file descriptor which should be monitored
 *	@param flags Flags for the Future.
 *	@return <0 on call failure, or other error.
 *	@return 0 on success.
 */
int bk_run_close(bk_s B, struct bk_run *run, int fd, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_run_fdassoc *curfda;

  if (!run)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!(curfda = fdassoc_search(run->br_fdassoc, &fd)))
    {
      bk_error_printf(B, BK_ERR_WARN, "Could not find fd %d in association while attempting to delete\n",fd);
      BK_RETURN(B, 0);
    }

  // Get rid of event in list, which will also prevent double deletion
  fdassoc_delete(run->br_fdassoc, curfda);

  // Optionally tell user handler that he will never be called again.
  if (BK_FLAG_ISSET(flags, BK_RUN_FLAG_NOTIFYANYWAY))
    {
      struct timeval curtime;
      gettimeofday(&curtime, NULL);
      (*curfda->brf_handler)(B, run, fd, BK_RUN_CLOSE, curfda->brf_opaque, curtime);
    }

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

  BK_RETURN(B, 0);
}



/**
 * Find out what read/write/xcpt desires are current for a given fd.
 *
 *	@param B BAKA thread/global state 
 *	@param run The baka run environment state
 *	@param fd The file descriptor which should be monitored
 *	@param flags Flags for the Future.
 *	@return (u_int)-1 on error
 *	@return BK_RUN_WANT* bitmap on success
 */
u_int bk_run_getpref(bk_s B, struct bk_run *run, int fd, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  u_int type = 0;

  if (!run && fd >= 0)
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
 *	@return -1 on error
 *	@return 0 on success
 */
int bk_run_setpref(bk_s B, struct bk_run *run, int fd, u_int wanttypes, u_int wantmask, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  u_int oldtype = 0;

  if (!run && fd >= 0)
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



/*
 * Event priority queue comparison routines
 */
static int bk_run_event_comparator(struct br_equeue *a, struct br_equeue *b)
{
  return(BK_TV_CMP(&a->bre_when,&b->bre_when));
}



/*
 * Signal handler for synchronous signal--glue beween OS and libbk
 *
 * <TODO>Worry about locking!</TODO>
 * <TODO>Autoconf for multiple signal prototypes</TODO>
 */
static void bk_run_signal_ihandler(int signum)
{
  // <TODO>Worry about locking!</TODO>
  if (br_signums)
  {
    br_beensignaled = 1;
    (*br_signums)[signum]++;
  }
}



/*
 * The internal event which implements cron functionality on top of
 * the normal once-only event queue methodology
 */
static void bk_run_event_cron(bk_s B, struct bk_run *run, void *opaque, struct timeval starttime, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct br_equeuecron *brec = opaque;

  if (!run || !opaque)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (BK_FLAG_ISCLEAR(flags,BK_RUN_DESTROY))
    bk_run_enqueue_delta(B, run, brec->brec_interval, bk_run_event_cron, brec, ((void **)&brec->brec_equeue), 0);

  (*brec->brec_event)(B, run, brec->brec_opaque, starttime, flags);

  BK_VRETURN(B);
}



/*
 * Execute all pending event queue events, return delta time to next event via *delta.
 * Return <0 on error, ==0 if no next event, >0 otherwise.
 */
static int bk_run_checkeventq(bk_s B, struct bk_run *run, struct timeval starttime, struct timeval *delta)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret = 0;
  struct br_equeue *top;
  struct timeval curtime;

  if (!run)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, -1);
  }

  while (top = pq_head(run->br_equeue))
  {
    if (BK_TV_CMP(&top->bre_when, &starttime) > 0)
      break;  

    top = pq_extract_head(run->br_equeue);

    (*top->bre_event)(B, run, top->bre_opaque, starttime, 0);
    free(top);
  }

  if (!top)
    BK_RETURN(B, 0);

  if (delta)
    {
      /* Use the actual time to next event to allow for more accurate events */
      gettimeofday(&curtime, NULL);

      BK_TV_SUB(delta,&top->bre_when,&starttime);
      if (delta->tv_sec < 0 || delta->tv_usec < 0)
	{
	  delta->tv_sec = 0;
	  delta->tv_usec = 0;
	}

      if (!(ret = delta->tv_sec))
	ret = 1;
    }


  BK_RETURN(B, ret);
}



/** 
 * Add a function for bk_run polling.
 *	@param B BAKA thread/global state 
 * 	@param run bk_run structure pointer
 *	@param fun function to call
 * 	@param opaque data for fun call
 * 	@param handle handle to use to remove this fun
 * 	@return -1 on failure
 * 	@return 0 on success
 */
int
bk_run_poll_add(bk_s B, struct bk_run *run, int (*fun)(bk_s B, struct bk_run *run, void *opaque, struct timeval starttime, struct timeval *delta, bk_flags flags), void *opaque, void **handle)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_run_func *brfn=NULL;

  if (!fun)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (handle) *handle=NULL;

  if (!brfn_alloc(B))
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
 * 	@return -1 on failure
 * 	@return 0 on success
 */
int
bk_run_idle_add(bk_s B, struct bk_run *run, int (*fun)(bk_s B, struct bk_run *run, void *opaque, struct timeval starttime, struct timeval *delta, bk_flags flags), void *opaque, void **handle)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_run_func *brfn=NULL;

  if (!fun)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (handle) *handle=NULL;

  if (!brfn_alloc(B))
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



/*
 * Allocate a brf
 */
static struct bk_run_func *
brfn_alloc(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_run_func *brfn;

  BK_MALLOC(brfn);
  if (!brfn)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate brf: %s\n", strerror(errno));
    BK_RETURN(B, NULL);
  }

  brfn->brfn_key=brfn;

  BK_RETURN(B,brfn);
}



/*
 * Destroy a brf
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
 * 	@return -1 on failure
 * 	@return 0 on success
 */
int
bk_run_on_demand_add(bk_s B, struct bk_run *run, int (*fun)(bk_s B, struct bk_run *run, void *opaque, volatile int *demand, struct timeval starttime, bk_flags flags), void *opaque, volatile int *demand, void **handle)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_run_ondemand_func *brof=NULL;

  if (!fun)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  if (handle) *handle=NULL;

  if (!brof_alloc(B))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate brf: %s\n", strerror(errno));
    goto error;
  }
  
  brof->brof_fun=fun;
  brof->brof_opaque=opaque;
  brof->brof_demand=demand;
  brof->brof_flags=0;

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
 *	@param B BAKA thread/global state 
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

  if (!brfl_minimum(run->br_ondemand_funcs))
  {
    BK_FLAG_CLEAR(run->br_flags,BK_RUN_FLAG_CHECK_DEMAND);
  }
  BK_RETURN(B, 0);
}



/*
 * Allocate a brf
 */
static struct bk_run_ondemand_func *
brof_alloc(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_run_ondemand_func *brof;

  BK_MALLOC(brof);
  if (!brof)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate brf: %s\n", strerror(errno));
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B,brof);
}



/*
 * Destroy a brf
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

static int brfl_oo_cmp(struct bk_run_func *a, struct bk_run_func *b)
{
  return ((char *)(a->brfn_key) - (char *)(b->brfn_key));
}
static int brfl_ko_cmp(void *a, struct bk_run_func *b)
{
  return ((char *)a)-((char *)b->brfn_key);
}

static int brofl_oo_cmp(struct bk_run_ondemand_func *a, struct bk_run_ondemand_func *b)
{
  return ((char *)(a->brof_key) - (char *)(b->brof_key));
}
static int brofl_ko_cmp(void *a, struct bk_run_ondemand_func *b)
{
  return ((char *)a)-((char *)b->brof_key);
}

