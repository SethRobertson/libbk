#if !defined(lint) && !defined(__INSIGHT__)
#include "libbk_compiler.h"
UNUSED static const char libbk__rcsid[] = "$Id: b_signal.c,v 1.14 2004/07/08 04:40:17 lindauer Exp $";
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
 * Provide standard API for allowing users to set up async signal handlers
 */

#include <libbk.h>
#include "libbk_internal.h"

/*
 * <TODO>bk_signal probably needs an API change, so that certain signal flags,
 * like SA_SIGINFO (perhaps SA_NODEFER, SA_NOCLDWAIT too) are preserved
 * correctly when restoring an original signal handler.</TODO>>
 */


#define BK_SIGNAL_PRELOCKED	0x8000		///< Tell core routines no need to relock


/**
 * Stuff to save when temporarily seizing control of an alarm.
 */
struct bk_signal_saved
{
  bk_flags		bss_flags;		///< Everyone needs flags,
#define BK_SIGNAL_SAVED_FLAG_ACT_SAVED	0x1	///< We have actually copied what we want to.
  int			bss_signal;		///< signo we're dealing wth
  struct sigaction	bss_sigact;		///< Saved sigaction
};



#ifdef BK_USING_PTHREADS
pthread_mutex_t BkGlobalSignalLock = PTHREAD_MUTEX_INITIALIZER;		///< Prevent threads from trashing during signal management (note that signals and threads don't mix well anyhow)
#endif /* BK_USING_PTHREADS */



static struct bk_signal_saved *bss_create(bk_s B);
static void bss_destroy(bk_s B, struct bk_signal_saved *bss);



/**
 * Install a signal handler.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state
 *	@param signo Signal number
 *	@param handler Signal handler
 *	@param flags Flags--BK_RUN_SIGNAL_INTR to interrupt,
 *		BK_RUN_SIGNAL_RESTART to restart.  INTR is the default.
 *
 *	@return <i>-1</i> Failed to set signal handler
 *	@return <br><i>0</i> Set signal handler
 */
int bk_signal(bk_s B, int signo, bk_sighandler_f handler, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"libbk");
  struct sigaction new;
  int ret = 0;

  memset(&new, 0, sizeof(new));

  new.sa_handler = handler;
  sigemptyset(&new.sa_mask);
  new.sa_flags = BK_FLAG_ISSET(flags, BK_SIGNAL_FLAG_RESTART)?SA_RESTART:0;

#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISCLEAR(flags, BK_SIGNAL_PRELOCKED) && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&BkGlobalSignalLock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (sigaction(signo, &new, NULL) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not change signal handler for %d: %s\n",signo,strerror(errno));
    ret = -1;
  }

#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISCLEAR(flags, BK_SIGNAL_PRELOCKED) && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&BkGlobalSignalLock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B, ret);
}



/**
 * Manage signal handlers. Install new and/or get current value. If @a new
 * is set, install @a new. If @a old is set return current.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param sig Signal to manage.
 *	@param new New hander to install (may be NULL).
 *	@param old Copyout pointer to fill in with old hander.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_signal_mgmt(bk_s B, int sig, bk_sighandler_f new, bk_sighandler_f *old, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"libbk");
  struct sigaction newa, olda;
  int ret;

  /* Yes this I mean && not || */
  if (!new && !old)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISCLEAR(flags, BK_SIGNAL_PRELOCKED) && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&BkGlobalSignalLock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (new)
  {
    memset(&new, 0, sizeof(new));
    newa.sa_handler = new;
    if (BK_FLAG_ISSET(flags, BK_SIGNAL_FLAG_RESTART))
      BK_FLAG_SET(newa.sa_flags, SA_RESTART);
    ret = sigaction(sig, &newa, &olda);
  }
  else
  {
    ret = sigaction(sig, NULL, &olda);
  }

#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISCLEAR(flags, BK_SIGNAL_PRELOCKED) && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&BkGlobalSignalLock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (ret < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set signal action: %s\n", strerror(errno));
    goto error;
  }

  if (old)
    *old = olda.sa_handler;

  BK_RETURN(B,0);

 error:
  BK_RETURN(B,-1);

}




/**
 * Safely set up a signal handler.
 *
 * Unless @a BK_SIGNAL_SET_SIGNAL_FLAG_FORCE is on, this function will
 * abort if the current signal handler for signo is not one of the accepted
 * defaults. It returns an opaque thingy which should be destroyed by
 * calling bk_signal_reset(). <em>NB</em> This design of this function
 * should make it clear that this is not intended for temporary (or quick)
 * alarms only (since resetting is required). You may use it anyway you
 * wish, of course, but <i>caveat emptor</i>
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param signo The signal to take over.
 *	@param handler The handler to install.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>opaque</i> thingy to offer to @a bk_signal_reset()
 */
