#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_rand.c,v 1.7 2003/06/04 16:25:01 jtt Exp $";
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
 *
 * For our true random number generation system, we have chosen to use a
 * spin counter to measure the randomness of the OS's scheduling and
 * gettimeofday clock updates.  We sit in a loop incrementing a counter
 * and calling gettimeofday (which provides the OS an opportunity for
 * preemption) until we have at least N (100 in our example) microseconds
 * of time accumulated.  The number of loops we have gone through
 * represents 4 data bytes of input entropy.  Obviously some bytes,
 * especially the higher ones, will be *highly* predictable, thus we go
 * through many rounds to produce one byte of pure entropy.  A strong
 * mixing function (e.g. MD5) is used to distill the entropy.
 *
 * The exact level of randomness is machine dependent.  During testing on
 * available machines, we have observed at worst the ratio of 72 input
 * bytes to one output byte, and at best 26 input bytes to one output
 * byte.  We could hardcode the system to use the worst we have seen
 * (plus a fudge-factor), but an actual measurement facility would be
 * better--to avoid wasting time on more efficient/random machines.
 *
 * The proposed measurement facility is to take the maximum difference in
 * the first 10 numbers generated through the scheme.  This number is
 * inversely related to the number of input bytes required to generate
 * one output byte.  Further analysis on four platforms ranging from a
 * dual processor AMD Athlon 1.6GHz to an Mobile Intel 200MHz PII (18x
 * performance difference) suggests that the formula for producing this
 * number (and remaining stable for likely future numbers is):
 *
 *     y = 76.059 - 0.97271 x + 0.0045668 x^2 - 8.7497e-07 x^3
 *
 * It is expected that this formula will not be perfect, and it is
 * expected that individual machines will vary in entropy produced,
 * perhaps greatly--especially if an attacker can effect the
 * system--during the lifetime of a single process.  Thus, in addition to
 * proper seeing and replenishment of the entropy pool, we must ensure
 * that our mixing function is cryptographically strong so that even
 * minimal entropy will produce excellently non-predictable output.
 * Also, it is critical that knowledge of the entropy pool (through page
 * snooping of a root-privileged attacker) not be sufficient to predict
 * future output.
 *
 * Note in general these functions are   S  L  O  W   so only use when
 * appropriate.
 */

#include <libbk.h>
#include "libbk_internal.h"

#define BK_TRUERAND_TIME	100		///< usecs to spin, counting time
#define BK_TRUERAND_AX3		-8.7497e-07	///< Factor for x^3
#define BK_TRUERAND_BX2		0.0045668	///< Factor for x^2
#define BK_TRUERAND_CX		-0.97271	///< Factor for x
#define BK_TRUERAND_D		76.059		///< Additive factor
#define BK_ROUND_SIZE		sizeof(u_int32_t) ///< Number of bytes produced per round
#define BK_MIN_ROUNDS		3		///< Min rounds per byte
#define BK_MAX_ROUNDS		100		///< Min rounds per byte
#define BK_POOLSIZE		16		///< Size of pool in bytes (related to hash size)
#define BK_MAJOR_REFRESH	12		///< How often to mix into a new full batch of entropy



/**
 * "Function" to get one round's worth of random number
 *
 * @param cntr The counter which will contain the random number
 * @param end struct timeval end
 */
#define BK_TRUERAND_GENROUND(cntr,cur,end)				\
do {									\
  gettimeofday((end),NULL);						\
  (end)->tv_usec += BK_TRUERAND_TIME;					\
  BK_TV_RECTIFY(end);							\
  while (gettimeofday(cur, NULL) == 0 && BK_TV_CMP(cur,end) < 0)	\
    cntr++;								\
} while (0)



/**
 * Information about our random pool.  We suggest it be shared by all
 * users and all threads to increase security through interleaved
 * access.
 */
struct bk_truerandinfo
{
  u_int			br_roundsperbyte;	///< How many measurement rounds per per output byte
  u_int			br_usecounter;		///< Number of queries to pool
  u_int			br_reinitmask;		///< Mask for major renewal of entropy pool
  bk_flags		br_flags;		///< Fun for future
#ifdef BK_USING_PTHREADS
  pthread_mutex_t	br_lock;		///< Someone using the EP
#endif /* BK_USING_PTHREADS */
  u_char		br_pool[BK_POOLSIZE];	///< Entropy pool
} shared_random_pool;



static u_int bk_truerand_measuregen(bk_s B);
static void bk_truerand_generate(bk_s B, bk_MD5_CTX *ctx, int rounds);
static void bk_truerand_opertunistic(bk_s B, bk_MD5_CTX *ctx);



/**
 * Initialize the random pool
 *
 * THREADS: MT-SAFE
 *
 * @param B BAKA Thread/global state
 * @param flags Fun for the future
 * @return <i>NULL</i> on call failure, allocation failure
 * @return <br><i>structure</i> on success
 */
struct bk_truerandinfo *bk_truerand_init(bk_s B, int reinitbits, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_truerandinfo *R;
  bk_MD5_CTX ctx;

  if (!BK_MALLOC(R))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create random structure: %s\n", strerror(errno));
    BK_RETURN(B, NULL);
  }

