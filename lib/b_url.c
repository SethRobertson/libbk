#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_url.c,v 1.9 2001/12/21 21:28:44 jtt Exp $";
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


#define STORE_URL_ELEMENT(B, mode, element, start, end)					\
do {											\
  switch(mode)										\
  {											\
  case BkUrlParseVptr:									\
  case BkUrlParseVptrCopy:								\
    if (start)										\
    {											\
      (element).bue_vptr.ptr = ((char *)(start));					\
      (element).bue_vptr.len = ((end) - (start));					\
    }											\
    break;										\
  case BkUrlParseStrEmpty:								\
    if (!(start))									\
    {											\
      if (!((element).bue_str = strdup("")))						\
      {											\
	bk_error_printf(B, BK_ERR_ERR, "Could not strdup an element of url: %s\n",	\
			strerror(errno));						\
	goto error;									\
      }											\
    }											\
    /* Intetional fall through. */							\
  case BkUrlParseStrNULL:								\
    if (start)										\
    {											\
      if (!((element).bue_str = bk_strndup((B), (start), (end)-(start))))		\
      {											\
	bk_error_printf(B, BK_ERR_ERR, "Could not strdup an element of url: %s\n",	\
			strerror(errno));						\
	goto error;									\
      }											\
    }											\
    break;										\
  default:										\
    bk_error_printf(B, BK_ERR_ERR,"Unknown mode: %d\n", (mode));			\
    break;										\
  }											\
} while(0)


#define FREE_URL_ELEMENT(bu, mode, element)	\
{						\
  if (!BK_URL_IS_VPTR(bu))			\
  {						\
    free(BK_URL_DATA((bu),(element)));		\
  }						\
  memset(&(element), 0, sizeof(element));	\
}




/**
 * Parse a url in a roughly rfc2396 compliant way. This is to say: what it
 * does is rfc2396 compliant; it doesn't go the whole 9 yards. Place
 * results in returned structure with undeterminable value set to
 * NULL. This funtion sets the BK_URL_foo flag for each sectin foo which is
 * actually located. If the flag BK_URL_STRICT_PARSE is <em>not</em> set,
 * the function will then attempt to apply some fuzzy (non-rfr2396
 * compliant) logic to make things URL's like "foobar.baka.org" come uot
 * more like you might expect in a network enviornment.
 *
 * The RE (from rfc 2396) which we implement is:
 *     ^(([^:/?#]+):)?(//([^/?#]*))?([^?#]*)(\?([^#]*))?(#(.*))?
 *      12            3  4          5       6  7        8 9
 *
 *     $1 = http:
 *     $2 = http
 *     $3 = //www.ics.uci.edu
 *     $4 = www.ics.uci.edu
 *     $5 = /pub/ietf/uri/
 *     $6 = <undefined>
 *     $7 = <undefined>
 *     $8 = #Related
 *     $9 = Related
 *
 * Basic URI looks like: <scheme>://<authority><path>?<query>#<fragment>
 *
 *
 *	@param B BAKA thread/global state.
 *	@param url Url to parse.
 *	@param flags Flags for the future.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a bk_url on sucess.
 */
