#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_url.c,v 1.34 2003/06/17 06:07:17 seth Exp $";
static const char libbk__copyright[] = "Copyright (c) 2003";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright LIBBK++
 *
 * Copyright (c) 2003 The Authors. All rights reserved.
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


#define PARAM_DELIM ';'


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
 * Parse a url in a roughly RFC 2396 compliant way.  This is to say: what it
 * does is Rfc 2396 compliant; it doesn't go the whole 9 yards (interpreting
 * relative paths w.r.t. path of base document).  Place results in returned
 * structure with any missing values set to NULL.  This function sets the
 * BK_URL_foo flag for each section foo which is actually located.
 *
 * If BK_URL_FLAG_STRICT_PARSE is <em>not</em> set in the flags passed to
 * this function, it will attempt to apply some fuzzy (non-RFC 2396
 * compliant) logic to make URLs like "wump:foobar.baka.org" come out more
 * like James and Seth might expect in a network environment.  The only
 * time you would <em>not</em> want BK_URL_FLAG_STRICT_PARSE is if you are
 * implementing a protocol like *UMP; if the URL is supposed to identify a
 * resource object, rather than a server, you want strict RFC 2396 parsing.
 *
 * <TODO>Explain modes, rationalize them w.r.t flags</TODO>
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
 * THREADS: MT-SAFE
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
    /*
     * If we see scheme:///, we don't NULL out authority, but we don't set the
     * BK_URL_FLAG_AUTHORITY either.  This allows us to distinguish scheme:///
     * (except for BkUrlParseStrEmpty mode).
     */
    if (authority_end != authority)
      BK_FLAG_SET(bu->bu_flags, BK_URL_FLAG_AUTHORITY); // non-empty authority
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

  BK_RETURN(B,bu);

 error:
  if (bu) bk_url_destroy(B, bu);
  BK_RETURN(B,NULL);
}



/**
 * Create a url structure and clean it out.
 *
 * THREADS: MT-SAFE
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
 * THREADS: MT-SAFE
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
 * the first % is not a legal escape, but second one is).
 *
 * <TODO>This should be integrated with bk_url_parse and enabled/disabled with
 * a flag, but it is incompatible with the BkUrlParseVptr mode, since a copy of
 * the string must be made to expand % escapes.  In general, the mode vs. flags
 * arguments of bk_url_parse need to be rationalized.</TODO>
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param component The url component string (may be NULL)
 *	@return <i>NULL</i> on failure<br>
 *	@return unescaped, NUL-terminated, string on success
 */
char *
bk_url_unescape(bk_s B, const char *component)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!component)				// handle this gracefully
    BK_RETURN(B, calloc(1,1));

  BK_RETURN(B, bk_url_unescape_len(B, component, strlen(component)));
}



/**
 * Expand % escapes in a component of URL.
 *
 * This function creates a length-bounded copy of a string with rfc2396 %
 * escapes expanded.  For security reasons, % escapes that encode control
 * characters (i.e. %00 through %1F, and %7F) are <em>not</em> expanded.
 * Illegal % escapes (e.g. %GB, %%) are not altered (the string "%%20" is
 * expanded as "% ", as the first % is not a legal escape, but second one is).
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param component The url component string (may be NULL)
 *	@param len Length of url component string
 *	@return <i>NULL</i> on failure<br>
 *	@return unescaped, NUL-terminated, string on success
 */
