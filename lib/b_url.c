#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_url.c,v 1.18 2002/03/06 22:51:47 dupuy Exp $";
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



#define STORE_URL_ELEMENT(B, mode, element, start, end)			  \
do {									  \
  switch(mode)								  \
  {									  \
  case BkUrlParseVptr:							  \
  case BkUrlParseVptrCopy:						  \
    if (start)								  \
    {									  \
      (element).bue_vptr.ptr = ((char *)(start));			  \
      (element).bue_vptr.len = ((end) - (start));			  \
    }									  \
    break;								  \
  case BkUrlParseStrEmpty:						  \
    if (!(start))							  \
    {									  \
      if (!((element).bue_str = strdup("")))				  \
      {									  \
	bk_error_printf(B, BK_ERR_ERR,					  \
			"Could not strdup an element of url: %s\n",	  \
			strerror(errno));				  \
	goto error;							  \
      }									  \
    }									  \
    /* Intentional fall through. */					  \
  case BkUrlParseStrNULL:						  \
    if (start)								  \
    {									  \
      if (!((element).bue_str = bk_strndup((B), (start), (end)-(start)))) \
      {									  \
	bk_error_printf(B, BK_ERR_ERR,					  \
			"Could not strdup an element of url: %s\n",	  \
			strerror(errno));				  \
	goto error;							  \
      }									  \
    }									  \
    break;								  \
  default:								  \
    bk_error_printf(B, BK_ERR_ERR, "Unknown mode: %d\n", (mode));	  \
    break;								  \
  }									  \
} while(0)


#define FREE_URL_ELEMENT(bu, element)		\
{						\
  if (!BK_URL_IS_VPTR(bu))			\
  {						\
    free(BK_URL_DATA((bu),(element)));		\
  }						\
  memset(&(element), 0, sizeof(element));	\
}




