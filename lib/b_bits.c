#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_bits.c,v 1.4 2001/10/11 23:47:16 dupuy Exp $";
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
 * Create the bitfield of a certain size
 *
 * Pretty dumb and simple, but clean
 */
char *bk_bits_create(bk_s B, size_t size, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libphadm");
  char *ret;

  /* always allocate something */
  if (!(ret = malloc(BK_BITS_SIZE(size ? size : 1))))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bit storage: %s\n", strerror(errno));
    BK_RETURN(B, NULL);
  }
  memset(ret, 0, BK_BITS_SIZE(size));

  BK_RETURN(B, ret);
}



/*
 * Destroy the bitfield
 */
void bk_bits_destroy(bk_s B, char *base)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libphadm");

  if (!base)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  free(base);

  BK_VRETURN(B);
}



/*
 * Save bitfield to a character string for storage
 */
char *bk_bits_save(bk_s B, char *base, size_t size, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libphadm");
  char *ret, *cur;
  int len,x,tmp;

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
    strcpy(ret, "0");		/* distinct from "00" = zero byte */
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
      byte &= (0xff >> -size);	/* clear unused bits from MSB down */
      
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



/*
 * Restore a saved bitfield back to memory representation
 *
 * Note string must be exactly as it was given during _save, specifically,
 * the bitfield must be null terminated.
 */
char *bk_bits_restore(bk_s B, char *saved, size_t *size, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libphadm");
  char *ret, *cur;
  int len;
  u_int tmp;

  if (!saved)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  len = strlen(saved);
  if (len == 1)			/* zero bits */
  {
    if (size) *size = 0;

    BK_ORETURN(B, malloc(1));	/* try to return a pointer if possible */
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
