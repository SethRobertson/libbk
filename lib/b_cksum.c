#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2001-2008";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2001-2008 The Authors. All rights reserved.
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
 * Checksum routine(s).
 */

#include <libbk.h>



#define ADDCARRY(x)  (x > 65535 ? x -= 65535 : x)
#define REDUCE do {l_util.l = sum; sum = l_util.s[0] + l_util.s[1]; ADDCARRY(sum);} while(0)



/**
 * Checksum routine for Internet Protocol family headers (Portable Version).
 *
 * This routine is very heavily used in the network
 * code and should be modified for each CPU to be as fast as possible.
 *
 * This function is from Net4 BSD, and is NOT licensed under LGPL.
 *
 * THREADS: MT-SAFE
 *
 *	@param m Null terminated array of vectored pointers to data
 *	@param len Length of data in m that should be checksummed
 *	@return 16-bit <i>checksum</i> of data
 */
int bk_in_cksum(bk_s B, bk_vptr *m, int len)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  u_short *w;
  int sum = 0;
  int mlen = 0;
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
    if (m->len == 0)
      break;
    w = m->ptr;

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
      mlen = m->len - 1;
      len--;
    }
    else
      mlen = m->len;

    if (len < mlen)
      mlen = len;
    len -= mlen;

    /*
     * Force to even boundary.
     */
    if ((1 & (long)w) && (mlen > 0))
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
    bk_error_printf(B, BK_ERR_ERR, "cksum: out of data\n");

  if (mlen == -1)
  {
    /* The last mbuf has odd # of bytes. Follow the
       standard (the odd byte may be shifted left by 8 bits
       or not as determined by endian-ness of the machine) */
    s_util.c[1] = 0;
    sum += s_util.s;
  }

  REDUCE;
  BK_RETURN(B, ~sum & 0xffff);
}
