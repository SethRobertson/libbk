#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_thread.c,v 1.2 2002/11/11 22:53:58 jtt Exp $";
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
 * Random useful thread functions
 */

#include <libbk.h>
#include "libbk_internal.h"
#ifdef BK_USING_PTHREADS


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
#endif /* BK_USING_PTHREADS */