char *
bk_url_unescape_len(bk_s B, const char *component, size_t len)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *expanded;
  char *copy;

  if (!component)				// handle this gracefully
    BK_RETURN(B, calloc(1,1));

  if (!(expanded = malloc(len + 1))) // may be a bit too big, ok
  {
    bk_error_printf(B, BK_ERR_ERR, "could not allocate unescaped URL component\n");
    BK_RETURN(B, NULL);
  }

  for (copy = expanded; *component && len--; copy++, component++)
  {
    if (*component == '%' && isxdigit(component[1]) && isxdigit(component[2]))
    {
      char convert[3];
      int val;
      convert[0] = component[1];
      convert[1] = component[2];
      convert[2] = '\0';
      // escaped control characters are silently omitted
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
 * Parse delimited parameters of URL.
 *
 * This function, whose interface and implementation are brazenly stolen from
 * getsubopt(), parses URL parameters from a path component which has been
 * extracted from a URL by @a bk_url_parse.
 *
 * These parameters must be separated by delimiters and may consist of either a
 * single token, or a token-value pair separated by an equal sign.  Because
 * delimiters delimit parameters in the path, they are not allowed to be part
 * of the parameter name or the value of a parameter (delimiters can be escaped
 * using %).  Similarly, because the equal sign separates a parameter name from
 * its value, a parameter name must not contain an equal sign (equals signs can
 * be escaped as %3D).  For correct parsing when escaped delimiter or equals
 * are present, the supplied path string should <em>not</em> be unescaped.
 *
 * This function takes the address of a pointer to the path string, an array of
 * possible tokens, and the address of a value string pointer.  If the path
 * string at @a *pathp contains only one parameter, this function updates @a
 * *pathp to point to the null at the end of the string.  Otherwise, it
 * isolates the parameter by replacing the delimiter with a null, and updates
 * @a *pathp to point to the start of the next parameter.  If the parameter has
 * an associated value, this function updates @a *valuep to point to the
 * value's first character.  Otherwise it sets @a *valuep to a null pointer.
 *
 * The token array is organised as a series of pointers to strings.  The end
 * of the token array is identified by a null pointer.  An empty token ("")
 * (except as the first token) indicates that all following tokens are case
 * independent
 * ignored (except for specifying a delimiter as noted below).
 *
 * The default delimiter is ';' (semicolon).  If an alternate delimiter is
 * desired, the first token should be the string NUL delim NUL (e.g. to specify
 * ',' (comma) as delimiter, the first token should be "\0,".
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
 * THREADS: MT-SAFE
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
  char delim = PARAM_DELIM;
  int (*compare)(const char *, const char *) = strcmp;
  char * const *tokenstart;
  char *param;
  char *p;
  int cnt;
  extern int strcasecmp (const char *__s1, const char *__s2);

  if (!valuep)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  param = *valuep = NULL;

  if (!tokens || !pathp || !*pathp)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  /* check for alternate delimiter */
  if (*tokens && (*tokens)[0] == '\0'
      && (*tokens)[1] != '\0' && (*tokens)[2] == '\0')
    delim = (*tokens++)[1];

  /* skip leading white-space, delimiters */
  for (p = *pathp; *p && (*p == delim || *p == ' ' || *p == '\t'); ++p)
    /* empty */;

  if (!*p)
  {
    *pathp = p;
    BK_RETURN(B, -1);
  }

  /* save the start of the token, and skip the rest of the token */
  for (param = p; *++p && *p != delim && *p != '=';)
    /* empty */;

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
	   *p && *p != delim; ++p);
      if (*p)
	*p++ = '\0';
    }
    else
      *p++ = '\0';
    /* Skip any delimiters after this token. */
    for (; *p && *p == delim; ++p)
      /* empty */;
  }

  /* set pathp for next round */
  *pathp = p;

  if (*param)					// never match "empty" token
  {
    tokenstart = tokens;

    for (cnt = 0; *tokens; ++tokens, ++cnt)
    {
      if (!**tokens)				// empty token => case indep.
	compare = strcasecmp;
      if (!(*compare)(param, *tokens))
	BK_RETURN(B, cnt + (delim != PARAM_DELIM));
    }

    /* if parameter unrecognized, try unescaping and rescan if different */

    if ((p = bk_url_unescape(B, param)) && strcmp(p, param))
    {
      compare = strcmp;				// reset case dependent
      tokens = tokenstart;			// restart token list

      for (cnt = 0; *tokens; ++tokens, ++cnt)
      {
	if (!**tokens)				// empty token => case indep.
	  compare = strcasecmp;
	if (!(*compare)(p, *tokens))
	{
	  free(p);
	  BK_RETURN(B, cnt + (delim != PARAM_DELIM));
	}
      }
    }
    if (p)
      free(p);
  }

  /* on unrecognized param, restore '=' (if any), set *valuep to bad param */
  if (*valuep)
    (*valuep)[-1] = '=';
  *valuep = param;
  BK_RETURN(B, -1);
}



/**
 * Parse the authority section of a url assuming the
 * format is: [[user[:password]@]server[:port]]
 * char* members of bk_url_authority are guaranteed to be non-null.
 * Caller must free the returned value with bk_url_authority_free
 *
 * THREADS: MT-SAFE
 *
 * @param B BAKA Thread/global astate
 * @param auth_str NULL-terminated authority string to parse
 * @param flags reserved
 * @return <i>NULL</i> on failure.
 * @return a new @ bk_authority on sucess
 */
struct bk_url_authority *
bk_url_parse_authority(bk_s B, const char *auth_str, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_url_authority *auth = NULL;
  char *amp = NULL;
  char *col = NULL;

  if (!auth_str)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    goto error;
  }

  if (!BK_CALLOC(auth))
  {
    bk_error_printf(B, BK_ERR_ERR, "malloc failed\n");
    goto error;
  }

  // userinfo
  if (amp = strchr(auth_str, '@'))
  {
    u_int16_t userdata_len = amp - auth_str;	// don't include @
    if (col = (char*) memchr(auth_str, ':', userdata_len))
    {
      u_int16_t user_len = col - auth_str;	// don't include :
      auth->auth_user = bk_strndup(B, auth_str, user_len);
      auth->auth_pass = bk_strndup(B, col + 1, userdata_len - user_len - 1);
    }
    else
    {
      // whole userdata is just the user
      auth->auth_user = bk_strndup(B, auth_str, userdata_len);
    }
    auth_str = amp + 1;
  }

  // host and port
  if (col = strchr(auth_str, ':'))
  {
    auth->auth_host = bk_strndup(B, auth_str, col - auth_str);
    auth->auth_port = strdup(col + 1);
  }
  else
  {
    // no port, only host
    auth->auth_host = strdup(auth_str);
  }

  BK_RETURN(B, auth);

 error:
  if (auth)
    bk_url_authority_destroy(B, auth);

  BK_RETURN(B, NULL);
}



