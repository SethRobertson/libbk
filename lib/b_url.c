#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_url.c,v 1.3 2001/12/11 01:56:26 seth Exp $";
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
 * Parse a url
 *
 *	@param B BAKA thread/global state.
 *	@param url_in Url to parse.
 *	@param flags Flags for the future.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a bk_url on sucess.
 */
struct bk_url *
bk_url_parse(bk_s B, const char *url, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_url *bu = NULL;
  const char *host=NULL, *host_end;
  const char *proto=NULL, *proto_end;
  const char *serv=NULL, *serv_end=NULL;
  const char *path=NULL, *path_end;
  u_int cnt;
  u_int len;

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
  proto_end = bk_strdelim(B, proto,":/");	/* No : or / in proto */
  if (!proto_end || strncmp(proto_end,":/", 2) != 0) /* Proto ends with ":/" (at a minimum) */
  {
    proto = NULL;				/* No protocol */
    host = url;					/* Try host from begining */
  }
  else
  {
    host = proto_end+1;				/* Skip over : but *not* / */
  }
  
  if (strncmp(host,"//",2) == 0) 		/* Skip over // */
  {
    host += 2;
  }

  // XXX - IPv6 sucks.  Just look for "/" and then much later, when you know if you are focused at a host section,
  // XXX   reprocess and count colons (N.B. you probably will want count_colons take a host_end ptr, or a length).
  if ((host_end = bk_strdelim(B, host,":/")))
  {
    if (*host_end == ':')			/* Host ends with : (or EOS) */
    {
      serv = host_end+1;			/* Skip over : */
      serv_end = bk_strdelim(B, serv,":/");
      if (serv_end && *serv_end == ':')
      {
	bk_error_printf(B, BK_ERR_ERR, "Malfomed URL: <%s>\n", url);
	goto error;
      }
      path = serv_end;
    }
    else
    {
      path = host_end;
    }
  }
  else
  {
    path = host;
  }

  if (path)
  {
    if (*path != '/')
    {
      path=NULL;
    }
    else if (path == host)
    {
      if (serv)
      {
	bk_error_printf(B, BK_ERR_ERR, "I seem have a service spec with no host spec. How can that happen\n");
	goto error;
      }
      host = NULL;
    }
  }

  if (host)
  {
    // XXX - in case of foo://bar -- host_end is not set
    // XXX - note, "later" as defined above, has arrived
    len=host_end-host+1;
    if (!(bu->bu_host = bk_strndup(B, host, len)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not copy host: %s\n", strerror(errno));
      goto error;
    }
  }
  else
  {
    if (!(bu->bu_host = strdup("")))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not copy empty string to host: %s\n", strerror(errno));
      goto error;
    }
  }

  if (proto)
  {
    len=proto_end-proto+1;
    if (!(bu->bu_proto = bk_strndup(B, proto, len)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not copy proto: %s\n", strerror(errno));
      goto error;
    }
  }
  else
  {
    if (!(bu->bu_proto = strdup("")))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not copy empty string to proto: %s\n", strerror(errno));
      goto error;
    }
  }

  if (serv)
  {
    len=serv_end-serv+1;
    if (!(bu->bu_serv = bk_strndup(B, serv, len)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not copy serv: %s\n", strerror(errno));
      goto error;
    }
  }
  else
  {
    if (!(bu->bu_serv = strdup("")))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not copy empty string to serv: %s\n", strerror(errno));
      goto error;
    }
  }

  if (path)
  {
    if (!(bu->bu_path=strdup(path)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not strdup path: %s\n", strerror(errno));
      goto error;
    }
  }
  else
  {
    if (!(bu->bu_path=strdup("")))
    {
      bk_error_printf(B, BK_ERR_ERR, "Couldnot strdup empty string to path: %s\n", strerror(errno));
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
