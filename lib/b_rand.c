#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_rand.c,v 1.1 2002/01/20 03:19:11 seth Exp $";
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
 * Generate random numbers, using OS scheduling jitter and other
 * random crap we find lying around.  Whiten with MD5.  Hopefully good
 * enough for cryptographical purposes.
 */

#include <libbk.h>
#include "libbk_internal.h"

#define BK_TRUERAND_TIME	10000		///< usecs to spin, counting time
#define BK_TRUERAND_BITS	128		///< Bits of entropy gathered per spin--underestimated by 23500% from measurements
#define BK_TRUERAND_DEFAULT	8		///< Default minimum entropy level



/**
 * Information about our random pool
 */
struct bk_randinfo
{
  u_int		br_entropy;			///< Desired minimum entropy level
  u_int		br_cur;				///< Current entropy level
  u_char	br_pool[16];			///< Entropy pool
  u_int		br_cntr;			///< Output counter
  bk_flags	br_flags;			///< Fun for future
};



static int bk_rand_addentropy(bk_s B, struct bk_randinfo *R, bk_flags flags);



/**
 * Initialize the cryptograpical secure (we hope) random number
 * generator.  Note it is suggested to keep bk_randinfo private for
 * cryptographical purposes (do not expose to attackers).
 *	
 *	@param B BAKA Thread/global state
 *	@param entropy Entropy level to maintain, number of bits per 128 bits (zero default)
 *	@param flags Fun for the future
 *	@return <i>NULL</i> on call failure, allocation failure, other failure
 *	@return <br><i>New random state</i> on success
 */
struct bk_randinfo *bk_rand_init(bk_s B, u_int entropy, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_randinfo *R;

  if (!BK_MALLOC(R))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate random structure: %s\n", strerror(errno));
    BK_RETURN(B, NULL);
  }

  R->br_flags = flags;
  R->br_entropy = entropy?entropy:BK_TRUERAND_DEFAULT;
  R->br_cur = 0;

  // Specifically do NOT initialize pool or cntr, for maximum randomness

  BK_RETURN(B, R);
}



/**
 * Destroy the random number pool
 *	
 *	@param B BAKA Thread/global state
 *	@param R Random pool created by randinfo
 *	@param flags Fun for the future
 *	@return <i>NULL</i> on call failure, allocation failure, other failure
 *	@return <br><i>New random state</i> on success
 */
void bk_rand_destroy(bk_s B, struct bk_randinfo *R, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!R)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  free(R);

  BK_VRETURN(B);
}



/**
 * Obtain a random word, performing necessary entropy management
 *	
 *	@param B BAKA Thread/global state
 *	@param R Random pool created by randinfo
 *	@param co Copy-out random number, for speedups
 *	@param flags Fun for the future
 *	@return <i>-1</i> on call failure
 *	@return <br><i>random number</i> (including -1) on success
 */
u_int32_t bk_rand_getword(bk_s B, struct bk_randinfo *R, u_int32_t *co, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  u_int32_t randnum;
  bk_MD5_CTX ctx;

  if (!R)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, (u_int32_t)-1);		// Not very random, is it?
  }
  bk_MD5Init(B, &ctx);

  // Default buffer if user did not supply copyout
  if (!co)
    co = &randnum;

  // Check to make sure we have enough entropy
  if (R->br_cur < R->br_entropy)
    bk_rand_addentropy(B, R, 0);

  // Add in previous entropy, some limited entropy
  bk_MD5Update(B, &ctx, R->br_pool, sizeof(R->br_pool));
  bk_MD5Update(B, &ctx, (void *)&randnum, sizeof(randnum));
  bk_MD5Update(B, &ctx, (void *)&R->br_cntr, sizeof(R->br_cntr));
  R->br_cntr++;
  bk_MD5Final(B, &ctx);

  // Expose only first chunk of pool
  memcpy((void *)co, ctx.digest, sizeof(*co));

  /*
   * Adjust estimates of remaining "unexposed" entropy.
   *
   * We expose 32/128 (3/4) of the entropy remaining in the buffer
   * so adjust our estimates in that way
   *
   * Note, we might be able to more even entropy in the output,
   * and also maintain more time between entropy measurement
   * by not using the entire pool, and rotating which part of the
   * pools is used.  Probably.
   */
  R->br_cur = R->br_cur * 3 / 4;

  BK_RETURN(B, *co);
}



/**
 * Obtain a random buffer.
 *	
 *	@param B BAKA Thread/global state
 *	@param R Random pool created by randinfo
 *	@param buf Buffer to fill (word aligned)
 *	@param len Number of byte to zap
 *	@param flags Fun for the future
 *	@return <i>-1</i> on call failure
 *	@return <br><i>0</i> on success
 */
int bk_rand_getbuf(bk_s B, struct bk_randinfo *R, u_char *buf, u_int len, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!R || !buf || len < 1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, -1);
  }

  // Fill entire buffer, except for trailing odd sized chunk
  while (len > sizeof(u_int32_t))
  {
    bk_rand_getword(B, R, (u_int32_t *)buf, 0);
    len -= sizeof(u_int32_t);
    buf += sizeof(u_int32_t);
  }

  // Handle possible trailing odd sized chunk
  if (len > 0)
  {
    u_int32_t randnum = bk_rand_getword(B, R, NULL, 0);
    memcpy(buf,(char *)&randnum, len);
  }

  BK_RETURN(B, 0);
}



/**
 * Add entropy to pool until it is full to bursting, for possible batching
 *	
 *	@param B BAKA Thread/global state
 *	@param R Random pool created by randinfo
 *	@param flags Fun for the future
 *	@return <i>-1</i> on call failure
 *	@return <br><i>0</i> on success
 */
static int bk_rand_addentropy(bk_s B, struct bk_randinfo *R, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  bk_MD5_CTX ctx;
  struct timeval end, cur;
  
  if (!R)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, -1);
  }

  bk_MD5Init(B, &ctx);

  // Add in previous entropy, some limited entropy
  bk_MD5Update(B, &ctx, R->br_pool, sizeof(R->br_pool));
  gettimeofday(&end,NULL);
  bk_MD5Update(B, &ctx, (char *)&end, sizeof(end));
  bk_MD5Update(B, &ctx, (char *)&R->br_cntr, sizeof(R->br_cntr));
  R->br_cntr++;

  // Fill our pool chock full of entropy
  while (R->br_cur < sizeof(R->br_pool) * 8)
  {
    int cntr;

    gettimeofday(&end,NULL);
    end.tv_usec += BK_TRUERAND_TIME;
    BK_TV_RECTIFY(&end);
    bk_MD5Update(B, &ctx, (char *)&end, sizeof(end));

    cur.tv_sec = 0;
    cur.tv_usec = 0;

    while (BK_TV_CMP(&cur,&end) < 0)
    {
      gettimeofday(&cur,NULL);
      bk_MD5Update(B, &ctx, (char *)&cur, sizeof(cur));
      cntr++;
    }
    bk_MD5Update(B, &ctx, (char *)&cntr, sizeof(cntr));

    R->br_cur += BK_TRUERAND_BITS;
  }
  bk_MD5Final(B, &ctx);
  memcpy(R->br_pool, ctx.digest, sizeof(R->br_pool));

  R->br_cur = sizeof(R->br_pool) * 8;		// Cannot hold more than this

  BK_RETURN(B, 0);
}
