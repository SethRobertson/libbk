#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_run.c,v 1.1 2001/10/01 02:46:52 seth Exp $";
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



static int bk_run_event_comparator(struct br_equeue *a, struct br_equeue *b);
static void bk_run_signal_ihandler(int signum);
static void bk_run_event_cron(bk_s B, struct bk_run *run, void *opaque, struct timeval starttime);
static int bk_run_checkeventq(bk_s B, struct bk_run *run, struct timeval starttime, struct timeval *delta);



/*
 * FD association CLC
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
#define fdassoc_nextobj(h)		ht_nextobj(h)
#define fdassoc_error_reason(h,i)	ht_error_reason((h),(i))
static int fa_oo_cmp(struct bk_run_fdassoc *a, struct bk_run_fdassoc *b);
static int fa_ko_cmp(int *a, struct bk_run_fdassoc *b);
static ht_val fa_obj_hash(struct bk_run_fdassoc *a);
static ht_val fa_key_hash(int *a);
static struct ht_args fa_args = { 128, 1, (ht_func)fa_obj_hash, (ht_func)fa_key_hash };




/*
 * Static.  Yes, well, when you have signals which have to affect
 * system state, you have no choice but to use a static or global.
 *
 * This points to the bk_run number of signal received array.
 */
static volatile u_int8_t	(*br_signums)[NSIG];
static volatile int		br_beensignaled;



/*
 * Create and initialize the run environment
 */
struct bk_run *bk_run_init(bk_s B, int (*pollfun)(bk_s B, struct bk_run *run, void *opaque, struct timeval starttime), void *pollopaque, volatile int *demand, int (*ondemand)(bk_s B, struct bk_run *run, void *opaque, volatile int *demand, struct timeval starttime), void *demandopaque, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_run *run;

  if (!(run = malloc(sizeof(*run))))
  {
    bk_error_printf(B, BK_ERR_ERR, "Cannot create run structure: %s\n",strerror(errno));
    BK_RETURN(B, NULL);
  }
  memset(run, 0, sizeof(*run));

  run->br_pollfun = pollfun;
  run->br_pollopaque = pollopaque;
  run->br_ondemandtest = demand;
  run->br_ondemand = ondemand;
  run->br_ondemandopaque = demandopaque;
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

  BK_RETURN(B, run);

 error:
  if (run)
    bk_run_destroy(B, run);

  BK_RETURN(B, NULL);
}



/*
 * Destroy the baka run environment
 */
void bk_run_destroy(bk_s B, struct bk_run *run)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int signum;

  if (!run)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  // Destroy the fd association
  if (run->br_fdassoc)
  {
    struct bk_run_fdassoc *cur;

    while (cur = fdassoc_minimum(run->br_fdassoc))
    {
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
      // Should we notify the event that it is no more?
      free(cur);
    }
  }

  // Reset signal handlers to their default actions
  for(signum = 0;signum < NSIG;signum++)
  {
    bk_run_signal(B, run, signum, NULL, NULL, 0);
  }

  br_signums = NULL;

  free(run);

  BK_VRETURN(B);
}



/*
 * Set (or clear) a synchronous handler for some signal
 *
 * Default is to interrupt system calls
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



/*
 * Enqueue an event for future action
 */
int bk_run_enqueue(bk_s B, struct bk_run *run, struct timeval when, void (*event)(bk_s B, struct bk_run *run, void *opaque, struct timeval starttime), void *opaque, void **handle, bk_flags flags)
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



/*
 * Enqueue an event for a future time
 */
int bk_run_enqueue_delta(bk_s B, struct bk_run *run, time_t usec, void (*event)(bk_s B, struct bk_run *run, void *opaque, struct timeval starttime), void *opaque, void **handle, bk_flags flags)
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



/*
 * Set up a reoccurring event at a periodic interval
 */
