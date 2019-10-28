#if !defined(lint)
static const char libbk__copyright[] __attribute__((unused)) = "Copyright © 2002-2019";
static const char libbk__contact[] __attribute__((unused)) = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2002-2019 The Authors. All rights reserved.
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
 * String conversion functions
 */

#include <libbk.h>
#include "libbk_internal.h"



/**
 * @name MIME routines
 *
 * Convert raw memory coding to and from an efficient portable representation
 * (MIMEB64) a la RFC2045
 *
 * These routines are from the perl module MIME::Base64 (i.e. not covered
 * under LGPL) converted into C by the Authors (e.g. a derivative work).
 *
 * Copyright (c) 1991 Bell Communications Research, Inc. (Bellcore)
 *
 * Permission to use, copy, modify, and distribute this material
 * for any purpose and without fee is hereby granted, provided
 * that the above copyright notice and this permission notice
 * appear in all copies, and that the name of Bellcore not be
 * used in advertising or publicity pertaining to this
 * material without the specific, prior written permission
 * of an authorized representative of Bellcore.	BELLCORE
 * MAKES NO REPRESENTATIONS ABOUT THE ACCURACY OR SUITABILITY
 * OF THIS MATERIAL FOR ANY PURPOSE.  IT IS PROVIDED "AS IS",
 * WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES.
 */
// @{

#define MAX_LINE  76				///< size of encoded lines

/// The characters used for encoding, in order of their usage
static const char basis_64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

#define XX      255				///< illegal base64 char
#define EQ      254				///< padding
#define INVALID XX				///< illegal base64 char

/**
 * The mime decode lookup table
 */
static const unsigned char index_64[256] = {
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,62, XX,XX,XX,63,
    52,53,54,55, 56,57,58,59, 60,61,XX,XX, XX,EQ,XX,XX,
    XX, 0, 1, 2,  3, 4, 5, 6,  7, 8, 9,10, 11,12,13,14,
    15,16,17,18, 19,20,21,22, 23,24,25,XX, XX,XX,XX,XX,
    XX,26,27,28, 29,30,31,32, 33,34,35,36, 37,38,39,40,
    41,42,43,44, 45,46,47,48, 49,50,51,XX, XX,XX,XX,XX,

    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
    XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
};



/**
 * Encode a memory buffer into a base64 string.  The buffer will be broken
 * in to a series of lines 76 characters long, with the eol string used to
 * separate each line ("\n" used if the eol string is NULL).  If the eol
 * sequence is non-NULL, it will additionally be used to terminate the last
 * line. This function allocates memory which must be freed with free(3).
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/Global state
 *	@param src Source memory to convert
 *	@param eolseq End of line sequence.
 *	@return <i>NULL</i> on error
 *	@return <br><i>encoded string</i> on success which caller must free
 */
char *bk_encode_base64(bk_s B, const bk_vptr *src, const char *eolseq)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *str;					// string to encode
  ssize_t len;					// length of the string
  const char *eol;				// end-of-line sequence
  ssize_t eollen;				// length of the EOL sequence
  char *r, *ret;				// result string
  int32_t rlen;					// length of result string
  uint64_t elen;
  unsigned char c1, c2, c3;
  int chunk;

  if (!src)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_RETURN(B, NULL);
  }

  str = src->ptr;
  len = src->len;

  /* set up EOL from the second argument if present, default to "\n" */
  if (eolseq)
  {
    eol = eolseq;
  }
  else
  {
    eol = "\n";
  }
  eollen = strlen(eol);

  /*
   * <WARNING>integer overflow security, be sure rlen is large enough: due to
   * complexity and multiplication of vptr len by eolseq len, we use 64-bit
   * math to check for 32-bit overflow.</WARNING>
   */
  rlen = 4 * (1 + len / 3);			// encoded bytes
  elen = ((rlen-1) / MAX_LINE + 1) * (int64_t) eollen;
  if (rlen < 0 || rlen + elen + 1 > INT_MAX)
  {
    bk_error_printf(B, BK_ERR_ERR, "Overflow, length is %lld bytes\n",
		    BUG_LLI_CAST(rlen + elen + 1));
    BK_RETURN(B, NULL);
  }
  rlen += elen + 1;

  /* allocate a result buffer */
  if (!(ret = BK_MALLOC_LEN(r, rlen)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate memory for result");
    BK_RETURN(B, NULL);
  }

  /* encode */
  for (chunk=0; len > 0; len -= 3, chunk++)
  {
    if (chunk == (MAX_LINE/4))
    {
      const char *c = eol;
      const char *e = eol + eollen;
      while (c < e)
	*r++ = *c++;
      chunk = 0;
    }

    c1 = *str++;
    *r++ = basis_64[c1>>2];

    if (len == 1)
    {
      *r++ = basis_64[(c1 & 0x3) << 4];
      *r++ = '=';
      *r++ = '=';
    }
    else
    {
      c2 = *str++;
      *r++ = basis_64[((c1 & 0x3) << 4) | ((c2 & 0xF0) >> 4)];

      if (len == 2)
      {
	*r++ = basis_64[(c2 & 0xF) << 2];
	*r++ = '=';
      }
      else
      {
	c3 = *str++;
	*r++ = basis_64[((c2 & 0xF) << 2) | ((c3 & 0xC0) >>6)];
	*r++ = basis_64[c3 & 0x3F];
      }
    }
  }

  if (rlen)
  {						// append eol to result string
    const char *c = eol;
    const char *e = eol + eollen;
    while (c < e)
      *r++ = *c++;
  }
  *r = '\0';					// NUL terminate

  BK_RETURN(B, ret);
}



