#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_cksum.c,v 1.2 2001/09/08 19:13:52 seth Exp $";
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

#define ADDCARRY(x)  (x > 65535 ? x -= 65535 : x)
#define REDUCE {l_util.l = sum; sum = l_util.s[0] + l_util.s[1]; ADDCARRY(sum);}



/*
 * Checksum routine for Internet Protocol family headers (Portable Version).
 *
 * This routine is very heavily used in the network
 * code and should be modified for each CPU to be as fast as possible.
 *
 * This function is from Net4 BSD, and is NOT licensed under LGPL.
 */
int bk_in_cksum(register struct bk_vptr **m, register int len)
{
  register u_short *w;
  register int sum = 0;
  register int mlen = 0;
  int byte_swapped = 0;

  union
  {
    char    c[2];
    u_short s;
  } s_util;

  union
  {
    u_short s[2];
    long    l;
  } l_util;

  for (;m && len; m++)
  {
    if ((*m)->len == 0)
      break;
    w = (*m)->ptr;

    if (mlen == -1)
    {
      /*
       * The first byte of this mbuf is the continuation
       * of a word spanning between this mbuf and the
       * last mbuf.
       *
       * s_util.c[0] is already saved when scanning previous 
       * mbuf.
       */
      s_util.c[1] = *(char *)w;
      sum += s_util.s;
      w = (u_short *)((char *)w + 1);
      mlen = (*m)->len - 1;
      len--;
    }
    else
      mlen = (*m)->len;

    if (len < mlen)
      mlen = len;
    len -= mlen;

    /*
     * Force to even boundary.
     */
    if ((1 & (int) w) && (mlen > 0))
    {
      REDUCE;
      sum <<= 8;
      s_util.c[0] = *(u_char *)w;
      w = (u_short *)((char *)w + 1);
      mlen--;
      byte_swapped = 1;
    }

    /*
     * Unroll the loop to make overhead from
     * branches &c small.
     */
    while ((mlen -= 32) >= 0)
    {
      sum += w[0]; sum += w[1]; sum += w[2]; sum += w[3];
      sum += w[4]; sum += w[5]; sum += w[6]; sum += w[7];
      sum += w[8]; sum += w[9]; sum += w[10]; sum += w[11];
      sum += w[12]; sum += w[13]; sum += w[14]; sum += w[15];
      w += 16;
    }
    mlen += 32;

    while ((mlen -= 8) >= 0)
    {
      sum += w[0]; sum += w[1]; sum += w[2]; sum += w[3];
      w += 4;
    }
    mlen += 8;

    if (mlen == 0 && byte_swapped == 0)
      continue;
    REDUCE;

    while ((mlen -= 2) >= 0)
    {
      sum += *w++;
    }

    if (byte_swapped)
    {
      REDUCE;
      sum <<= 8;
      byte_swapped = 0;
      if (mlen == -1)
      {
	s_util.c[1] = *(char *)w;
	sum += s_util.s;
	mlen = 0;
      }
      else
	mlen = -1;
    }
    else
      if (mlen == -1)
	s_util.c[0] = *(char *)w;
  }

  if (len)
    printf("cksum: out of data\n");

  if (mlen == -1)
  {
    /* The last mbuf has odd # of bytes. Follow the
       standard (the odd byte may be shifted left by 8 bits
       or not as determined by endian-ness of the machine) */
    s_util.c[1] = 0;
    sum += s_util.s;
  }

  REDUCE;
  return (~sum & 0xffff);
}
