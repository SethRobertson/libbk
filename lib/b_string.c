#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_string.c,v 1.2 2001/08/16 21:10:48 seth Exp $";
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



/*
 * Hash a string
 *	
 * The Practice of Programming: Kernighan and Pike: 2.9
 *
 * Note we may not be applying modulus in this function since this is
 * normally used by CLC functions, CLC will supply its own modulus and
 * it is bad voodoo to double modulus if the moduluses are different.
 */
u_int 
bk_strhash(char *a, bk_flags flags)
{
  const u_int M = (unsigned)37U;		/* Multiplier */
  const u_int P = (unsigned)2147486459U;	/* Arbitrary large prime */
  u_int h;

  for (h = 0; *a; a++)
    h = h * M + *a;

  if (flags & BK_STRHASH_NOMODULUS)
    return(h);
  else
    return(h % P);
}



/* printbuf info
 * size + (n/16) * (prefix+61) + strlen(intro) + 10
 * AAA 1234 5678 1234 5678 1234 5678 1234 5678 0123456789abcdef
 *
 */
