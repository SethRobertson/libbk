#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_ringbuf.c,v 1.3 2003/07/10 03:09:03 seth Exp $";
static const char libbk__copyright[] = "Copyright (c) 2003";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright LIBBK++
 *
 * Copyright (c) 2003 The Authors. All rights reserved.
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
 * Implementation of a ring buffer, primarily for a threaded environment,
 * which does not use locking as long as system has has stuff to read and room to write.
 */

#include <libbk.h>
#include "libbk_internal.h"



#define HAND_PLUS_ONE(B, ring, number) (((number) + 1)%(ring)->br_size)
#define WRITEALLOWED(B, ring) ((ring)->br_rhand != HAND_PLUS_ONE(B, ring, (ring)->br_whand))
#define READALLOWED(B, ring) ((ring)->br_rhand != (ring)->br_whand)


/**
 * Information about a ring buffer.
 *
 * First, remember that a ring buffer implies modulus arithmetic
 *
 * Write hand points to the node it LAST WROTE
 * Read hand points to the node it LAST READ
 *
 * Thus, if WR=X and RD=X, further reads are blocked
 * If WR=X and RD=X+1, futher writes are blocked
 */
struct bk_ring
{
  volatile u_int	br_rhand;			///< Location of read hand
  int			br_readasleep;			///< Atomic bit--read is sleeping
  volatile u_int	br_whand;			///< Location of write hand
  int			br_writeasleep;			///< Atomic bit--write is sleeping
  u_int			br_size;			///< Size of ring buffer
  bk_flags		br_flags;			///< Fun for the future
#define BK_RING_CLOSING		0x1			///< Ring is being terminated
#ifdef BK_USING_PTHREADS
  pthread_mutex_t	br_wlock;			///< Producer locking
  pthread_mutex_t	br_rlock;			///< Consumer locking
  pthread_mutex_t	br_lock;			///< Locking to prevent everyong from being asleep
  pthread_cond_t	br_cond;			///< Where threads go to sleep
#endif /* BK_USING_PTHREADS */
  void	     * volatile br_ring[0];			///< Ring buffer (MUST BE LAST)
  // DO NOT ADD ANY ELEMENTS AFTER BR_RING
};



/**
 * Create a ring buffer.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param size Maximum size of ring buffer
 *	@param flags Flags Fun for the future
 *	@return <i>NULL</i> on failure.<br>
 *	@return <br><i>Ring structure</i> on success.
 */
struct bk_ring *bk_ring_create(bk_s B, u_int size, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ring *ring;

  if (size < 1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!BK_CALLOC_LEN(ring, sizeof(*ring)+(sizeof(void *)*size)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create ring structure: %s\n", strerror(errno));
    BK_RETURN(B, NULL);
  }

  ring->br_size = size;
  ring->br_flags = flags;
  ring->br_rhand = size-1;
  ring->br_whand = size-1;

#ifdef BK_USING_PTHREADS
  pthread_mutex_init(&ring->br_lock, NULL);
  pthread_mutex_init(&ring->br_rlock, NULL);
  pthread_mutex_init(&ring->br_wlock, NULL);
  pthread_cond_init(&ring->br_cond, NULL);
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B, ring);
}



/**
 * Destroy a ring buffer
 *
 * THREADS: REENTRANT
 *
 * @param B BAKA thread/global environment
 * @param ring Ring buffer
 * @param flags BK_RING_WAIT
 */
void bk_ring_destroy(bk_s B, struct bk_ring *ring, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!ring)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  BK_FLAG_SET(ring->br_flags, BK_RING_CLOSING);

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&ring->br_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */
  while (BK_FLAG_ISSET(flags, BK_RING_WAIT) && READALLOWED(B, ring))
  {
    pthread_cond_broadcast(&ring->br_cond);	// Double-check
    pthread_cond_wait(&ring->br_cond, &ring->br_lock);
  }
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&ring->br_lock) != 0)
    abort();

  if (pthread_mutex_destroy(&ring->br_lock) != 0)
    abort();

  if (pthread_mutex_destroy(&ring->br_rlock) != 0)
    abort();

  if (pthread_mutex_destroy(&ring->br_wlock) != 0)
    abort();

  if (pthread_cond_destroy(&ring->br_cond) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  free(ring);

  BK_VRETURN(B);
}



/**
 * Write to ring buffer
 *
 * THREADS: MT-SAFE (unless ring is full)
 * THREADS: MT-REENTRANT (otherwise)
 *
 * @param B Baka global thread environment
 * @param ring Ring buffer
 * @param opaque Object to add
 * @param flags BK_RING_WAIT (threaded only)
 * @return <i>-1</i> on error
 * @return <br><i>0</i> on queue-full waiting impossible
 * @return <br><i>1</i> on success
 */
int bk_ring_write(bk_s B, struct bk_ring *ring, void *opaque, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  volatile u_int new;
  volatile int ret = 1;

  if (!ring)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, -1);
  }

  if (BK_FLAG_ISSET(ring->br_flags, BK_RING_CLOSING))
    BK_RETURN(B, -1);