int bk_run_enqueue_cron(bk_s B, struct bk_run *run, time_t usec, void (*event)(bk_s B, struct bk_run *run, void *opaque, struct timeval starttime), void *opaque, void **handle, bk_flags flags)
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



/*
 * Dequeue a normal event or a full "cron" job
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



/*
 * Run the event loop until the end
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

  while (BK_FLAG_ISCLEAR(run->br_flags, BK_RUN_RUN_OVER))
  {
    if ((ret = bk_run_once(B, run, flags)) < 0)
      break;
  }

  BK_RETURN(B, ret);
}



/*
 * Run through all events
 */
int bk_run_once(bk_s B, struct bk_run *run, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  fd_set readset, writeset, xcptset;
  struct timeval curtime, deltaevent;
  struct timeval *selectarg;
  int ret;
  u_int x;

  if (!run)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  gettimeofday(&curtime, NULL);

  // Check for ondemand activities
  if (run->br_ondemandtest && *run->br_ondemandtest && (*run->br_ondemand)(B, run, run->br_ondemandopaque, run->br_ondemandtest, curtime) < 0)
  {
    bk_error_printf(B, BK_ERR_WARN, "The ondemand procedure failed severely.\n");
    BK_RETURN(B, -1);
  }
    
  // Run polling activities
  if (run->br_pollfun && (*run->br_pollfun)(B, run, run->br_pollopaque, curtime) < 0)
  {
    bk_error_printf(B, BK_ERR_WARN, "The polling procedure failed severely.\n");
    BK_RETURN(B, -1);
  }

  // Check for event queue
  if ((ret = bk_run_checkeventq(B, run, curtime, &deltaevent)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "The event queue checking procedure failed severely.\n");
    BK_RETURN(B, -1);
  }

  // Figure out the select argument (if no queued event
  if (ret == 0)
    selectarg = NULL;
  else
    selectarg = &deltaevent;

  readset = run->br_readset;
  writeset = run->br_writeset;
  xcptset = run->br_xcptset;

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

  // Are there any I/O events pending?
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
  }

  // Check for synchronous signals to handle
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






/*
 * Event priority queue comparison routines
 */
int bk_run_handle(bk_s B, struct bk_run *run, void (*handler)(bk_s B, struct bk_run *run, u_int fd, u_int gottypes, void *opaque, struct timeval starttime), void *opaque, u_int wanttypes, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!run || !handler)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  BK_RETURN(B, 0);
}



/*
 * Event priority queue comparison routines
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



/*
 * Event priority queue comparison routines
 */
int bk_run_setpref(bk_s B, struct bk_run *run, int fd, u_int wanttypes, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!run && fd >= 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  FD_CLR(fd, &run->br_readset);
  FD_CLR(fd, &run->br_writeset);
  FD_CLR(fd, &run->br_xcptset);

  if (BK_FLAG_ISSET(wanttypes, BK_RUN_WANTREAD))
    FD_SET(fd, &run->br_readset);
  if (BK_FLAG_ISSET(wanttypes, BK_RUN_WANTWRITE))
    FD_SET(fd, &run->br_writeset);
  if (BK_FLAG_ISSET(wanttypes, BK_RUN_WANTXCPT))
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
static void bk_run_event_cron(bk_s B, struct bk_run *run, void *opaque, struct timeval starttime)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct br_equeuecron *brec = opaque;

  if (!run || !opaque)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  bk_run_enqueue_delta(B, run, brec->brec_interval, bk_run_event_cron, brec, ((void **)&brec->brec_equeue), 0);
  (*brec->brec_event)(B, run, brec->brec_opaque, starttime);

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

    (*top->bre_event)(B, run, top->bre_opaque, starttime);
    free(top);
  }

  if (!top)
    BK_RETURN(B, 0);

  if (delta)
    BK_TV_SUB(delta,&top->bre_when,&starttime);

  if (!(ret = delta->tv_sec))
    ret = 1;

  BK_RETURN(B, ret);
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