void *
bk_signal_set(bk_s B, int signo, bk_sighandler_f handler, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"libbk");
  struct sigaction old;
  sigset_t oldmask;
  sigset_t newmask;
  int setmask = 0;
  bk_sighandler_f old2;
  struct bk_signal_saved *bss;


  if (!(bss = bss_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create bk_signal_saved\n");
    BK_RETURN(B, NULL);
  }

  /* Block out alarm while figuring out what to do */
  sigemptyset(&newmask);
  sigaddset(&newmask, signo);

#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISCLEAR(flags, BK_SIGNAL_PRELOCKED) && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&BkGlobalSignalLock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  sigprocmask(SIG_BLOCK, &newmask, &oldmask);
  setmask = 1;

  if (sigaction(signo, NULL, &old) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not retrieve current alarm action\n");
    goto error;
  }

  if (!(old.sa_handler == SIG_DFL ||
	old.sa_handler == SIG_IGN ||
	old.sa_handler == bk_run_signal_ihandler ||
	BK_FLAG_ISSET(flags, BK_SIGNAL_SET_SIGNAL_FLAG_FORCE)))
  {
    goto done;
  }

  bss->bss_signal = signo;
  BK_FLAG_SET(bss->bss_flags, BK_SIGNAL_SAVED_FLAG_ACT_SAVED);
  memmove(&bss->bss_sigact, &old, sizeof(old));

  /* Release SIGALARM, just in case we've held one */
  sigprocmask(SIG_SETMASK, &oldmask, NULL);
  setmask = 0;

  if (bk_signal_mgmt(B, signo, handler, &old2, flags|BK_SIGNAL_PRELOCKED) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not install new alarm handler\n");
    goto error;
  }

 done:
  if (setmask) sigprocmask(SIG_SETMASK, &oldmask, NULL);

#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISCLEAR(flags, BK_SIGNAL_PRELOCKED) && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&BkGlobalSignalLock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B,bss);

 error:
  if (setmask) sigprocmask(SIG_SETMASK, &oldmask, NULL);

#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISCLEAR(flags, BK_SIGNAL_PRELOCKED) && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&BkGlobalSignalLock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (bss) bk_signal_reset(B, bss, 0);
  BK_RETURN(B,NULL);
}



/**
 * Set an alarm for @a secs seconds in the future.  Note anyone
 * calling this function before the alarm fires may cause the person
 * who generated this call to be unhappy since it does not restore
 * previously handlers/alarms.
 *
 * THREADS: MT-SAFE (technically, but see note above)
 *
 *	@param B BAKA thread/global state.
 *	@param secs When the alarm should go off.
 *	@param handler Function to call when alarm goes off.
 *	@param flags Flag s for the future.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>opaque</i> thingy to offer to @a bk_signal_reset()
 */
void *
bk_signal_set_alarm(bk_s B, u_int secs, bk_sighandler_f handler, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"libbk");
  int setmask = 0;
  sigset_t oldmask;
  sigset_t newmask;
  u_int oldtime = 0;
  struct bk_signal_saved *bss;

  /* Sigh... we have to block the signal for the whole time */
  sigemptyset(&newmask);
  sigaddset(&newmask, SIGALRM);

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&BkGlobalSignalLock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  sigprocmask(SIG_BLOCK, &newmask, &oldmask);
  setmask = 1;

  /* Insouciant reuse of a flag here, but what the hell */
  if ((oldtime=alarm(0)) && BK_FLAG_ISCLEAR(flags, BK_SIGNAL_SET_SIGNAL_FLAG_FORCE))
  {
    bk_error_printf(B, BK_ERR_ERR, "Pending alarm exists. Aborting this set\n");
    goto error;
  }

  if (!(bss=bk_signal_set(B, SIGALRM, handler, flags|BK_SIGNAL_PRELOCKED)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Coudl not set SIGALRM handler\n");
    goto error;
  }

  alarm(secs);

  sigprocmask(SIG_SETMASK, &oldmask, NULL);
  setmask = 0;					// protect against future code

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&BkGlobalSignalLock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B,bss);

 error:
  if (setmask) sigprocmask(SIG_SETMASK, &oldmask, NULL);
  if (oldtime) alarm(oldtime);
  BK_RETURN(B,NULL);
}




