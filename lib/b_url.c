#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_url.c,v 1.1 2001/12/10 22:50:16 jtt Exp $";
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
 * All of the support routines for dealing with urls.
 */

#include <libbk.h>
#include "libbk_internal.h"



static u_int count_colons(bk_s B, const char *str);


/**
 * Baka internal representation of a URL. The char *'s are all NULL
 * terminated strings for convenience.
 */
struct bk_url
{
  bk_flags		bu_flags;		///< Everyone needs flags.
  char *		bu_proto;		///< Protocol specification
  char *		bu_host;		///< Host specification
  char *		bu_serv;		///< Service specification
  char *		bu_path;		///< Path specification
};




/**
 * Parse a url
 *	@param B BAKA thread/global state.
 *	@param url Url to parse.
 *	@param flags Flags for the future.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a bk_url on sucess.
 */
struct bk_url *
bk_url_parse(bk_s B, const char *url_in, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *url = NULL;
  struct bk_url *bu = NULL;
  char *host = NULL;
  char *proto = NULL;
  char *serv = NULL;
  char *path = NULL;
  u_int cnt;

  if (!url_in)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }
  
  if (!(bu = bk_url_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "could not create url struct\n");
    goto error;
  }

  if (!(url = strdup(url_in)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup url: %s\n", strerror(errno));
    goto error;
  }

  if (!(host = strstr(url, "://")))
  {
    /* There is no protocol specified */
    host = url;
  }
  else
  {
    proto = url;
    *host = '\0';
    host += strlen("://");
    if (!(bu->bu_proto = strdup(proto)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not strdup protocol: %s\n", strerror(errno));
      goto error;
    }
  }
  
  if ((path = strchr(host,'/')))
  {
    if (!(bu->bu_path=strdup(path)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not strdup pathd: %s\n", strerror(errno));
      goto error;
    }
    *path='\0';
  }

  /* 
   * If path == host (ie there is no host/serv part), then host will
   * pointing at an <em>empty</em> (ie not NULL) string, so the following
   * code should all work as you would expect.
   */
  switch ((cnt = count_colons(B, host)))
  {
    /* Case which includes service information */
  case 1: /* AF_INET */
  case 8: /* AF_INET6 */
    if (!(serv = strrchr(host,':')))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not locate service specifier in string where I *know* it exists\n");
      goto error;
    }
    *serv++ = '\0';
    if (!(bu->bu_serv = strdup(serv)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not strdup service: %s\n", strerror(errno));
      goto error;
    }

    /* Intentional fall through */

    /* Case which does not include service information */
  case 0: /* AF_INET */
  case 7: /* AF_INET6 */
    /* This *intentially* copies the empty string */
    if (!(bu->bu_host = strdup(host)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Coulld not strdup host specifier: %s\n", strerror(errno));
      goto error;
    }
    break;

  default:
    bk_error_printf(B, BK_ERR_ERR, "Illegal number of colons in host[:service] specifier: %d (only 0, 1, 7, or 8 allowed)\n", cnt);
    goto error;
    break;
  }

  /* 
   * Now "normalize" the structure by replacing NULLs with "" 
   * NB The host part should already be copied if empty, but we check here
   * too just to be safe.
   */
  if (!bu->bu_proto && !(bu->bu_proto = strdup("")) ||
      !bu->bu_host && !(bu->bu_host = strdup("")) ||
      !bu->bu_serv && !(bu->bu_serv = strdup("")) ||
      !bu->bu_path && !(bu->bu_path = strdup("")))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not normalize structure by strdup'ing empty string for NULL's: %s\n", strerror(errno));
    goto error;
  }
  
  if (url) free(url);
  BK_RETURN(B,bu);

 error:
  if (bu) bk_url_destroy(B, bu);
  if (url) free(url);
  BK_RETURN(B,NULL);
}



/**
 * Create a url structure and clean it out.
 *
 *	@param B BAKA thread/global state.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a bk_url on success.
 */
struct bk_url *
bk_url_create(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_url *bu = NULL;

  if (!(BK_CALLOC(bu)))
  {
    bk_error_printf(B, BK_ERR_ERR, "could not allocate bk_url: %s\n", strerror(errno));
    BK_RETURN(B,NULL);
  }

  BK_RETURN(B,bu);
}



/**
 * Destroy a @a bk_url completely.
 *
 *	@param B BAKA thread/global state.
 *	@param bu The url structure to nuke.
 */
void
bk_url_destroy(bk_s B, struct bk_url *bu)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bu)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (bu->bu_proto) free(bu->bu_proto);
  if (bu->bu_host) free(bu->bu_host);
  if (bu->bu_serv) free(bu->bu_serv);
  if (bu->bu_path) free(bu->bu_path);
  free(bu);
  BK_VRETURN(B);
  
}



/**
 * Count the number of colons (:) in a string.
 *
 *	@param B BAKA thread/global state.
 *	@param str The string to sum up.
 *	@return <i>-1</i> on failure.<br>
 *	@return The <i>cnt</i> of colons on success.
 */
static u_int
count_colons(bk_s B, const char *str)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  u_int cnt = 0;
  char c;

  if (!str)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  while((c = *str++))
  {
    if (c == ':') 
      cnt++;
  }

  BK_RETURN(B,cnt);
}
