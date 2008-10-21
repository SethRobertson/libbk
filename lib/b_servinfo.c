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
 * BAKA's @a bk_servinfo structure.
 */

#include <libbk.h>
#include "libbk_internal.h"



#define MAXINTSIZE 11				///< Maximum size of a u_int32 in bytes + NULL



static struct bk_servinfo *bsi_create (bk_s B);
static void bsi_destroy (bk_s B,struct bk_servinfo *bsi);



/**
 * Allocate a @a servinfo.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a servinfo on success.
 */
static struct bk_servinfo *
bsi_create (bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_servinfo *bsi = NULL;

  if (!(BK_CALLOC(bsi)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bsi: %s\n", strerror(errno));
    goto error;
  }

  BK_RETURN(B,bsi);

 error:
  if (bsi) bsi_destroy(B, bsi);
  BK_RETURN(B,NULL);
}



/**
 * Destroy a @a servinfo. NB: If the @a protoinfo pointer is not NULL, then
 * it is destroyed too. This is probably not what you want in most
 * cases. Just make sure that you NULL it out if you don't want it nuked.
 *
 * THREADS: MT-SAFE (assuming different bsi)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param bsi to destroy.
 */
static void
bsi_destroy (bk_s B,struct bk_servinfo *bsi)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bsi)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (bsi->bsi_servstr) free (bsi->bsi_servstr);
  free(bsi);

  BK_VRETURN(B);
}



/**
 * Create a @a servinfo from a traditional @a servent. Allocates memory.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param s @a servent to copy.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a servinfo on success.
 */
struct bk_servinfo *
bk_servinfo_serventdup (bk_s B, struct servent *s)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_servinfo *bsi;

  if (!s)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!(bsi = bsi_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create bsi\n");
    goto error;
  }

  if (s->s_name)
  {
    if (!(bsi->bsi_servstr = strdup(s->s_name)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not strdup serv name: %s\n", strerror(errno));
      goto error;
    }
  }
  else
  {
    char buf[MAXINTSIZE];

    snprintf(buf, MAXINTSIZE, "%d", ntohs(s->s_port));
    if (!(bsi->bsi_servstr = strdup(buf)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate servstr: %s\n", strerror(errno));
      goto error;
    }
  }

  bsi->bsi_flags = 0;
  bsi->bsi_port = s->s_port;

  BK_RETURN(B,bsi);

 error:
  if (bsi) bsi_destroy(B,bsi);
  BK_RETURN(B,NULL);
}



/**
 * Public @a servinfo destuctor
 *
 * THREADS: MT-SAFE (assuming different bsi)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param bsi The @a servinfo to destroy.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
void
bk_servinfo_destroy (bk_s B,struct bk_servinfo *bsi)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bsi)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  bsi_destroy(B, bsi);
  BK_VRETURN(B);
}



/**
 * Clone a @a servinfo. Allocates memory.
 *
 * THREADS: MT-SAFE (assuming different obsi)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param obsi The old @a bk_servinfo from which to clone.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a servinfo on success.
 */
struct bk_servinfo *
bk_servinfo_clone (bk_s B, struct bk_servinfo *obsi)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_servinfo *nbsi = NULL;

  if (!obsi)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!(nbsi = bsi_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bsi\n");
    goto error;
  }

  if (obsi->bsi_servstr && !(nbsi->bsi_servstr = strdup(obsi->bsi_servstr)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup serv name: %s\n", strerror(errno));
    goto error;
  }

  nbsi->bsi_port = obsi->bsi_port;
  nbsi->bsi_flags = obsi->bsi_flags;

  BK_RETURN(B,nbsi);

 error:
  if (nbsi) bsi_destroy(B,nbsi);
  BK_RETURN(B,NULL);

}
