#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_signal.c,v 1.2 2001/11/15 02:12:32 dupuy Exp $";
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
 * Provide standard API for allowing users to set up async signal handlers
 */

#include <libbk.h>
#include "libbk_internal.h"

/*
 * <TODO>bk_signal probably needs an API change, so that certain signal flags,
 * like SA_SIGINFO (perhaps SA_NODEFER, SA_NOCLDWAIT too) are preserved
 * correctly when restoring an original signal handler.</TODO>>
 */



/**
 * Install a signal handler.
 *
 *	@param B BAKA Thread/global state
 *	@param signo Signal number
 *	@param handler Signal handler
 *	@param flags Flags--BK_RUN_SIGNAL_INTR to interrupt,
 *		BK_RUN_SIGNAL_RESTART to restart.  INTR is the default.
 *
 *	@return <i>NULL</i> Failed to set signal handler
 *	@return <br><i>Old signal handler</i> on success
 */
bk_sighandler bk_signal(bk_s B, int signo, bk_sighandler handler, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"libbk");
  struct sigaction new, old;
  void *ret;

  memset(&new, 0, sizeof(new));

  new.sa_handler = handler;
  sigemptyset(&new.sa_mask);
  new.sa_flags = BK_FLAG_ISSET(flags, BK_RUN_SIGNAL_RESTART)?SA_RESTART:0;

  if (sigaction(signo, &new, &old) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not change signal handler for %d: %s\n",signo,strerror(errno));
    BK_RETURN(B, NULL);
  }

#ifdef SA_SIGINFO
  /*
   * <WARNING>If SA_SIGINFO is set, we correctly return the signal handler
   * stored in sa_sigaction.  But if you later use bk_signal to restore the
   * original "handler", it will go in sa_handler, SA_SIGINFO will not be set,
   * and the function will get garbage instead of the siginfo_t * it expects.
   * </WARNING>
   */
  if (old.sa_flags & SA_SIGINFO)
    ret = old.sa_sigaction;
  else
#endif /* SA_SIGINFO */
    ret = old.sa_handler;

  if (!ret)
    ret = (void *)10;				// Not very cool.

  BK_RETURN(B, ret);
}

