#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2008-2011";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2008-2011 The Authors. All rights reserved.
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
 * Random useful string functions
 */

#include <libbk.h>
#include "libbk_internal.h"

/*
 * This file exists SOLELY because Inusre can't deal with this
 * function. The reasons for this are very much unclear as it seems to deal
 * with other functions very similiar to it just fine. With this one,
 * however, there seems to be trouble with both the function prototype and
 * the function body. At any rate, this function is exerpted because when
 * Insure can't instrument a function, it stops intrumenting the *entire8
 * file. And there are few files where you want Insure's guards *more* than
 * b_string.c. So this file with its one function is not expected to pass
 * Insure's instrumentation, but that's OK.
 */

/**
 * Allocate a string based on a printf like format. This algorithm does
 * waste some space. Worst case (size-1), expected case ((size-1)/2) (or
 * something like that. User must free space with free(3).
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param chunk Chunk size to use (0 means user the default)
 *	@param flags Flags.
 *		BK_STRING_ALLOC_SPRINTF_FLAG_STINGY_MEMORY
 *	@param fmt The format string to use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a malloc'ed <i>string</i> on success.
 */
char *
bk_string_alloc_sprintf(bk_s B, u_int chunk, bk_flags flags, const char *fmt, ...)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *ret = NULL;
  va_list ap;

  va_start(ap, fmt);
  ret = bk_string_alloc_vsprintf(B, chunk, flags, fmt, ap);
  va_end(ap);

  BK_RETURN(B, ret);
}
