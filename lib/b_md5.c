#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_md5.c,v 1.1 2002/01/20 03:19:11 seth Exp $";
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
 *
 * MD5 as implemented by RSA Data Security, with minor modification to
 * bkify it to some minimal standard.  Note that the RSA copyright,
 * included below, overrides the standard "libbk" copyright.
 */

#include <libbk.h>
#include "libbk_internal.h"



/*
 ***********************************************************************
 ** md5.c -- the source code for MD5 routines                         **
 ** RSA Data Security, Inc. MD5 Message-Digest Algorithm              **
 ** Created: 2/17/90 RLR                                              **
 ** Revised: 1/91 SRD,AJ,BSK,JT Reference C Version                   **
 ** Revised (for MD5): RLR 4/27/91                                    **
 **   -- G modified to have y&~z instead of y&z                       **
 **   -- FF, GG, HH modified to add in last register done             **
 **   -- Access pattern: round 2 works mod 5, round 3 works mod 3     **
 **   -- distinct additive constant for each step                     **
 **   -- round 4 added, working mod 7                                 **
 ***********************************************************************
 */

/*
 ***********************************************************************
 ** Copyright (C) 1990, RSA Data Security, Inc. All rights reserved.  **
 **                                                                   **
 ** License to copy and use this software is granted provided that    **
 ** it is identified as the "RSA Data Security, Inc. MD5 Message-     **
 ** Digest Algorithm" in all material mentioning or referencing this  **
 ** software or this function.                                        **
 **                                                                   **
 ** License is also granted to make and use derivative works          **
 ** provided that such works are identified as "derived from the RSA  **
 ** Data Security, Inc. MD5 Message-Digest Algorithm" in all          **
 ** material mentioning or referencing the derived work.              **
 **                                                                   **
 ** RSA Data Security, Inc. makes no representations concerning       **
 ** either the merchantability of this software or the suitability    **
 ** of this software for any particular purpose.  It is provided "as  **
 ** is" without express or implied warranty of any kind.              **
 **                                                                   **
 ** These notices must be retained in any copies of any part of this  **
 ** documentation and/or software.                                    **
 ***********************************************************************
 */


/*
 ***********************************************************************
 **  Message-digest routines:                                         **
 **  To form the message digest for a message M                       **
 **    (1) Initialize a context buffer mdContext using MD5Init        **
 **    (2) Call MD5Update on mdContext and M                          **
 **    (3) Call MD5Final on mdContext                                 **
 **  The message digest is now in mdContext->digest[0...15]           **
 ***********************************************************************
 */

/* forward declaration */
static void Transform (u_int32_t *buf, u_int32_t *in);


static const unsigned char PADDING[64] = {
  0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};



/* F, G, H and I are basic MD5 functions */
#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z)))

#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32-(n))))

/* FF, GG, HH, and II transformations for rounds 1, 2, 3, and 4 */
/* Rotation is separate from addition to prevent recomputation */
#define FF(a, b, c, d, x, s, ac) \
  {(a) += F ((b), (c), (d)) + (x) + (u_int32_t)(ac); \
   (a) = ROTATE_LEFT ((a), (s)); \
   (a) += (b); \
  }
#define GG(a, b, c, d, x, s, ac) \
  {(a) += G ((b), (c), (d)) + (x) + (u_int32_t)(ac); \
   (a) = ROTATE_LEFT ((a), (s)); \
   (a) += (b); \
  }
#define HH(a, b, c, d, x, s, ac) \
  {(a) += H ((b), (c), (d)) + (x) + (u_int32_t)(ac); \
   (a) = ROTATE_LEFT ((a), (s)); \
   (a) += (b); \
  }
#define II(a, b, c, d, x, s, ac) \
  {(a) += I ((b), (c), (d)) + (x) + (u_int32_t)(ac); \
   (a) = ROTATE_LEFT ((a), (s)); \
   (a) += (b); \
  }



/**
 * The routine MD5Init initializes the message-digest context
 * mdContext. All fields are set to zero.
 *
 *	@param B BAKA thread/global state
 *	@param mdContext The MD5 state data
 */