#ifdef BK_USING_PTHREADS
  // <TRICKY>Danger Will Robertson--we can go to sleep while holding this lock!!!</TRICKY>
  if (BK_GENERAL_FLAG_ISTHREADON(B) && BK_FLAG_ISCLEAR(flags, BK_RING_NOLOCK) && pthread_mutex_lock(&ring->br_wlock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  // While we can make no forward progress
  while (!WRITEALLOWED(B, ring))
  {
    if (BK_FLAG_ISSET(ring->br_flags, BK_RING_CLOSING))
    {
      ret = -1;
      goto unlockexit;
    }

    // Have we been asked to wait/will it be useful
    if (BK_FLAG_ISCLEAR(flags, BK_RING_WAIT) || !BK_GENERAL_FLAG_ISTHREADON(B))
    {
      ret = 0;
      goto unlockexit;
    }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&ring->br_lock) != 0)
    abort();

    ring->br_writeasleep = 1;
    pthread_cond_broadcast(&ring->br_cond);	// Ensure noone is accidentially asleep

    /*
     * <WARNING>Note, there IS a race condition whereby the reader may
     * consume data from the ring before noticing that we have gone to
     * sleep, since we intentionally do not lock.  However, it is
     * assumed that the reader will eventually try to read again and
     * we will wake up.  Please note that we will NOT ever have both
     * people sleeping at the same time due to the locks around the
     * conditions</WARNING>
     */

    if (!WRITEALLOWED(B, ring) && BK_FLAG_ISCLEAR(ring->br_flags, BK_RING_CLOSING))
      pthread_cond_wait(&ring->br_cond, &ring->br_lock);

    ring->br_writeasleep = 0;

    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&ring->br_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */
  }

  new = HAND_PLUS_ONE(B, ring, ring->br_whand);

  // Please do not reorder these statements Mr. Compiler
  ring->br_ring[new] = opaque;
  ring->br_whand = new;

  if (ring->br_readasleep || !opaque)
    pthread_cond_broadcast(&ring->br_cond);	// Wake up sleeping reader

 unlockexit:
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && BK_FLAG_ISCLEAR(flags, BK_RING_NOLOCK) && pthread_mutex_unlock(&ring->br_wlock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B, ret);
}



/**
 * Read from ring buffer
 *
 * THREADS: MT-SAFE (unless ring is empty)
 * THREADS: MT-REENTRANT (otherwise)
 *
 * @param B Baka global thread environment
 * @param ring Ring buffer
 * @param flags BK_RING_WAIT (threaded only)
 * @return <i>NULL</i> on error or queue-empty waiting impossible
 * @return <br><i>object</i> on success
 */
volatile void *bk_ring_read(bk_s B, struct bk_ring *ring, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  volatile u_int new;
  volatile void *ret = NULL;

  if (!ring)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

#ifdef BK_USING_PTHREADS
  // <TRICKY>Danger Will Robertson--we can go to sleep while holding this lock!!!</TRICKY>
  if (BK_GENERAL_FLAG_ISTHREADON(B) && BK_FLAG_ISCLEAR(flags, BK_RING_NOLOCK) && pthread_mutex_lock(&ring->br_rlock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  // While we can make no forward progress
  while (!READALLOWED(B, ring))
  {
    // If we can be in this situation, we have major race problems...user must communicate shutdown
    if (BK_FLAG_ISSET(ring->br_flags, BK_RING_CLOSING))
      goto unlockexit;

    // Have we been asked to wait/will it be useful
    if (BK_FLAG_ISCLEAR(flags, BK_RING_WAIT) || !BK_GENERAL_FLAG_ISTHREADON(B))
      goto unlockexit;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(&ring->br_lock) != 0)
    abort();

    ring->br_readasleep = 1;
    pthread_cond_broadcast(&ring->br_cond);	// Ensure noone is accidentially asleep

    /*
     * <WARNING>Note, there IS a race condition whereby the reader may
     * write data to the ring before noticing that we have gone to
     * sleep, since we intentionally do not lock.  However, it is
     * assumed that the write will eventually try to write again and
     * we will wake up.  Please note that we will NOT ever have both
     * people sleeping at the same time due to the locks around the
     * conditions.  This might be a problem with EOF processing...</WARNING>
     */

    if (!READALLOWED(B, ring) && BK_FLAG_ISCLEAR(ring->br_flags, BK_RING_CLOSING))
      pthread_cond_wait(&ring->br_cond, &ring->br_lock);

    ring->br_readasleep = 0;

    if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(&ring->br_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */
  }

  new = HAND_PLUS_ONE(B, ring, ring->br_rhand);

  // Please do not reorder these statements Mr. Compiler
  ret = ring->br_ring[new];
  ring->br_ring[new] = (void *)0xdeadbeef;	// Invalid value
  ring->br_rhand = new;

  if (ring->br_writeasleep || BK_FLAG_ISSET(ring->br_flags, BK_RING_CLOSING) || !ret)
    pthread_cond_broadcast(&ring->br_cond);	// Wake up sleeping write, or if we are closing

 unlockexit:
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && BK_FLAG_ISCLEAR(flags, BK_RING_NOLOCK) && pthread_mutex_unlock(&ring->br_rlock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B, ret);
}
