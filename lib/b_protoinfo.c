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
 * The file contains all the required support routines for implementing @a
 * BAKA's @a protoinfo strucutre.
 */

#include <libbk.h>
#include "libbk_internal.h"



#define MAXINTSIZE 11				///< Maximum size of a u_int32 in bytes + NULL



static struct bk_protoinfo *bpi_create (bk_s B);
static void bpi_destroy (bk_s B,struct bk_protoinfo *bpi);



/**
 * Allocate a @a protoinfo.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a protoinfo on success.
 */
static struct bk_protoinfo *
bpi_create (bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_protoinfo *bpi = NULL;

  if (!(BK_CALLOC(bpi)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bpi: %s\n", strerror(errno));
    goto error;
  }

  BK_RETURN(B, bpi);

 error:
  if (bpi) bpi_destroy(B, bpi);
  BK_RETURN(B,NULL);
}



/**
 * Destroy a @a protoinfo.
 *
 * THREADS: MT-SAFE (assuming different bpi)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param bpi to destroy.
 */
static void
bpi_destroy (bk_s B,struct bk_protoinfo *bpi)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bpi)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (bpi->bpi_protostr) free(bpi->bpi_protostr);
  free(bpi);

  BK_VRETURN(B);
}



/**
 * Create a baka proto info.
 *
 *	@param B BAKA thread/global state.
 *	@param proto The protocol number
 *	@param proto_str The protocol string
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
struct bk_protoinfo *
bk_protoinfo_create(bk_s B, int proto, const char *proto_str, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_protoinfo *bpi = NULL;

  if (!proto_str)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!(bpi = bpi_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create the protoinfo\n");
    goto error;
  }

  bpi->bpi_flags = flags;
  bpi->bpi_proto = proto;
  if (!(bpi->bpi_protostr = strdup(proto_str)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not duplicate protocol string: %s\n", strerror(errno));
    goto error;
  }

  BK_RETURN(B, bpi);

 error:
  if (bpi)
    bpi_destroy(B, bpi);
  BK_RETURN(B, NULL);
}



/**
 * Create a @a protoinfo from a traditional @a protoent. Allocates memory.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param s @a protoent to copy.
 *	@param bpi @a bk_protoinfo to add (may be NULL).
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a protoinfo on success.
 */
struct bk_protoinfo *
bk_protoinfo_protoentdup(bk_s B, struct protoent *p)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_protoinfo *bpi;

  if (!p)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!(bpi=bpi_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create bpi\n");
    goto error;
  }

  if (p->p_name)
  {
    if (!(bpi->bpi_protostr = strdup(p->p_name)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not strdup proto name: %s\n", strerror(errno));
      goto error;
    }
  }
  else
  {
    char buf[MAXINTSIZE];

    snprintf(buf, sizeof(buf), "%d", p->p_proto);
    if (!(bpi->bpi_protostr = strdup(buf)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate prootstr: %s\n", strerror(errno));
      goto error;
    }
  }

  bpi->bpi_flags = 0;
  bpi->bpi_proto = p->p_proto;

  BK_RETURN(B,bpi);

 error:
  if (bpi) bpi_destroy(B,bpi);
  BK_RETURN(B,NULL);
}



/**
 * Public @a protoinfo destuctor
 *
 * THREADS: MT-SAFE (assuming different bpi)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param bpi The @a protoinfo to destroy.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
void
bk_protoinfo_destroy (bk_s B,struct bk_protoinfo *bpi)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bpi)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  bpi_destroy(B, bpi);
  BK_VRETURN(B);
}



/**
 * Clone a @a protoinfo. Allocates memory.
 *
 * THREADS: MT-SAFE (assuming different bpi)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a protoinfo on success.
 */
struct bk_protoinfo *
bk_protoinfo_clone(bk_s B, struct bk_protoinfo *obpi)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_protoinfo *nbpi = NULL;

  if (!obpi)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!(nbpi = bpi_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bpi\n");
    goto error;
  }

  if (obpi->bpi_protostr && !(nbpi->bpi_protostr = strdup(obpi->bpi_protostr)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup proto name: %s\n", strerror(errno));
    goto error;
  }

  nbpi->bpi_proto = obpi->bpi_proto;
  nbpi->bpi_flags = obpi->bpi_flags;

  BK_RETURN(B, nbpi);

 error:
  if (nbpi) bpi_destroy(B, nbpi);
  BK_RETURN(B, NULL);
}