/**
 * Destroy a bk_url_authority
 *
 * THREADS: MT-SAFE
 *
 * @param B BAKA Thread/global state
 * @param auth ptr to struct
 */
void
bk_url_authority_destroy(bk_s B, struct bk_url_authority *auth)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (auth)
  {
    if (auth->auth_user)
      free(auth->auth_user);

    if (auth->auth_pass)
      free(auth->auth_pass);

    if (auth->auth_host)
      free(auth->auth_host);

    if (auth->auth_port)
      free(auth->auth_port);

    free(auth);
  }

  BK_VRETURN(B);
}



/**
 * Reconstruct a URL from its parts, allowing the caller to specify those
 * parts in which s/he is interested. Returns a malloc(3)'ed string.
 *
 * <WARNING id="1178">Although the handling of non-generic URLs has been
 * improved, this function may still generate incorrect URLs for non-generic
 * schemes; don't use this unless you know the scheme type and have validated
 * the reconstructed URLs for all variants.</WARNING>
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param bu The parsed url
 *	@param sections A bit field containing a list of the desired sections
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>url string</i> on success.
 */
char *
bk_url_reconstruct(bk_s B, struct bk_url *bu, bk_flags sections, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *url = NULL;
  char *tmp;
  int len, tmp_len;
  char *insert;

  if (!bu || !BK_URL_SCHEME_DATA(bu))
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  // Space for scheme and "://" and NUL
  len = strlen(BK_URL_SCHEME_DATA(bu)) + 4;

  if (!(BK_MALLOC_LEN(url, len)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate space for scheme: %s\n", strerror(errno));
    goto error;
  }

  if (!BK_URL_AUTHORITY_DATA(bu))
    snprintf(url, len, "%s:", BK_URL_SCHEME_DATA(bu));
  else
  {
    snprintf(url, len, "%s://", BK_URL_SCHEME_DATA(bu));

    tmp_len = strlen(BK_URL_AUTHORITY_DATA(bu));
    if (!(tmp = realloc(url, len + tmp_len)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not realloc space for athority section: %s\n", strerror(errno));
      goto error;
    }
    url = tmp;
    insert = url + strlen(url);
    snprintf(insert, tmp_len+1, "%s", BK_URL_AUTHORITY_DATA(bu));
    len += tmp_len;
  }


  if (BK_FLAG_ISSET(sections, BK_URL_FLAG_PATH) &&  BK_URL_PATH_DATA(bu))
  {
    tmp_len = strlen(BK_URL_PATH_DATA(bu));
    if (!(tmp = realloc(url, len + tmp_len)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not realloc space for path section: %s\n", strerror(errno));
      goto error;
    }
    url = tmp;
    insert = url + strlen(url);
    snprintf(insert, tmp_len+1, "%s", BK_URL_PATH_DATA(bu));
    len += tmp_len;
  }

  if (BK_FLAG_ISSET(sections, BK_URL_FLAG_QUERY) &&  BK_URL_QUERY_DATA(bu))
  {
    tmp_len = strlen(BK_URL_QUERY_DATA(bu)) + 1;
    if (!(tmp = realloc(url, len + tmp_len)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not realloc space for query section: %s\n", strerror(errno));
      goto error;
    }
    url = tmp;
    insert = url + strlen(url);
    snprintf(insert, tmp_len+1, "?%s", BK_URL_QUERY_DATA(bu));
    len += tmp_len;
  }

  if (BK_FLAG_ISSET(sections, BK_URL_FLAG_FRAGMENT) &&  BK_URL_FRAGMENT_DATA(bu))
  {
    tmp_len = strlen(BK_URL_FRAGMENT_DATA(bu)) + 1;
    if (!(tmp = realloc(url, len + tmp_len)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not realloc space for fragment section: %s\n", strerror(errno));
      goto error;
    }
    url = tmp;
    insert = url + strlen(url);
    snprintf(insert, tmp_len+1, "#%s", BK_URL_FRAGMENT_DATA(bu));
    len += tmp_len;
  }

  BK_RETURN(B,url);

 error:
  if (url)
    free(url);

  BK_RETURN(B,NULL);
}