/**
 * Parse a url in a roughly rfc2396 compliant way.  This is to say: what it
 * does is rfc2396 compliant; it doesn't go the whole 9 yards (interpreting
 * relative paths w.r.t. path of base document).  Place results in returned
 * structure with any missing values set to NULL.  This function sets the
 * BK_URL_foo flag for each section foo which is actually located.  If
 * BK_URL_STRICT_PARSE is <em>not</em> set in the flags passed to this
 * function, it will attempt to apply some fuzzy (non-rfc2396 compliant) logic
 * to make URLs like "foobar.baka.org" come out more like you might expect in a
 * network environment.
 *
 * <TODO> Explain modes, rationalize them w.r.t flags </TODO>
 *
 * The regex (adapted from rfc2396) that this implements is:
 *	http://www.ics.uci.edu/pub/ietf/uri/#Related
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
 *	@param B BAKA thread/global state.
 *	@param url Url to parse.
 *	@param mode The mode of parsing.
 *	@param flags Flags for the future.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a bk_url on success.
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
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }
  
  if (!(bu = bk_url_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "could not create url struct\n");
    goto error;
  }
  
  bu->bu_mode = mode;
  
  // Cache the end of the url since we use it a lot.
  url_end = url + strlen(url);

  // Search for scheme.
  start = url;

  // The inclusion of [ is compliant with rfc2732 ipv6 literal address parsing
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
   * The following is safe. start is guaranteed to point at something valid
   * (although perhaps '\0') and if *start == '/' then *(start+1) is
   * guaranteed to point at something valid (though, again, it might be
   * '\0').
   */
  if (*start == '/' && *(start+1) == '/')
  {
    start += 2;
    authority = start;
    authority_end = strpbrk(start, "/?#");
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
    path_end = strpbrk(start, "?#");
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
    query_end = strpbrk(start, "#");
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
    // Do BAKA fuzzy URL logic (basically update relative paths).

    /*
     * <WARNING>
     * Ordering in the fuzzy logic section is important. For instance you
     * want to make sure you promote (and demote too I suppose) all info
     * into (out of) the authority section before creating the host/serv
     * thingies.
     * </WARNING>
     */


    /*
     * If we have a relative path and no authority component (not that we
     * *can* have an authority component *without* an absolute path :-)),
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
	authority_end = strpbrk(authority, "/?#");
	if (!authority_end)
	  authority_end = path_end;

	/*
	 * You must free this even though it does not appear to be set
	 * owing to the fact that in BkUrlParseStrEmpty mode all ptrs are
	 * strdup'ed to "" even if unset.
	 */
	FREE_URL_ELEMENT(bu, bu->bu_authority);
	STORE_URL_ELEMENT(B, mode, bu->bu_authority, authority, authority_end);
	BK_FLAG_SET(bu->bu_flags, BK_URL_FLAG_AUTHORITY);

	BK_FLAG_CLEAR(bu->bu_flags, BK_URL_FLAG_PATH);

	if (authority_end != path_end)
	{
	  STORE_URL_ELEMENT(B, mode, bu->bu_path, authority_end, path_end);
	  BK_FLAG_SET(bu->bu_flags, BK_URL_FLAG_PATH);
	  FREE_URL_ELEMENT(bu, hold);
	}
	else
	{
	  FREE_URL_ELEMENT(bu, bu->bu_path);
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
	if (!(host_end = strpbrk(host, "]/?#")) || *host_end != ']')
	{
	  bk_error_printf(B, BK_ERR_ERR, "Malformed ipv6 address\n");
	  goto error;
	}
      }
      else
      {
	host_end = host;
      }

      if ((serv = strpbrk(host_end, ":/?#")))
      {
	if (*serv == ':')
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
	    host_end = serv;
	  serv = NULL;
	}
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
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
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
 * Expand % escapes in a component of URL.
 *
 * This function creates a copy of a string with rfc2396 % escapes expanded.
 * For security reasons, % escapes that encode control characters (i.e. %00
 * through %1F, and %7F) are <em>not</em> expanded.  Illegal % escapes
 * (e.g. %GB, %%) are not altered (the string "%%20" is expanded as "% ", as
 * the first % is not a legal escape, but the second one is).
 *
 * <TODO>This should be integrated with bk_url_parse and enabled/disabled with
 * a flag, but it is incompatible with the BkUrlParseVptr mode, since a copy of
 * the string must be made to expand % escapes.  In general, the mode vs. flags
 * arguments of bk_url_parse need to be rationalized.</TODO>
 *
 *	@param B BAKA thread/global state.
 *	@param component The url component string (may be NULL)
 *	@return <i>NULL</i> on failure<br>
 *	@return a new @a bk_url on success.
 */
char *
bk_url_unescape(bk_s B, const char *component)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *expanded;
  char *copy;

  if (!component)				// handle this gracefully
    BK_RETURN(B, calloc(1,1));

  if (!(expanded = malloc(strlen(component) + 1))) // may be a bit too big, ok
  {
    bk_error_printf(B, BK_ERR_ERR, "could not allocate unescaped URL component\n");
    BK_RETURN(B, NULL);
  }

  for (copy = expanded; *component; copy++, component++)
  {
    if (*component == '%' && isxdigit(component[1]) && isxdigit(component[2]))
    {
      char convert[3];
      int val;
      convert[0] = component[1];
      convert[1] = component[2];
      convert[2] = '\0';
      if (!iscntrl((val = strtoul(convert, NULL, 16))))
      {
	*copy = val;
	component += 2;				// advance over hex digits
	continue;
      }						// else fall through
    }

    *copy = *component;				// default action
  }

  *copy = '\0';					// finish expanded
  BK_RETURN(B, expanded);
}