/**
 * Decode a base64 encoded buffer into memory buffer.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/Global state
 *	@param src Source memory to convert
 *	@param eolseq End of line sequence.
 *	@return <i>NULL</i> on error
 *	@return <br><i>decoded buffer</i> on success which caller must free
 *	(both bk_vptr and allocated buffer ptr)
 */
bk_vptr *bk_decode_base64(bk_s B, const char *str)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  ssize_t len;
  const char *end;
  char *r;
  ssize_t rlen;
  unsigned char c[4];
  bk_vptr *ret;

  if (!str)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!BK_MALLOC(ret))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate return structure\n");
    BK_RETURN(B, NULL);
  }

  len = strlen(str);
  /*
   * <WARNING>integer overflow security, be sure rlen is large enough: divide
   * before multiply, round up, and 1 for NUL termination.</WARNING>
   */
  rlen = 3 * (1 + len / 4) + 1;
  if (rlen < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Overflow, length is %lld bytes\n", BUG_LLI_CAST(rlen));
    free(ret);
    BK_RETURN(B, NULL);
  }

  if (!BK_MALLOC_LEN(ret->ptr, rlen))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate return structure\n");
    free(ret);
    BK_RETURN(B, NULL);
  }

  len = strlen(str);
  end = str + len;
  r = ret->ptr;
  ret->len = 0;

  while (str < end)
  {
    int i = 0;

    do
    {
      unsigned char uc = index_64[*(unsigned char *)str++];
      if (uc != INVALID)
	c[i++] = uc;

      if (str == end)
      {
	if (i < 4)
	{
	  if (i)
	    bk_error_printf(B, BK_ERR_WARN, "Premature end of base64 data\n");

	  if (i < 2)
	    goto done;				// break outer loop

	  if (i == 2)
	    c[2] = EQ;

	  c[3] = EQ;
	}
	break;
      }
    } while (i < 4);

    if (c[0] == EQ || c[1] == EQ)
    {
      bk_error_printf(B, BK_ERR_WARN, "Premature padding of base64 data\n");
      break;
    }

    bk_debug_printf_and(B, 1, "c0=%d,c1=%d,c2=%d,c3=%d\n", c[0],c[1],c[2],c[3]);

    *r++ = (c[0] << 2) | ((c[1] & 0x30) >> 4);
    ret->len++;

    if (c[2] == EQ)
      break;
    *r++ = ((c[1] & 0x0F) << 4) | ((c[2] & 0x3C) >> 2);
    ret->len++;

    if (c[3] == EQ)
      break;
    *r++ = ((c[2] & 0x03) << 6) | c[3];
    ret->len++;
  }

 done:
  BK_RETURN(B, ret);
}
// @}


#define XML_LT_STR		"&lt;"
#define XML_GT_STR		"&gt;"
#define XML_AMP_STR		"&amp;"



/**
 * Convert a string to a "valid" xml string. The function allocates memory
 * which must be freed with free(3).
 *
 * <WARNING bugid="1219">Note that 8-bit characters are passed as-is, even
 * though they are only valid if the appropriate encoding was specified in the
 * <?xml?> processing instruction.</WARNING>
 *
 * <TODO bugid="1277">There should be some way for this function to return an
 * indication that non-normalized whitespace is present in the string, so an
 * xml:space="preserve" attribute can be added if desired.</TODO>
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param str The string to convert
 *	@param flags
 *	BK_STRING_STR2XML_FLAG_ALLOW_NON_PRINT to prevent entity-encoding<br>
 *	BK_STRING_STR2XML_FLAG_ENCODE_WHITESPACE for entity-encoding of
 *	whitespace other than SP (ASCII 0x20)
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>xml string</i> on success.
 */