/**
 * Reset an alarm set with @a bk_signal_set_signal
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param args The opaque value returned from @a bk_signal_set_signal.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_signal_reset(bk_s B, void *args, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"libbk");
  struct bk_signal_saved *bss=args;
  int setmask = 0;
  sigset_t newmask;
  sigset_t oldmask;
  bk_sighandler_f old;

  if (!bss)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (BK_FLAG_ISCLEAR(bss->bss_flags, BK_SIGNAL_SAVED_FLAG_ACT_SAVED))
  {
    /* We have no sigaction saved, so there's nothing we can do. */
    BK_RETURN(B,0);
  }

  sigemptyset(&newmask);
  sigaddset(&newmask, bss->bss_signal);

#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISCLEAR(flags, BK_SIGNAL_PRELOCKED) && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&BkGlobalSignalLock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  sigprocmask(SIG_BLOCK, &newmask, &oldmask);
  setmask = 1;

  if (bk_signal_mgmt(B, bss->bss_signal, NULL, &old, BK_SIGNAL_PRELOCKED) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not get current signo action\n");
    goto error;
  }

  if (old != bss->bss_sigact.sa_handler)
  {
    bk_error_printf(B, BK_ERR_ERR, "Alarm handler was not what expected abort reset\n");
    goto error;
  }

  /* Release held signal */
  sigprocmask(SIG_SETMASK, &oldmask, NULL);
  setmask = 0;

  if (bk_signal_mgmt(B, bss->bss_signal, bss->bss_sigact.sa_handler, NULL, BK_SIGNAL_PRELOCKED) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not reset signo action\n");
    goto error;
  }

#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISCLEAR(flags, BK_SIGNAL_PRELOCKED) && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&BkGlobalSignalLock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  bss_destroy(B, bss);

  BK_RETURN(B,0);

 error:
  if (setmask) sigprocmask(SIG_SETMASK, &oldmask, NULL);

#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISCLEAR(flags, BK_SIGNAL_PRELOCKED) && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&BkGlobalSignalLock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B,-1);
}



/**
 * Reset an alarm set with @a bk_signal_set_signal
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_signal_reset_alarm(bk_s B, void *args, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"libbk");
  int setmask = 0;
  sigset_t newmask;
  sigset_t oldmask;

  if (!args)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  sigemptyset(&newmask);
  sigaddset(&newmask, SIGALRM);

#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISCLEAR(flags, BK_SIGNAL_PRELOCKED) && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&BkGlobalSignalLock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  sigprocmask(SIG_BLOCK, &newmask, &oldmask);
  setmask = 1;

  if (bk_signal_reset(B, args, flags|BK_SIGNAL_PRELOCKED) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not reset alarm\n");
    goto error;
  }

  /*
   * Since we know that reset our *own* handler and SIGALRM has been
   * blocked it's almost 100% certain that this is OK (ie that someone has
   * not sneaked in an alarm of his own which we are now nuking.
   */
  alarm(0);

  sigprocmask(SIG_SETMASK, &oldmask, NULL);
  setmask = 0;					// protect against future code

#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISCLEAR(flags, BK_SIGNAL_PRELOCKED) && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&BkGlobalSignalLock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B,0);

 error:
  if (setmask) sigprocmask(SIG_SETMASK, &oldmask, NULL);

#ifdef BK_USING_PTHREADS
  if (BK_FLAG_ISCLEAR(flags, BK_SIGNAL_PRELOCKED) && BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&BkGlobalSignalLock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B,-1);
}



/**
 * Sigchild reaper signal handler
 *
 * THREADS: MT-SAFE
 *
 *	@param signal Signal number
 */
void bk_reaper(int signum)
{
  // Reap 'dem children
  while (waitpid(-1, NULL, WNOHANG|WUNTRACED) > 0)
    ;
}



/**
 * Create a ba
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a bk_signal_saved on success.
 */
static struct bk_signal_saved *
bss_create(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"libbk");
  struct bk_signal_saved *bss;

  if (!(BK_CALLOC(bss)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bk_signal_saved: %s\n", strerror(errno));
    BK_RETURN(B,NULL);
  }

  BK_RETURN(B,bss);
}




/**
 * Destroy a @a bk_signal_saved.
 *
 * THREADS: MT-SAFE (assuming different ba)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param ba The @a bk_signal_saved to destroy.
 */
static void
bss_destroy(bk_s B, struct bk_signal_saved *bss)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"libbk");

  if (!bss)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  free(bss);
  BK_VRETURN(B);
}


#ifdef __INSURE__
/*
 * Insure gets really irritated with ((__sighandler_t)1) (which doesn't
 * bother gcc at all), so we shut it up by make SIG_IGN not *really*
 * ignored, but rather handled by a function which does nothing.
 */
void
bk_sig_ign(int signal)
{
  return;
}
#endif /* __INSURE__ */