struct bk_url *
bk_url_parse(bk_s B, const char *url, bk_url_parse_mode_e mode, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_url *bu = NULL;
  const char *url_end = NULL;
  const char *scheme = NULL, *scheme_end = NULL;
  const char *authority = NULL, *authority_end = NULL;
  const char *path = NULL, *path_end = NULL;
  const char *query = NULL, *query_end = NULL;
  const char *fragment = NULL, *fragment_end = NULL;
  const char *start, *end;
  

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
  
  bu->bu_mode = mode;
  
  // Cache the end of the url since we use it alot.
  url_end = url + strlen(url);

  // Search for scheme.
  start = url;
  /*
   * <WANRNING> 
   * The inclusion if [ is *not* RFC compliant, but necessary to enforce BAKA ipv6 conventions.
   * </WANRNING> 
   */
  end = strpbrk(start, ":/?#[");
  
  if (end && *end == ':')
  {
    // We demand at least one char before the ':'
    if (end > start)
    {
      BK_FLAG_SET(bu->bu_flags, BK_URL_FLAG_SCHEME); // Mark that scheme is set.
      scheme = start;
      scheme_end = end;
    }
  }

  if (scheme_end)
  {
    start = scheme_end + 1;
  }

  
  /*
   * The following is safe. start is gaurenteed to point at something valid
   * (although perhaps '\0') and if *start == '/' then *(start+1) is
   * guarenteed to point at something valid (though, again, it might be
   * '\0').
   */
  if (*start == '/' && *(start+1) == '/')
  {
    start += 2;
    authority = start;
    authority_end = strpbrk(start,"/?#");
    if (!authority_end)
      authority_end = url_end;
    if (authority_end == authority)
    {
      authority = NULL;
      authority_end = NULL;
    }
    else
    {
      BK_FLAG_SET(bu->bu_flags, BK_URL_FLAG_AUTHORITY); // Mark that authority is set.
    }
  }

  if (authority)
  {
    start = authority_end;
  }

  if (*start)
  {
    path = start;
    path_end = strpbrk(start,"?#");
    if (!path_end)
      path_end = url_end;
    if (path_end == path)
    {
      path = NULL;
      path_end = NULL;
    }
    else
    {
      BK_FLAG_SET(bu->bu_flags, BK_URL_FLAG_PATH); // Mark that path is set.
    }
  }

  if (path)
  {
    start = path_end;
  }

  if (*start == '?')
  {
    query = start + 1;
    query_end = strpbrk(start,"#");
    if (!query_end)
      query_end = url_end;
    if (query_end == query)
    {
      query = NULL;
      query_end = NULL;
    }
    else
    {
      BK_FLAG_SET(bu->bu_flags, BK_URL_FLAG_QUERY); // Mark that query is set.
    }
  }

  if (query)
  {
    start = query_end;
  }

  if (*start == '#')
  {
    fragment = start + 1;
    fragment_end = url_end;
    if (fragment_end == fragment)
    {
      fragment = NULL;
      fragment_end = NULL;
    }
    else
    {
      BK_FLAG_SET(bu->bu_flags, BK_URL_FLAG_FRAGMENT); // Mark that fragment is set.
    }
  }

  // Save URL in the right way.
  if (mode == BkUrlParseVptr)
  {
    bu->bu_url = (char *)url;
  }
  else
  {
    if (!(bu->bu_url = strdup(url)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not strdup url: %s\n", strerror(errno));
      goto error;
    }
  }

  // Save all the data.
  // <WARNING> odious "goto error" hidden in this macro </WARNING>
  STORE_URL_ELEMENT(B, mode, bu->bu_scheme, scheme, scheme_end);
  STORE_URL_ELEMENT(B, mode, bu->bu_authority, authority, authority_end);
  STORE_URL_ELEMENT(B, mode, bu->bu_path, path, path_end);
  STORE_URL_ELEMENT(B, mode, bu->bu_query, query, query_end);
  STORE_URL_ELEMENT(B, mode, bu->bu_fragment, fragment, fragment_end);


  if (BK_FLAG_ISCLEAR(flags, BK_URL_FLAG_STRICT_PARSE))
  {
    // Do BAKA fuzzy URL logic (basically update relavtive paths).

    /*
     * <WARNING>
     * Ordering in the fuzzy logic section is important. For instance you
     * want to make sure you promote (and demote too I suppose) all info
     * into (out of) the authority section before creatin the host serv
     * thingys.
     * </WARNING>
     */


    /*
     * If we have a relative path and no authority component (not that we
     * *can* have an autority component *without* an aboslute path :-)),
     * then "promote" the first path component to authority.
     */
    if (BK_FLAG_ISCLEAR(bu->bu_flags, BK_URL_FLAG_AUTHORITY) &&
	BK_FLAG_ISSET(bu->bu_flags, BK_URL_FLAG_PATH))
    {
      union bk_url_element_u hold;

      hold = bu->bu_path;			// Structure copy
      path = BK_URL_PATH_DATA(bu);
      path_end = path + BK_URL_PATH_LEN(bu);
      if (*path != '/')
      {
	// Relative path.
	authority = path;
	authority_end = strchr(authority,'/');
	if (!authority_end)
	  authority_end = path_end;

	STORE_URL_ELEMENT(B, mode, bu->bu_authority, authority, authority_end);
	BK_FLAG_SET(bu->bu_flags, BK_URL_FLAG_AUTHORITY);

	BK_FLAG_CLEAR(bu->bu_flags, BK_URL_FLAG_PATH);

	if (authority_end != path_end)
	{
	  STORE_URL_ELEMENT(B, mode, bu->bu_path, authority_end, path_end);
	  BK_FLAG_SET(bu->bu_flags, BK_URL_FLAG_PATH);
	  FREE_URL_ELEMENT(bu, bu->bu_mode, hold);
	}
	else
	{
	  FREE_URL_ELEMENT(bu, bu->bu_mode, bu->bu_path);
	}
      }

    }

    // Build host/serv sections 
    if (BK_FLAG_ISSET(bu->bu_flags, BK_URL_FLAG_AUTHORITY))
    {
      const char *host = NULL, *host_end = NULL;
      const char *serv = NULL, *serv_end = NULL;

      host = BK_URL_AUTHORITY_DATA(bu);
      if (*host == '[')
      {
	host++;
	// ipv6 address (we're mandating square brackets around ipv6's).
	if (!(host_end = strchr(host, ']')))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Malformed ipv6 address\n");
	  goto error;
	}
      }
      else
      {
	host_end = host;
      }

      if ((serv = strchr(host_end, ':')))
      {
	if (host_end == host)
	{
	  host_end = serv;
	}
	serv++;
	BK_FLAG_SET(bu->bu_flags, BK_URL_FLAG_SERV);
	serv_end = (BK_URL_AUTHORITY_DATA(bu) + BK_URL_AUTHORITY_LEN(bu));
      }
      else
      {
	if (host_end == host)
	  host_end = (BK_URL_AUTHORITY_DATA(bu) + BK_URL_AUTHORITY_LEN(bu));
      }
	
      if (host_end != host)
      {
	BK_FLAG_SET(bu->bu_flags, BK_URL_FLAG_HOST);
      }

      STORE_URL_ELEMENT(B, mode, bu->bu_host, host, host_end);
      STORE_URL_ELEMENT(B, mode, bu->bu_serv, serv, serv_end);
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

  if (bu->bu_mode != BkUrlParseVptr && bu->bu_url)
    free(bu->bu_url);
      
  if (bu->bu_mode == BkUrlParseStrNULL || bu->bu_mode == BkUrlParseStrEmpty)
  {
    if (bu->bu_scheme.bue_str) 
      free(bu->bu_scheme.bue_str);

    if (bu->bu_authority.bue_str) 
      free(bu->bu_authority.bue_str);

    if (bu->bu_path.bue_str) 
      free(bu->bu_path.bue_str);

    if (bu->bu_query.bue_str) 
      free(bu->bu_query.bue_str);

    if (bu->bu_fragment.bue_str) 
      free(bu->bu_fragment.bue_str);

    if (bu->bu_host.bue_str) 
      free(bu->bu_host.bue_str);

    if (bu->bu_serv.bue_str) 
      free(bu->bu_serv.bue_str);
  }

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