void bk_MD5Init (bk_s B, bk_MD5_CTX *mdContext)
{

  if (!mdContext)
  {
    BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  mdContext->i[0] = mdContext->i[1] = (u_int32_t)0;

  /*
   * Load magic initialization constants.
   */
  mdContext->buf[0] = (u_int32_t)0x67452301;
  mdContext->buf[1] = (u_int32_t)0xefcdab89;
  mdContext->buf[2] = (u_int32_t)0x98badcfe;
  mdContext->buf[3] = (u_int32_t)0x10325476;
}



/**
 * The routine MD5Update updates the message-digest context to
 * account for the presence of each of the characters inBuf[0..inLen-1]
 * in the message whose digest is being computed.
 *
 *	@param B BAKA thread/global state
 *	@param mdContext The MD5 state data
 *	@param inBuf The data to be incorporated into the digest
 *	@param inLen The length of the data to be incorporated
 */
void bk_MD5Update(bk_s B, bk_MD5_CTX *mdContext, unsigned char *inBuf, unsigned int inLen)
{
  u_int32_t in[16];
  int mdi;
  unsigned int i, ii;

  if (!mdContext || !inBuf)
  {
    BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  /* compute number of bytes mod 64 */
  mdi = (int)((mdContext->i[0] >> 3) & 0x3F);

  /* update number of bits */
  if ((mdContext->i[0] + ((u_int32_t)inLen << 3)) < mdContext->i[0])
    mdContext->i[1]++;
  mdContext->i[0] += ((u_int32_t)inLen << 3);
  mdContext->i[1] += ((u_int32_t)inLen >> 29);

  while (inLen--)
  {
    /* add new character to buffer, increment mdi */
    mdContext->in[mdi++] = *inBuf++;

    /* transform if necessary */
    if (mdi == 0x40)
    {
      for (i = 0, ii = 0; i < 16; i++, ii += 4)
        in[i] = (((u_int32_t)mdContext->in[ii+3]) << 24) |
	  (((u_int32_t)mdContext->in[ii+2]) << 16) |
	  (((u_int32_t)mdContext->in[ii+1]) << 8) |
	  ((u_int32_t)mdContext->in[ii]);
      Transform (mdContext->buf, in);
      mdi = 0;
    }
  }
}



/**
 * The routine MD5Final terminates the message-digest computation and
 * ends with the desired message digest in mdContext->digest[0...15].
 *
 *	@param B BAKA thread/global state
 *	@param mdContext The MD5 state data
 *	@param inBuf The data to be incorporated into the digest
 *	@param inLen The length of the data to be incorporated
 */
void bk_MD5Final(bk_s B, bk_MD5_CTX *mdContext)
{
  u_int32_t in[16];
  int mdi;
  unsigned int i, ii;
  unsigned int padLen;

  if (!mdContext)
  {
    BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  /* save number of bits */
  in[14] = mdContext->i[0];
  in[15] = mdContext->i[1];

  /* compute number of bytes mod 64 */
  mdi = (int)((mdContext->i[0] >> 3) & 0x3F);

  /* pad out to 56 mod 64 */
  padLen = (mdi < 56) ? (56 - mdi) : (120 - mdi);
  bk_MD5Update (B, mdContext, (unsigned char *)PADDING, padLen);

  /* append length in bits and transform */
  for (i = 0, ii = 0; i < 14; i++, ii += 4)
    in[i] = (((u_int32_t)mdContext->in[ii+3]) << 24) |
      (((u_int32_t)mdContext->in[ii+2]) << 16) |
      (((u_int32_t)mdContext->in[ii+1]) << 8) |
      ((u_int32_t)mdContext->in[ii]);
  Transform (mdContext->buf, in);

  /* store buffer in digest */
  for (i = 0, ii = 0; i < 4; i++, ii += 4)
  {
    mdContext->digest[ii] = (unsigned char)(mdContext->buf[i] & 0xFF);
    mdContext->digest[ii+1] =
      (unsigned char)((mdContext->buf[i] >> 8) & 0xFF);
    mdContext->digest[ii+2] =
      (unsigned char)((mdContext->buf[i] >> 16) & 0xFF);
    mdContext->digest[ii+3] =
      (unsigned char)((mdContext->buf[i] >> 24) & 0xFF);
  }
}



/*
 * Basic MD5 step. Transforms buf based on in.
 */
static void Transform(u_int32_t *buf, u_int32_t *in)
{
  u_int32_t a = buf[0], b = buf[1], c = buf[2], d = buf[3];

  /* Round 1 */
#define S11 7
#define S12 12
#define S13 17
#define S14 22

  FF ( a, b, c, d, in[ 0], S11, 0xd76aa478); /* 1 */
  FF ( d, a, b, c, in[ 1], S12, 0xe8c7b756); /* 2 */
  FF ( c, d, a, b, in[ 2], S13, 0x242070db); /* 3 */
  FF ( b, c, d, a, in[ 3], S14, 0xc1bdceee); /* 4 */
  FF ( a, b, c, d, in[ 4], S11, 0xf57c0faf); /* 5 */
  FF ( d, a, b, c, in[ 5], S12, 0x4787c62a); /* 6 */
  FF ( c, d, a, b, in[ 6], S13, 0xa8304613); /* 7 */
  FF ( b, c, d, a, in[ 7], S14, 0xfd469501); /* 8 */
  FF ( a, b, c, d, in[ 8], S11, 0x698098d8); /* 9 */
  FF ( d, a, b, c, in[ 9], S12, 0x8b44f7af); /* 10 */
  FF ( c, d, a, b, in[10], S13, 0xffff5bb1); /* 11 */
  FF ( b, c, d, a, in[11], S14, 0x895cd7be); /* 12 */
  FF ( a, b, c, d, in[12], S11, 0x6b901122); /* 13 */
  FF ( d, a, b, c, in[13], S12, 0xfd987193); /* 14 */
  FF ( c, d, a, b, in[14], S13, 0xa679438e); /* 15 */
  FF ( b, c, d, a, in[15], S14, 0x49b40821); /* 16 */

  /* Round 2 */
#define S21 5
#define S22 9
#define S23 14
#define S24 20
  GG ( a, b, c, d, in[ 1], S21, 0xf61e2562); /* 17 */
  GG ( d, a, b, c, in[ 6], S22, 0xc040b340); /* 18 */
  GG ( c, d, a, b, in[11], S23, 0x265e5a51); /* 19 */
  GG ( b, c, d, a, in[ 0], S24, 0xe9b6c7aa); /* 20 */
  GG ( a, b, c, d, in[ 5], S21, 0xd62f105d); /* 21 */
  GG ( d, a, b, c, in[10], S22,  0x2441453); /* 22 */
  GG ( c, d, a, b, in[15], S23, 0xd8a1e681); /* 23 */
  GG ( b, c, d, a, in[ 4], S24, 0xe7d3fbc8); /* 24 */
  GG ( a, b, c, d, in[ 9], S21, 0x21e1cde6); /* 25 */
  GG ( d, a, b, c, in[14], S22, 0xc33707d6); /* 26 */
  GG ( c, d, a, b, in[ 3], S23, 0xf4d50d87); /* 27 */
  GG ( b, c, d, a, in[ 8], S24, 0x455a14ed); /* 28 */
  GG ( a, b, c, d, in[13], S21, 0xa9e3e905); /* 29 */
  GG ( d, a, b, c, in[ 2], S22, 0xfcefa3f8); /* 30 */
  GG ( c, d, a, b, in[ 7], S23, 0x676f02d9); /* 31 */
  GG ( b, c, d, a, in[12], S24, 0x8d2a4c8a); /* 32 */

  /* Round 3 */
#define S31 4
#define S32 11
#define S33 16
#define S34 23
  HH ( a, b, c, d, in[ 5], S31, 0xfffa3942); /* 33 */
  HH ( d, a, b, c, in[ 8], S32, 0x8771f681); /* 34 */
  HH ( c, d, a, b, in[11], S33, 0x6d9d6122); /* 35 */
  HH ( b, c, d, a, in[14], S34, 0xfde5380c); /* 36 */
  HH ( a, b, c, d, in[ 1], S31, 0xa4beea44); /* 37 */
  HH ( d, a, b, c, in[ 4], S32, 0x4bdecfa9); /* 38 */
  HH ( c, d, a, b, in[ 7], S33, 0xf6bb4b60); /* 39 */
  HH ( b, c, d, a, in[10], S34, 0xbebfbc70); /* 40 */
  HH ( a, b, c, d, in[13], S31, 0x289b7ec6); /* 41 */
  HH ( d, a, b, c, in[ 0], S32, 0xeaa127fa); /* 42 */
  HH ( c, d, a, b, in[ 3], S33, 0xd4ef3085); /* 43 */
  HH ( b, c, d, a, in[ 6], S34,  0x4881d05); /* 44 */
  HH ( a, b, c, d, in[ 9], S31, 0xd9d4d039); /* 45 */
  HH ( d, a, b, c, in[12], S32, 0xe6db99e5); /* 46 */
  HH ( c, d, a, b, in[15], S33, 0x1fa27cf8); /* 47 */
  HH ( b, c, d, a, in[ 2], S34, 0xc4ac5665); /* 48 */

  /* Round 4 */
#define S41 6
#define S42 10
#define S43 15
#define S44 21
  II ( a, b, c, d, in[ 0], S41, 0xf4292244); /* 49 */
  II ( d, a, b, c, in[ 7], S42, 0x432aff97); /* 50 */
  II ( c, d, a, b, in[14], S43, 0xab9423a7); /* 51 */
  II ( b, c, d, a, in[ 5], S44, 0xfc93a039); /* 52 */
  II ( a, b, c, d, in[12], S41, 0x655b59c3); /* 53 */
  II ( d, a, b, c, in[ 3], S42, 0x8f0ccc92); /* 54 */
  II ( c, d, a, b, in[10], S43, 0xffeff47d); /* 55 */
  II ( b, c, d, a, in[ 1], S44, 0x85845dd1); /* 56 */
  II ( a, b, c, d, in[ 8], S41, 0x6fa87e4f); /* 57 */
  II ( d, a, b, c, in[15], S42, 0xfe2ce6e0); /* 58 */
  II ( c, d, a, b, in[ 6], S43, 0xa3014314); /* 59 */
  II ( b, c, d, a, in[13], S44, 0x4e0811a1); /* 60 */
  II ( a, b, c, d, in[ 4], S41, 0xf7537e82); /* 61 */
  II ( d, a, b, c, in[11], S42, 0xbd3af235); /* 62 */
  II ( c, d, a, b, in[ 2], S43, 0x2ad7d2bb); /* 63 */
  II ( b, c, d, a, in[ 9], S44, 0xeb86d391); /* 64 */

  buf[0] += a;
  buf[1] += b;
  buf[2] += c;
  buf[3] += d;
}