#ifdef BK_USING_PTHREADS
  pthread_mutex_init(&R->br_lock, NULL);
#endif /* BK_USING_PTHREADS */

  R->br_roundsperbyte = 0;
  R->br_usecounter = 0;
  R->br_reinitmask = (1<<(reinitbits>0?reinitbits:BK_MAJOR_REFRESH)) - 1;

  assert(sizeof(ctx.digest) == BK_POOLSIZE);

  /*
   * <TRICKY>Explicitly leave pool uninitialized to allow potential
   * for addition randomness</TRICKY>
   */

  BK_RETURN(B, R);
}



/**
 * Destroy the random pool
 *
 * THREADS: MT-SAFE (assuming different R)
 * THREADS: REENTRANT (assuming same R)
 *
 * @param B BAKA Thread/global state
 * @param flags Fun for the future
 * @return <i>NULL</i> on call failure, allocation failure
 * @return <br><i>structure</i> on success
 */
void bk_truerand_destroy(bk_s B, struct bk_truerandinfo *R)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!R)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

#ifdef BK_USING_PTHREADS
  pthread_mutex_destroy(&R->br_lock);
#endif /* BK_USING_PTHREADS */

  free(R);

  BK_VRETURN(B);
}



/**
 * Slowly obtain a true random word, performing necessary entropy management
 *
 * THREADS: MT-SAFE (assuming different R)
 * THREADS: THREAD-REENTRANT (otherwise)
 *
 *	@param B BAKA Thread/global state
 *	@param co Copy-out random number, for speedups
 *	@param flags Fun for the future
 *	@return <i>-1</i> on call failure
 *	@return <br><i>random number</i> (including -1) on success
 */
u_int32_t bk_truerand_getword(bk_s B, struct bk_truerandinfo *R, u_int32_t *co, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  bk_MD5_CTX ctx;
  u_int32_t testnum = 0;
  int x;

  if (!R)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, -1);				// <TRICKY>Cannot be told apart from success</TRICKY>
  }

  bk_MD5Init(B, &ctx);

  // Default buffer if user did not supply copyout
  if (!co)
    co = &testnum;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADREADY(B) && pthread_mutex_lock(&R->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  // <TODO>Ideally make this configurable</TODO>
  if (!(R->br_usecounter & R->br_reinitmask))
  {
    // Need a major refresh of the entropy pool
    bk_debug_printf_and(B, 1, "Counter %d, mask %x, major reinit\n", R->br_usecounter, R->br_reinitmask);

    bk_MD5Init(B, &ctx);
    bk_MD5Update(B, &ctx, R->br_pool, sizeof(R->br_pool)); // Ignore uninit memory errors
    R->br_roundsperbyte = bk_truerand_measuregen(B);
    bk_truerand_generate(B, &ctx, R->br_roundsperbyte * BK_POOLSIZE);
    bk_truerand_opertunistic(B, &ctx);
    bk_MD5Final(B, &ctx);
    memcpy(R->br_pool, ctx.digest, BK_POOLSIZE);
  }

  bk_MD5Init(B, &ctx);
  bk_MD5Update(B, &ctx, R->br_pool, sizeof(R->br_pool));

  // Perform a pre-emptive replenish of the entropy bits we are removing
  bk_truerand_generate(B, &ctx, R->br_roundsperbyte * sizeof(testnum));
  bk_MD5Update(B, &ctx, (void *)&R->br_usecounter, sizeof(R->br_usecounter));
  R->br_usecounter++;
  bk_MD5Final(B, &ctx);

  // First word is returned to user
  memcpy((void *)co, ctx.digest, sizeof(*co));

  /*
   * Other words are XORd back into the pool
   *
   * <WARNING>Yes, the last word of the pool is only modified during
   * major pool refreshs.  Wanna make something of it?</WARNING>
   */
  for (x=sizeof(*co);x<BK_POOLSIZE;x+=sizeof(u_int32_t))
  {
    *((u_int32_t *)&R->br_pool[x]) ^= *((u_int32_t *)&ctx.digest[x]);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADREADY(B) && pthread_mutex_unlock(&R->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B, *co);
}



/**
 * Obtain a random buffer.
 *
 * THREADS: MT-SAFE (different R)
 * THREADS: THREAD-REENTRANT (otherwise)
 *
 *	@param B BAKA Thread/global state
 *	@param R Random pool created by randinfo
 *	@param buf Buffer to fill (word aligned)
 *	@param len Number of byte to zap
 *	@param flags Fun for the future
 *	@return <i>-1</i> on call failure
 *	@return <br><i>0</i> on success
 */
int bk_truerand_getbuf(bk_s B, struct bk_truerandinfo *R, u_char *buf, u_int len, bk_flags flags)
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
    bk_truerand_getword(B, R, (u_int32_t *)buf, 0);
    len -= sizeof(u_int32_t);
    buf += sizeof(u_int32_t);
  }

  // Handle possible trailing odd sized chunk
  if (len > 0)
  {
    u_int32_t randnum = bk_truerand_getword(B, R, NULL, 0);
    memcpy(buf,(char *)&randnum, len);
  }

  BK_RETURN(B, 0);
}