/**
 * Parse semicolon delimited parameters of URL.
 *
 * This function, whose interface and implementation are brazenly stolen from
 * getsubopt(), parses URL parameters from a path component which has been
 * extracted from a URL by @a bk_url_parse.
 *
 * These parameters must be separated by semicolons and may consist of either a
 * single token, or a token-value pair separated by an equal sign.  Because
 * semicolons delimit parameters in the path, they are not allowed to be part
 * of the parameter name or the value of a parameter (semicolons can be escaped
 * as %3B).  Similarly, because the equal sign separates a parameter name from
 * its value, a parameter name must not contain an equal sign (equals signs can
 * be escaped as %3D).  For correct parsing when escaped semicolon or equals
 * are present, the supplied path string should <em>not</em> be unescaped.
 *
 * This function takes the address of a pointer to the path string, an array of
 * possible tokens, and the address of a value string pointer.  If the path
 * string at @a *pathp contains only one parameter, this function updates @a
 * *pathp to point to the null at the end of the string.  Otherwise, it
 * isolates the parameter by replacing the semicolon separator with a null, and
 * updates @a *pathp to point to the start of the next parameter.  If the
 * parameter has an associated value, this function updates @a *valuep to point
 * to the value's first character.  Otherwise it sets @a *valuep to a null
 * pointer.
 *
 * The token array is organised as a series of pointers to strings.  The end
 * of the token array is identified by a null pointer.
 *
 * On a successful return, if @a *valuep is not a null pointer then the
 * parameter processed included a value.  The calling program may use this
 * information to determine if the presence or lack of a value for this
 * parameter is an error.  Note that the value string should be passed to @a
 * bk_url_unescape to expand any embedded % escapes.
 *
 * If the parameter does not match any tokens in the tokens array, this
 * function updates @a *valuep to point to the unknown parameter (including
 * value, if any).  The calling program should decide if this is an error, or
 * if the unrecognised option should be passed on to another program.
 *
 *	@param B BAKA thread/global state.
 *	@param pathp pointer to path component (passed by reference).
 *	@param tokens array of recognized parameter names, terminated by NULL.
 *	@param valuep pointer to value of parameter (passed by reference).
 *	@return <i>-1</i> on failure (bad arguments, unrecognized token) or<br>
 *	@return index of the matched token string.
 *
 * @see getsubopt(3)
 *
 * Note that the returning a pointer to the bad parameter in *valuep is a GNU C
 * library or Linux extension to the X/Open @a getsubopt standard.
 */
int
bk_url_getparam(bk_s B, char **pathp, char * const *tokens, char **valuep)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *param;
  char *p;
  int cnt;

  if (!valuep)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  param = *valuep = NULL;

  if (!pathp || !*pathp)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  /* skip leading white-space, semicolons */
  for (p = *pathp; *p && (*p == ';' || *p == ' ' || *p == '\t'); ++p)
    /* empty */;

  if (!*p) 
  {
    *pathp = p;
    BK_RETURN(B, -1);
  }

  /* save the start of the token, and skip the rest of the token */
  for (param = p;
       *++p && *p != ';' && *p != '=' && *p != ' ' && *p != '\t';);

  if (*p) 
  {
    /*
     * If there's an equals sign, set the value pointer, and
     * skip over the value part of the token.  Terminate the
     * token.
     */
    if (*p == '=') 
    {
      *p = '\0';
      for (*valuep = ++p;
	   *p && *p != ';' && *p != ' ' && *p != '\t'; ++p);
      if (*p) 
	*p++ = '\0';
    } 
    else
      *p++ = '\0';
    /* Skip any whitespace or semicolons after this token. */
    for (; *p && (*p == ';' || *p == ' ' || *p == '\t'); ++p)
      /* empty */;
  }

  /* set pathp for next round */
  *pathp = p;

  for (cnt = 0; *tokens; ++tokens, ++cnt)
    if (!strcmp(param, *tokens))
      BK_RETURN(B, cnt);

  /* if parameter unrecognized, try unescaping and rescan if different */

  if ((p = bk_url_unescape(B, param)) && strcmp(p, param))
    for (cnt = 0; *tokens; ++tokens, ++cnt)
      if (!strcmp(param, *tokens))
      {
	free(p);
	BK_RETURN(B, cnt);
      }

  if (p)
    free(p);

  /* on unrecognized param, restore '=' (if any), set *valuep to bad param */
  if (*valuep)
    (*valuep)[-1] = '=';
  *valuep = param;
  BK_RETURN(B, -1);
}
