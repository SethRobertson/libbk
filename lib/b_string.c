#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_string.c,v 1.1 2001/07/09 07:08:18 jtt Exp $";
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
 *
 * Shamelessly stolen by jtt from code writen by Ugen.
 */
u_int 
bk_strhash(char *a)
{
  const u_int M = (unsigned)37U;		/* Multiplier */
#ifdef MODULUS_IN_BK
  const u_int P = (unsigned)2147486459U;	/* Arbitrary large prime */
#endif /* MODULUS_IN_BK */
  u_int h;

  for (h = 0; *a; a++)
    h = h * M + *a;

#ifdef MODULUS_IN_BK
  return(h % P);
#else /* MODULUS_IN_BK */
  return(h);
#endif /* MODULUS_IN_BK */
}

