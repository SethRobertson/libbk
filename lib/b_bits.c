#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_bits.c,v 1.10 2002/07/18 22:52:43 dupuy Exp $";
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
 * Multibyte bitfield fundamental routines (creation, destruction, import, export).
 * See also BK_BITS_*
 *
 * Also other bitish functions
 */

#include <libbk.h>
#include "libbk_internal.h"



/**
 * Create the bitfield of a certain size.
 *
 * Pretty dumb and simple, but clean
 *
 *	@param B BAKA thread/global state 
 *	@param size Size of bitfield.
 *	@param flags Fun for the future.
 *	@return <i>NULL</i> on allocation failure.
 *	@return <BR><i>bitarray</i> on success.
 */
char *bk_bits_create(bk_s B, size_t size, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *ret;

  if (!size) size=1;

  /* always allocate something */
  if (!(ret = malloc(BK_BITS_SIZE(size))))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bit storage: %s\n", strerror(errno));
    BK_RETURN(B, NULL);
  }
  memset(ret, 0, BK_BITS_SIZE(size));

  BK_RETURN(B, ret);
}



/**
 * Destroy the bitfield
 *
 *	@param B BAKA thread/global state 
 *	@param base The created bitfield
 */
void bk_bits_destroy(bk_s B, char *base)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!base)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  free(base);

  BK_VRETURN(B);
}



/**
 * Save bitfield to a character string for storage. Allocates memory
 * which should be freed with free(3). Remember that you still may
 * need to destroy the bitfield you are converting.
 *
 *	@param B BAKA thread/global state 
 *	@param base The bitfield we are converting to ascii format
 *	@param size The size of the bitfield in question
 *	@param flags Fun for the future.
 *	@return <i>NULL</i> on call failure, allocation failure, other failure
 *	@return <BR><i>string</i> representing @a base for saving.
 */
char *bk_bits_save(bk_s B, char *base, size_t size, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *ret=NULL, *cur;
  int len,tmp;

  if (!base)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  if (size == 0)
  {
    if (!(ret = malloc(2)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate bits storage: %s\n",strerror(errno));
      BK_RETURN(B, NULL);
    }
    strcpy(ret, "0");				/* distinct from "00" = zero byte */
    BK_RETURN(B, ret);
  }

  len = BK_BITS_SIZE(size)*2+1;

  if (!(ret = malloc(len)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bits storage: %s\n",strerror(errno));
    BK_RETURN(B, NULL);
  }

  cur = ret;
  while (size > 0)
  {
    u_char byte = *base;

    if ((int)(size -= 8) < 0)
      byte &= (0xff >> -size);			/* clear unused bits from MSB down */
      
    tmp = snprintf(cur, len, "%02x", (u_char)byte);
    if (tmp < 0 || tmp >= len)
    {
      bk_error_printf(B, BK_ERR_ERR, "Fatal math error in computing ascii size, probably: %s\n",strerror(errno));
      goto error;
    }

    base++;
    len -= 2;
    cur += 2;
  }

  BK_RETURN(B, ret);

 error:
  if (ret) free(ret);
  BK_RETURN(B, NULL);
}



/**
 * Restore a saved bitfield back to memory representation.
 *
 * Note string must be exactly as it was given during _save,
 * specifically, the bitfield must be null terminated.  Also note that
 * the size is rounded up to the nearest octet--a 7 bit bitfield will
 * be returned, here, as an 8 bit bitfield.
 *
 *	@param B BAKA thread/global state 
 *	@param saved The ascii data previously created by @a bk_bits_save
 *	@param size The size of the bitfield in question
 *	@param flags Fun for the future.
 *	@return <i>NULL</i> on call failure, allocation failure, other failure
 *	@return <BR><i>bitfield</i> on success
 */
char *bk_bits_restore(bk_s B, char *saved, size_t *size, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *ret=NULL, *cur;
  int len;
  u_int tmp;

  if (!saved)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  len = strlen(saved);
  if (len == 1)					/* zero bits */
  {
    if (size) *size = 0;

    if (!(ret = malloc(1)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate space for zero length bitfield: %s\n", strerror(errno));
      BK_RETURN(B, NULL);
    }

    BK_RETURN(B, ret);				/* try to return a pointer if possible */
  }

  if (size) *size = 4 * len;

  if (!(ret = malloc(len / 2)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate space for bitfield: %s\n", strerror(errno));
    BK_RETURN(B, NULL);
  }

  cur = ret;
  while (*saved != '\0' && len > 0)
  {
    tmp = 0;
    sscanf(saved,"%2x",&tmp);
    *cur++ = tmp;
    saved += 2;
    len -= 2;
  }

  BK_RETURN(B, ret);
}



#define BC_TWO(c) (0x1u << (c))
#define BC_MASK(c) (((unsigned int)(-1)) / (BC_TWO(BC_TWO(c)) + 1u))
#define BC_COUNT(x,c) ((x) & BC_MASK(c)) + (((x) >> (BC_TWO(c))) & BC_MASK(c))



/**
 * Count the number of bits in a word
 *
 * This version was chosen after speed trials of eight different bit
 * count routines.  The original source of these functions are
 * unknown, but the algorithms were gathered from
 * http://www-db.stanford.edu/~manku/bitcount.html
 *
 * Parallel Count carries out bit counting in a parallel fashion.
 * Consider n after the first line has finished executing. Imagine
 * splitting n into pairs of bits. Each pair contains the <em>number
 * of ones</em> in those two bit positions in the original n.  After
 * the second line has finished executing, each nibble contains the
 * <em>number of ones</em> in those four bits positions in the
 * original n. Continuing this for five iterations, the 64 bits
 * contain the number of ones among these sixty-four bit positions in
 * the original n. That is what we wanted to compute.
 *
 *
 * @param B BAKA Thread/global environment
 * @param word The word whose bits you wish to count
 * @return <i>number of bits in word</i>
 */
u_int bk_bitcount(bk_s B, u_int n)
{
  n = BC_COUNT(n, 0) ;
  n = BC_COUNT(n, 1) ;
  n = BC_COUNT(n, 2) ;
  n = BC_COUNT(n, 3) ;
  n = BC_COUNT(n, 4) ;
  /* n = BC_COUNT(n, 5) ;    for 64-bit integers */
  return n ;
}
