#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2006-2008";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2006-2008 The Authors. All rights reserved.
 *
 * This source code is licensed to you under the terms of the file
 * LICENSE.TXT in this release for further details.
 *
 * Send e-mail to <projectbaka@baka.org> for further information.
 *
 * - -Copyright BAKA- -
 */

/**
 * @file
 * Functions for handling integers with overflow protection
 */

#include <libbk.h>
#include "libbk_internal.h"

/**
 * Initialize a bigint structure
 *
 *	@param B BAKA thread/global state.
 *	@param bb The bigint struct to use
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_bigint_init(bk_s B, struct bk_bigint *bb, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bb)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  bb->bb_cur_uint = 0;
  bb->bb_curval = 0;

  BK_RETURN(B, 0);
}



/**
 * Accumulate a value from u_int "new values" with u_int overflow
 * detection.  The input is a sequence of absolute u_int values,
 * the accumulation will continue to count upwards even when the input
 * u_int wraps.
 *
 *	@param B BAKA thread/global state.
 *	@param bb The bigint structure to use.
 *	@param val The absolute values to accumulate.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_bigint_accumulate(bk_s B, struct bk_bigint *bb, u_int val, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bb)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  // Shortcut success
  if (val == bb->bb_cur_uint)
    BK_RETURN(B, 0);

  // Detect wraps
  if (val < bb->bb_cur_uint)
  {
    bb->bb_curval += (u_int64_t)UINT_MAX - (u_int64_t)bb->bb_cur_uint;
    bb->bb_cur_uint = 0;
  }

  // Handle increments
  bb->bb_curval += val - bb->bb_cur_uint;

  bb->bb_cur_uint = val;
  BK_RETURN(B, 0);
}



/**
 * Return the current bigint value.
 *
 *	@param B BAKA thread/global state.
 *	@param bb The bigint structure to use.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
u_int64_t
bk_bigint_value(bk_s B, struct bk_bigint *bb, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  // This is a little bogus, returning a (u_int64_t_)-1, but who's going to check anyway?
  if (!bb)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, (u_int64_t)-1);
  }

  BK_RETURN(B, bb->bb_curval);
}
