#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_url.c,v 1.6 2001/12/11 21:30:53 seth Exp $";
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



static u_int count_colons(bk_s B, const char *str, const char *str_end);



/**
 * Parse a url
 *
 *	@param B BAKA thread/global state.
 *	@param url Url to parse.
 *	@param flags Flags for the future.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a bk_url on sucess.
 */
struct bk_url *
bk_url_parse(bk_s B, const char *url, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_url *bu = NULL;
  const char *host=NULL;
  const char *host_end;
  const char *proto=NULL;
  const char *proto_end;
  const char *serv=NULL;
  const char *path=NULL;
  u_int cnt;

  if (!url)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }
  
  if (!(bu = bk_url_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "could not create url struct\n");
    goto error;
  }

  proto = url;
  proto_end = strpbrk(proto,":/");		/* No : or / in proto */
  if (!proto_end || strncmp(proto_end,":/", 2) != 0) /* Proto ends with ":/" (at a minimum) */
  {
    proto = NULL;				/* No protocol */
    host = url;					/* Try host from begining */
    if (!(bu->bu_proto = strdup("")))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not strdup proto: %s\n", strerror(errno));
      goto error;
    }
  }
  else
  {
    host = proto_end+1;				/* Skip over : but *not* / */
    if (!(bu->bu_proto = bk_strndup(B, proto, proto_end - proto +1)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not copy proto\n");
      goto error;
    }
  }
  
  if (strncmp(host,"//",2) == 0) 		/* Skip over // */
  {
    host += 2;
  }

  /* 
   * Host is now set to the begining of host. Now we attempt to find the
   * _path_ component and save it (if we find it). Then we process the
   * host/serv part.
   */
  if ((path = strchr(host,'/')))
  {
    if (!(bu->bu_path = strdup(path)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not strdup path: %s\n", strerror(errno));
      goto error;
    }
    host_end = path;
  }
  else
  {
    if (!(bu->bu_path = strdup("")))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not strdup empty string to path: %s\n", strerror(errno));
      goto error;
    }
    host_end = host+strlen(host);
  }

  if (path != host)
  {
    switch ((cnt = count_colons(B, host, host_end)))
    {
      /* host_part:serv_part */
    case 1: /* AF_INET or hostname or missing host_part */
    case 8: /* AF_INET6 */
      if (!(serv = strrchr(host,':')))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not locate the last colon in string which I know has either 1 or 8. How could this happend?\n");
	goto error;
      }
      serv++;					/* Skip over ':' */
      if (!(bu->bu_serv = bk_strndup(B, serv, host_end - serv +1)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not copy service string\n");
	goto error;
      }
    
      if (!(bu->bu_host = bk_strndup(B, host, serv - host)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not copy host string\n");
	goto error;
      }
      break;
    
    /* host_part only. No service string */
    case 0: /* AF_INET or hostname or missing host_part */
    case 7: /* Af_INET6 */
      if (!(bu->bu_serv = strdup("")))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not strdup service string: %s\n", strerror(errno));
	goto error;
      }

      if (!(bu->bu_host = bk_strndup(B, host, host_end - host +1)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not copy host string\n");
	goto error;
      }
      break;
    
    default:
      bk_error_printf(B, BK_ERR_ERR, "Illegal colon count (is: %d -- allowed values: 0, 1, 7, and 8\n", cnt);
      goto error;
    }
  }
  else
  {
    /* There is no host *or* service part */
    if (!(bu->bu_host = strdup("")))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not strdup empty string to host\n");
      goto error;
    }
    if (!(bu->bu_serv = strdup("")))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not strdup empty string to serv\n");
      goto error;
    }
  }

  BK_RETURN(B,bu);

 error:
  if (bu) bk_url_destroy(B, bu);
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
 *	@param str_end The maximum position of str.
 *	@return <i>-1</i> on failure.<br>
 *	@return The <i>cnt</i> of colons on success.
 */
static u_int
count_colons(bk_s B, const char *str, const char *str_end)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  u_int cnt = 0;
  char c;

  if (!str)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  while(str < str_end && *str)
  {
    if (*str == ':') 
      cnt++;
    str++;
  }

  BK_RETURN(B,cnt);
}
