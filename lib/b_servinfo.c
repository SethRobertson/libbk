#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_servinfo.c,v 1.1 2001/11/12 19:15:45 jtt Exp $";
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
 * All of the baka run public and private functions.
 */

#include <libbk.h>
#include "libbk_internal.h"



/**
 * @file
 * The file contains all the required support routines for implementing @a
 * BAKA's @a bk_servinfo structure.
 */

static struct bk_servinfo *bsi_create (bk_s B);
static void bsi_destroy (bk_s B,struct bk_servinfo *bsi);



/**
 * Allocate a @a servinfo.
 *	@param B BAKA thread/global state.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a servinfo on success.
 */
static struct bk_servinfo *
bsi_create (bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_servinfo *bsi=NULL;

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
  if (bsi->bsi_protostr) free (bsi->bsi_protostr);
  free(bsi);

  BK_VRETURN(B);
}



/**
 * Create a @a servinfo from a traditional @a servent. Allocates memory.
 *	@param B BAKA thread/global state.
 *	@param s @a servent to copy.
 *	@param bpi @a bk_protoinfo to add (may be NULL).
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a servinfo on success.
 */
struct bk_servinfo *
bk_servinfo_serventdup (bk_s B, struct servent *s, struct bk_protoinfo *bpi)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_servinfo *bsi;

  if (!s)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!(bsi=bsi_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create bsi\n");
    goto error;
  }
  
  if (s->s_name && !(bsi->bsi_servstr=strdup(s->s_name)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup serv name: %s\n", strerror(errno));
    goto error;
  }

  bsi->bsi_flags = 0;
  bsi->bsi_port = s->s_port;			/* Network order */

  if (s->s_proto && !(bsi->bsi_protostr=strdup(s->s_proto)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup protoname in servinfo: %s\n", strerror(errno));
    goto error;
  }

  BK_RETURN(B,bsi);

 error:
  if (bsi) bsi_destroy(B,bsi);
  BK_RETURN(B,NULL);
  
}

  

/**
 * Create a servinfo from user information. NB: @a port is assumed to be in
 * <em>host</em> order here (since the input comes from a user. If @a
 * servname is NULL, then a string made from the (host order) port is
 * quietly constructed.. Allocates memory.
 *	@param B BAKA thread/global state.
 *	@param servname Optional name of the service.
 *	@param port Port number in <em>host</em> order.
 *	@param bpi @a bk_protoinfo to add (may be NULL);
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a servinfo on success.
 */
struct bk_servinfo *
bk_servinfo_user(bk_s B, char *servstr, u_short port, char *protostr)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char scratch[100];
  struct bk_servinfo *bsi=NULL;
  
  if (!(bsi=bsi_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bsdi\n");
    goto error;
  }

  if (servstr)
  {
    if (!(bsi->bsi_servstr=strdup(servstr)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not strdup serv name: %s\n", strerror(errno));
      goto error;
    }
  }
  else
  {
    snprintf(scratch,100,"%u",port);
    if (!(bsi->bsi_servstr=strdup(scratch)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not strdup serv portnum: %s\n", strerror(errno));
      goto error;
    }
  }
  
  bsi->bsi_flags=0;
  bsi->bsi_port=htons(port);
  if (protostr && !(bsi->bsi_protostr=strdup(protostr)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup protostr in servinfo: %s\n", strerror(errno));
    goto error;
  }
    
  BK_RETURN(B,bsi);

 error:
  if (bsi) bsi_destroy(B,bsi);
  BK_RETURN(B,NULL);

}



/**
 * Public @a servinfo destuctor
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
 *	@param B BAKA thread/global state.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a servinfo on success.
 */
struct bk_servinfo *
bk_servinfo_clone (bk_s B, struct bk_servinfo *obsi)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_servinfo *nbsi=NULL;

  if (!obsi)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!(nbsi=bsi_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bsi\n");
    goto error;
  }

  if (obsi->bsi_servstr && !(nbsi->bsi_servstr=strdup(obsi->bsi_servstr)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup serv name: %s\n", strerror(errno));
    goto error;
  }

  nbsi->bsi_port=obsi->bsi_port;
  nbsi->bsi_flags=obsi->bsi_flags;
  if (obsi->bsi_protostr && !(nbsi->bsi_protostr=strdup(obsi->bsi_protostr)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup protostr in servinfo: %s\n", strerror(errno));
    goto error;
  }

  BK_RETURN(B,nbsi);

 error:
  if (nbsi) bsi_destroy(B,nbsi);
  BK_RETURN(B,NULL);

}