char *
bk_string_str2xml(bk_s B, const char *str, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *xml = NULL;
  char *p;
  enum { GROW = 8 };				// size of largest addition
  int chunk, len, left, l;
  char c;
  char *tmp;

  /*
   * In the worst case, a source string that is numerically entity-encoded
   * requires six times as much space ('\377' -> "&#xFF;") but this is very
   * rare.  Instead, we estimate the size as source strlen + chunk, where
   * chunk is computed as max(strlen/8, 128), and grow the array by chunk
   * whenever there are less than 8 bytes left (that's just slightly more
   * than we might add to the string at a time with an entity like &#xFF;).
   * See ticket #963 for the details of previous bugs.
   */

  len = strlen(str);
  chunk = MAX(len / 8, 128);
  len += chunk;
  left = len;

  // <WARNING>integer overflow security, be sure len is positive.</WARNING>
  if (len < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Overflow, length is %d bytes\n", len);
    BK_RETURN(B, NULL);
  }

  if (!BK_MALLOC_LEN(xml, len))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate xml string\n");
    goto error;
  }

  p = xml;
  while((c = *str))
  {
    /*
     * <BUG bugid="963">The use of numeric entity encoding here is only a
     * partial fix, since libxml will reject files containing not only a naked
     * ^A, but even an encoded &#x1;.  (The Java dom4j parser is more tolerant
     * of the latter).  This seems to be a problem only for chars < 0x20, and
     * note that strict XML interpretation of legal whitespace is not the same
     * as isspace(), since XML doesn't allow FF or VT.</BUG>
     */
    if (!isprint(c) &&
	(BK_FLAG_ISCLEAR(flags, BK_STRING_STR2XML_FLAG_ALLOW_NON_PRINT) &&
	 !isspace(c)) ||
	(BK_FLAG_ISSET(flags, BK_STRING_STR2XML_FLAG_ENCODE_WHITESPACE) &&
	 isspace(c)))
    {
      char scratch[GROW];
      snprintf(scratch, sizeof(scratch), "&#x%02x;", (unsigned char) c);
      l = strlen(scratch);
      memcpy(p, scratch, l);
      left -= l;
      p += l;
    }
    else
    {
      switch (c)
      {
      case '<':
	l = sizeof(XML_LT_STR) - 1;
	memcpy(p, XML_LT_STR, l);
	left -= l;
	p += l;
	break;

      case '>':
	l = sizeof(XML_GT_STR) - 1;
	memcpy(p, XML_GT_STR, l);
	left -= l;
	p += l;
	break;

      case '&':
	l = sizeof(XML_AMP_STR) - 1;
	memcpy(p, XML_AMP_STR, l);
	left -= l;
	p += l;
	break;

      default:
	*p = c;
	p++;
	left--;
	break;
      }
    }

    if (left < GROW)
    {
      int offset = p-xml;

      len += chunk;
      left += chunk;
      // <WARNING>integer overflow security, be sure len is positive.</WARNING>
      if (len < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Overflow, length is %d bytes\n", len);
	goto error;
      }
      if (!(tmp = realloc(xml, len)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not realloc xml string\n");
	goto error;
      }
      xml = tmp;
      p = xml + offset;
    }
    str++;
  }

  *p = '\0';

  BK_RETURN(B,xml);

 error:
  if (xml)
    free(xml);

  BK_RETURN(B,NULL);
}



#define XML_HYPHEN_CHARCODE "&#45;"
/**
 * Escape a string that is to be included inside an XML comment. Currently
 * this means converting double hyphens into &#45;- pairs. NB: Only the
 * first hyphen of the pair gets converted. <a>result</a> is CO char *
 * which is allocated by the function and must be free(3)'ed by the
 * caller. if *result is not NULL, it is freed and then reallocated. If the
 * function fails *result will be NULL.
 *
 *	@param B BAKA thread/global state.
 *	@param str The string to escape.
 *	@param result The escaped string. See comment above
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_string_xml_escape_comment(bk_s B, const char *str, char **result, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *estr = NULL;
  const char *p;
  char *q;
  int hyphen_cnt = 0;
  int last_char_was_hyphen;

  if (!str || !result)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (*result)
    free((char *)(*result));
  *result = NULL;

  p = str;
  while((q = strchr(p, '-')))
  {
    hyphen_cnt++;
    p = q+1;
  }

  /*
   * <STUPID>
   * Yes, I know that when hyphen_cnt is not 0, this allocates more space
   * than is necessary, but so what? It's simple.
   * </STUPID>
   */
  if (!BK_CALLOC_LEN(estr, hyphen_cnt * strlen(XML_HYPHEN_CHARCODE) + strlen(str) + 1))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not copy comment string for escaping: %s\n", strerror(errno));
    goto error;
  }

  /*
   * NB: estr was calloc(3)'ed, so this string is NUL terminated even
   * though the NUL is not copied.
   */
  last_char_was_hyphen = 0;
  q = estr;
  for(p = str; *p; p++)
  {
    if (*p == '-')
    {
      if (last_char_was_hyphen)
      {
	// q is "one behind" here (just FYI. It's not important yet).
	strcpy(q, XML_HYPHEN_CHARCODE);
	q += strlen(XML_HYPHEN_CHARCODE);
      }
      else
      {
	// NB: q is *NOT* advanced here and q falls "one behind"
	last_char_was_hyphen = 1;
      }
    }
    else
    {
      if (last_char_was_hyphen)
      {
	// q is "one behind" here, so catch up by "copying" the hyphen.
	*q++ = '-';
      }
      *q++ = *p;
      last_char_was_hyphen = 0;
    }
  }

  if (last_char_was_hyphen)
  {
    // q is "one behind" here, so catch up by "copying" the hyphen.
    *q = '-';
  }

  *result = estr;

  BK_RETURN(B, 0);

 error:
  if (estr)
    free(estr);
  BK_RETURN(B, -1);
}
