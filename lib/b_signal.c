#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_signal.c,v 1.3 2001/11/28 18:24:09 seth Exp $";
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
 *	@return <i>-1</i> Failed to set signal handler
 *	@return <br><i>0</i> Set signal handler
 */
int bk_signal(bk_s B, int signo, bk_sighandler handler, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"libbk");
  struct sigaction new;
  void *ret;

  memset(&new, 0, sizeof(new));

  new.sa_handler = handler;
  sigemptyset(&new.sa_mask);
  new.sa_flags = BK_FLAG_ISSET(flags, BK_RUN_SIGNAL_RESTART)?SA_RESTART:0;

  if (sigaction(signo, &new, NULL) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not change signal handler for %d: %s\n",signo,strerror(errno));
    BK_RETURN(B, -1);
  }

  BK_RETURN(B, 0);
}


// <TODO> write bk_signal_manage (which returns old signal handler for saving) </TODO>