/**
 * Generate a round of (low) entropy by using timing and scheduling jitter
 *
 * THREADS: MT-SAFE
 *
 * @param B Baka thread/global environment
 * @return <i>-1</i> on call failure
 * @return <i>&gt; 0 - number of rounds</i> on success.
 */
static u_int bk_truerand_measuregen(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  volatile u_int32_t thisc;
  u_int32_t minc,maxc;
  struct timeval this,end;
  int x;
  u_int y;

  minc = UINT_MAX;
  maxc = 0;

  for (x=10;x>0;x--)
  {
    thisc = 0;
    BK_TRUERAND_GENROUND(thisc, &this, &end);
    minc = MIN(minc,thisc);
    maxc = MAX(maxc,thisc);
    bk_debug_printf_and(B, 2, "Test %d: cur: %d, min %d, max %d, %d.%06d %d.%06d\n", x, thisc, minc, maxc, (int)this.tv_sec, (int)this.tv_usec, (int)end.tv_sec, (int)end.tv_usec);
  }

  x = maxc - minc;

  // Compute number of input bytes to one random output byte
  y = (BK_TRUERAND_AX3 * x * x *x +
       BK_TRUERAND_BX2 * x * x +
       BK_TRUERAND_CX * x +
       BK_TRUERAND_D);
  bk_debug_printf_and(B, 1, "Computed difference %d: I need %d input bytes per output byte\n", x, y);

  // Convert into rounds of 4 bytes
  y = (y + sizeof(u_int32_t) - 1) / sizeof(u_int32_t);

  // Ensure we don't go crazy do to formula instability in outliers
  y = MIN(y,BK_MAX_ROUNDS);
  y = MAX(y,BK_MIN_ROUNDS);

  BK_RETURN(B, y);
}



/**
 * Use available randomness in a number of rounds to influence the random number
 *
 * THREADS: MT-SAFE (assuming different ctx)
 *
 * @param B BAKA Thread/global state
 * @param ctx Hash context we are updating
 * @param rounds Number of rounds of data creation
 */
static void bk_truerand_generate(bk_s B, bk_MD5_CTX *ctx, int rounds)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
#ifndef __INSURE__
  // Seth specifically wants this variable uninitialized for entropy....
  volatile u_int32_t thisc;
#else
  // ... but that results in a pointless Insight error, so we initialize it in that case only.
  volatile u_int32_t thisc = 0;
#endif
  struct timeval this,end;

  if (!ctx)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  // Stupid uninitialized variable warnings...
  memcpy((void *)&thisc, (void *)&thisc, 0);

  // Double check sanity
  if (rounds < BK_MIN_ROUNDS)
    rounds = BK_MAX_ROUNDS;

  while (rounds--)
  {
    BK_TRUERAND_GENROUND(thisc,&this,&end);

    // Throw in one round's worth of data
    bk_MD5Update(B, ctx, (void *)&thisc, sizeof(thisc));

    // Opportunistically throw in timing information
    bk_MD5Update(B, ctx, (void *)&this, sizeof(this));
    bk_MD5Update(B, ctx, (void *)&end, sizeof(end));
  }

  BK_VRETURN(B);
}



/**
 * Opportunistically add other available randomness from the system.
 *
 * This could include things like vmstat/ps/netstat output, but we
 * currently just use /dev/{random,srandom}
 *
 * THREADS: MT-SAFE (assuming different ctx)
 *
 * @param B Baka thread/global environment
 * @param ctx Hash context
 */
void bk_truerand_opertunistic(bk_s B, bk_MD5_CTX *ctx)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int fd = -1;
  char buf[BK_POOLSIZE];

  if (!ctx)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  // Precedence of devices is perhaps wrong...
#ifdef HAVE_DEVSRANDOM				  // Strong random data (may block)
  fd = open("/dev/srandom", O_RDONLY|O_NONBLOCK);
#elsif HAVE_DEVURANDOM
  fd = open("/dev/urandom", O_RDONLY|O_NONBLOCK); // Non-blocking random data
#elsif HAVE_DEVRANDOM
  fd = open("/dev/random", O_RDONLY|O_NONBLOCK);  // Nuclear random data (may block)
#elsif HAVE_DEVRANDOM
  fd = open("/dev/prandom", O_RDONLY|O_NONBLOCK); // Kernel pseudo random data
#endif /* HAVE_DEV*RANDOM */

  if (fd >= 0)
  {
    read(fd, buf, BK_POOLSIZE);			// Yes, I am intentionally ignoring return code
    close(fd);
    bk_MD5Update(B, ctx, buf, BK_POOLSIZE);
    bk_MD5Update(B, ctx, (void *)&fd, sizeof(fd));	// Probably predictable, but what the hey
  }

  // <TODO>Add other opportunistic data sources</TODO>

  BK_VRETURN(B);
}
