#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_string.c,v 1.41 2002/05/14 23:00:20 dupuy Exp $";
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
 * Random useful string functions
 */

#include <libbk.h>
#include "libbk_internal.h"
#include <math.h>


#define TOKENIZE_FIRST		8		///< How many we will start with 
#define TOKENIZE_INCR		4		///< How many we will expand if we need more 
#define TOKENIZE_STR_FIRST	16		///< How many we will start with 
#define TOKENIZE_STR_INCR	16		///< How many we will expand 

#define S_BASE			0x40		///< Base state 
#define S_SQUOTE		0x1		///< In single quote 
#define S_DQUOTE		0x2		///< In double quote 
#define S_VARIABLE		0x4		///< In variable 
#define S_BSLASH		0x8		///< Backslash found 
#define S_BSLASH_OCT		0x10		///< Backslash octal 
#define S_SPLIT			0x20		///< In split character 
#define INSTATE(x)		(state & (x))	///< Are we in this state 
#define GOSTATE(x)		state = x	///< Become this state  
#define ADDSTATE(x)		state |= x	///< Superstate (typically variable in dquote) 
#define SUBSTATE(x)		state &= ~(x)	///< Get rid of superstate

#define LIMITNOTREACHED	(!limit || (limit > 1 && limit--))	///< Check to see if the limit on numbers of tokens has been reached or not.  Yes, limit>1 and limit-- will always have the same truth value


static int bk_string_atoull_int(bk_s B, const char *string, u_int64_t *value, int *sign, bk_flags flags);




/**
 * Hash a string
 *	
 * The Practice of Programming: Kernighan and Pike: 2.9 (i.e. not covered
 * under LGPL)
 *
 * Note we may not be applying modulus in this function since this is
 * normally used by CLC functions, CLC will supply its own modulus and
 * it is bad voodoo to double modulus if the moduli are different.
 *
 *	@param a The string to be hashed
 *	@param flags Whether we want this value to be modulo a large prime
 *	@return <i>hash</i> of number
 */
u_int 
bk_strhash(const char *a, bk_flags flags)
{
  const u_int M = 37U;				// Multiplier
  const u_int P = 2147486459U;			// Arbitrary large prime
  u_int h;

  for (h = 17; *a; a++)
    h = h * M + *a;

  if (BK_FLAG_ISSET(flags, BK_STRHASH_NOMODULUS))
    return(h);
  else
    return(h % P);
}



/**
 * Print a binary buffer into human readable form.  Limit 65K bytes.
 * Allocates and returns a character buffer of sufficient size.
 * Caller is required to free this buffer.
 *
 * size = (n/16) * (prefix+61) + strlen(intro) + 10
 *
 * Example output line:
 * PREFIXAddress  Hex dump of data                     ASCII interpretation
 * PREFIXAddr 1234 5678 1234 5678 1234 5678 1234 5678  qwertu...&*ptueo
 *
 *	@param B BAKA Thread/global state
 *	@param intro Description of buffer being printed
 *	@param prefix Data to be prepended to each line of buffer data
 *	@param buf The vectored raw data to be converted
 *	@param flags Fun for the future
 *	@return <i>NULL</i> on call failure, allocation failure, size failure, etc
 *	@return <br><i>string</i> representing the buffer on success
 */
char *bk_string_printbuf(bk_s B, const char *intro, const char *prefix, const bk_vptr *buf, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  const u_int bytesperline = 16;		// Must be multiple of bytes per group--the maximum number of source bytes displayed on one output line
  const u_int hexaddresscols = 4;		// The number of columns outputting the address takes
  const u_int addressbitspercol = 16;		// The base of the address
  const u_int bytespergroup = 2;		// The number of source bytes outputted per space separated group
  const u_int colsperbyte = 2;			// The number of columns of output it takes to output one bytes of source data
  const u_int colsperline = hexaddresscols + 2 + (bytesperline / bytespergroup) * (colsperbyte * bytespergroup + 1) + 1 + bytesperline + 1; // Number of output columns for a single line
  u_int64_t maxaddress;
  u_int32_t nextaddr, addr, addrgrp, nextgrp, addrbyte, len, curlen;
  char *ret;
  char *cur;

  if (!buf)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,NULL);
  }

  // addressbitspercol ^ hexaddresscols 
  for(len=0,maxaddress=1;len<hexaddresscols;len++) maxaddress *= addressbitspercol;

  if (buf->len >= maxaddress)
  {
    bk_error_printf(B, BK_ERR_ERR, "Buffer size too large to print\n");
    BK_RETURN(B,NULL);
  }

  if (!intro) intro="";
  if (!prefix) prefix="";

  curlen = len = ((buf->len+bytesperline-1) / bytesperline) * (strlen(prefix)+colsperline) + strlen(intro) + 2;
  if (!(ret = malloc(len)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate buffer string of size %d: %s\n",len,strerror(errno));
    BK_RETURN(B,NULL);
  }
  cur = ret;
  cur[0] = 0;

  /* General introduction */
  strcpy(cur,intro); curlen -= strlen(cur); cur += strlen(cur);
  strcpy(cur,"\n"); curlen -= strlen(cur); cur += strlen(cur);

  /* For all lines of output */
  for(addr=0;addr<buf->len;addr=nextaddr)
  {
    nextaddr = addr + bytesperline;
    strcpy(cur,prefix); curlen -= strlen(cur); cur += strlen(cur);
    snprintf(cur,curlen,"%04x  ",addr);  curlen -= strlen(cur); cur += strlen(cur);

    /* For each group of characters in line */
    for(addrgrp = addr; addrgrp < nextaddr; addrgrp = nextgrp)
    {
      nextgrp = addrgrp + bytespergroup;

      /* For each byte in group */
      for(addrbyte = addrgrp; addrbyte < nextgrp; addrbyte++)
      {
	if (addrbyte >= buf->len)
	  snprintf(cur,curlen,"  ");
	else
	  snprintf(cur,curlen,"%02x",((u_char *)buf->ptr)[addrbyte]);
	curlen -= strlen(cur); cur += strlen(cur);
      }
      strcpy(cur," "); curlen -= strlen(cur); cur += strlen(cur);
    }
    strcpy(cur," "); curlen -= strlen(cur); cur += strlen(cur);

    /* For each byte in line */
    for(addrbyte = addr; addrbyte < nextaddr; addrbyte++)
    {
      char c;

      if (addrbyte >= buf->len)
	c = ' ';
      else
	c = ((char *)buf->ptr)[addrbyte];

      snprintf(cur,curlen,"%c",isprint(c)?c:'.');
      curlen -= strlen(cur); cur += strlen(cur);
    }
    strcpy(cur,"\n"); curlen -= strlen(cur); cur += strlen(cur);
  }
  *cur = 0;
  ret[len-1] = 0;

  BK_RETURN(B, ret);
}



/**
 * Convert ascii string to unsigned int32
 *
 *	@param B BAKA Thread/global state
 *	@param string String to convert
 *	@param value Copy-out of number the string represents
 *	@param flags Fun for the future.
 *    	@see bk_string_atoull_int
 *	@return <i>-1</i> on error (string not converted)
 *	@return <br><i>0</i> on success
 *	@return <br><i>positive</i> on non-null terminated number--best effort
 *	number conversion still performed
 */
int bk_string_atou(bk_s B, const char *string, u_int32_t *value, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int sign = 0;
  u_int64_t tmp;
  int ret = bk_string_atoull_int(B, string, &tmp, &sign, flags);

  /* We could trivially check for 32 bit overflow, but what is the proper response? */
  *value = (u_int32_t)tmp;

  /* We are pretending number terminated at the minus sign */
  if (sign < 0)
  {
    *value = 0;
    if (ret == 0) ret=1;
  }

  BK_RETURN(B, ret);
}



/**
 * Convert ascii string to signed int
 *
 *	@param B BAKA Thread/global state
 *	@param string String to convert
 *	@param value Copy-out of number the string represents
 *	@param flags Fun for the future.
 *    	@see bk_string_atoull_int
 *	@return <i>-1</i> on error (string not converted)
 *	@return <br><i>0</i> on success
 *	@return <br><i>positive</i> on non-null terminated number--best effort
 *	number conversion still performed
 */
int bk_string_atoi(bk_s B, const char *string, int32_t *value, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int sign = 0;
  u_int64_t tmp;
  int ret = bk_string_atoull_int(B, string, &tmp, &sign, flags);


  /* We could trivially check for 32 bit overflow, but what is the proper response? */
  *value = tmp;

  /* Not enough bits -- I guess this is still a pos */
  if (*value < 0)
    *value = 0;

  *value *= sign;

  BK_RETURN(B, ret);
}



/**
 * Convert ascii string to unsigned int64
 *
 *	@param B BAKA Thread/global state
 *	@param string String to convert
 *	@param value Copy-out of number the string represents
 *	@param flags Fun for the future.
 *    	@see bk_string_atoull_int
 *	@return <i>-1</i> on error (string not converted)
 *	@return <br><i>0</i> on success
 *	@return <br><i>positive</i> on non-null terminated number--best effort
 *	number conversion still performed
 */
int bk_string_atoull(bk_s B, const char *string, u_int64_t *value, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int sign = 0;
  u_int64_t tmp;
  int ret = bk_string_atoull_int(B, string, &tmp, &sign, flags);

  *value = tmp;

  /* We are pretending number terminated at the minus sign */
  if (sign < 0)
  {
    *value = 0;
    if (ret == 0) ret=1;
  }

  BK_RETURN(B, ret);
}



/**
 * Convert ascii string to signed int64
 *
 *	@param B BAKA Thread/global state
 *	@param string String to convert
 *	@param value Copy-out of number the string represents
 *	@param flags Fun for the future.
 *    	@see bk_string_atoull_int
 *	@return <i>-1</i> on error (string not converted)
 *	@return <br><i>0</i> on success
 *	@return <br><i>positive</i> on non-null terminated number--best effort
 *	number conversion still performed
 */
int bk_string_atoill(bk_s B, const char *string, int64_t *value, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int sign = 0;
  u_int64_t tmp;
  int ret = bk_string_atoull_int(B, string, &tmp, &sign, flags);

  *value = tmp;

  /* Not enough bits -- I guess this is still a pos */
  if (*value < 0)
    *value = 0;

  *value *= sign;

  BK_RETURN(B, ret);
}



/**
 * Convert ascii string to unsigned int64 with sign extension.  Uses
 * the normal convention of leading "0x" to detect hexadecimal numbers
 * and "0" to detect octal numbers.
 *
 *	@param B BAKA Thread/global state
 *	@param string String to convert
 *	@param value Copy-out of number the string represents
 *	@param sign Copy-out sign of number converted
 *	@param flags Fun for the future.
 *    	@see bk_string_atoull_int
 *	@return <i>-1</i> on call failure
 *	@return <br><i>0</i> on success
 *	@return <br><i>positive</i> on non-null terminated number--best effort
 *	number conversion still performed
 */
static int bk_string_atoull_int(bk_s B, const char *string, u_int64_t *value, int *sign, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  signed char decode[256];
  int base;
  u_int tmp;
  int neg = 1;

  if (!string || !value || !sign)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, -1);
  }

  memset(decode, -1, sizeof(decode));
  for(tmp='0';tmp<='9';tmp++)
    decode[tmp] = tmp - '0';
  for(tmp='A';tmp<='Z';tmp++)
    decode[tmp] = tmp - 'A' + 10;
  for(tmp='a';tmp<='z';tmp++)
    decode[tmp] = tmp - 'a' + 10;

  /* Skip over leading space */
  while (isspace(*string))
    string++;

  if (!*string)
  {
    *sign = 1;
    *value = 0;

    BK_RETURN(B,1);
  }

  /* Sign determination */
  switch(*string)
  {
  case '-':
    neg = -1; string++; break;
  case '+':
    neg = 1; string++; break;
  }

  /* Base determination */
  if (*string == '0')
  {
    switch(*(string+1))
    {
    case 'x':
    case 'X':
      base = 16; string += 2; break;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
      base = 8; string += 1; break;
    default:
      base = 10; break;
    }
  }
  else
    base = 10;

  *value = 0;
  while (*string)
  {
    int x = decode[(int)*string++];

    /* Is this the end of the number? */
    if (x < 0 || x >= base)
      break;

    *value *= base;
    *value += x;
  }

  *sign = neg;
  BK_RETURN(B, *((u_char *)string));
}



/**
 * Tokenize a string into an array of the tokens in the string
 *
 * Yeah, this function uses gotos a bit more than I really like, but
 * avoiding the code duplication is pretty important too.
 *
 *	@param B BAKA Thread/global state
 *	@param src Source string to tokenize
 *	@param limit Maximum number of tokens to generate--last token contains "rest" of string.  Zero for unlimited.
 *	@param spliton The string containing the character(s) which separate tokens
 *	@param variabledb Future expansion--variable substitution.  Set to NULL for now.
 *	@param flags BK_STRING_TOKENIZE_SKIPLEADING, if set will cause
 *		leading separator characters to be ignored; otherwise,
 *		an initial zero-length token will be generated.
 *		BK_STRING_TOKENIZE_MULTISPLIT, if set, will cause
 *		multiple separators in a row to be treated as the same
 *		separator; otherwise, zero-length tokens will be
 *		generated for every subsequent separator character.
 *		BK_STRING_TOKENIZE_BACKSLASH_INTERPOLATE_CHAR, if set,
 *		will cause ANSI-C backslash sequences to be
 *		interpolated (\n); otherwise, ANSI-C backslash
 *		sequences are not treated differently.
 *		BK_STRING_TOKENIZE_BACKSLASH_INTERPOLATE_OCT, if set,
 *		will cause backslash followed by zero and an octal
 *		number (of variable length up to length 3) to be
 *		interpreted as, and converted to, an ASCII character;
 *		otherwise, that sequence is not treated
 *		specially. BK_STRING_TOKENIZE_BACKSLASH, if set, will
 *		cause a backslash to be a general quoting character
 *		which will quote the next character (aside from NUL)
 *		appearing in the source string (BACKSLASH_INTERPOLATE
 *		will override this); otherwise BACKSLASH is not
 *		treated specially (modulo BACKSLASH_INTERPOLATE).
 *		BK_STRING_TOKENIZE_SINGLEQUOTE, if set, will cause
 *		single quotes to create a string in which the
 *		separator character, backslashes, and other magic
 *		characters may exist without interpolation; otherwise
 *		single quote is not treated specially.
 *		BK_STRING_TOKENIZE_DOUBLEQUOTE, if set, will cause
 *		double quotes to create a string in which the
 *		separator character may exist without causing
 *		tokenization separation (backslashes are still magic
 *		and may quote a double quote); otherwise, double
 *		quotes are not treated specially.  At some point in
 *		the future, BK_STRING_TOKENIZE_VARIABLE may exist
 *		which may cause $variable text substitution. There are
 *		some convenience flags which group some of these
 *		together. See @a libbk.h for details. You should call
 *		@a bk_string_tokenize_destroy to free up the generated
 *		array.
 *	@return <i>NULL</i> on call failure, allocation failure, other failure
 *	@return <br><i>null terminated array of token strings</i> on success.
 */
char **bk_string_tokenize_split(bk_s B, const char *src, u_int limit, const char *spliton, const void *variabledb, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char **ret;
  const char *curloc = src;
  int tmp;
  u_int toklen, state = S_BASE;
  const char *startseq = NULL;
  char *token;
  u_char newchar;
  struct bk_memx *tokenx, *splitx = NULL;

  if (!src)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  bk_debug_printf_and(B, 1, "Tokenizing ``%s'' with limit %d and flags %x\n",src,limit,flags);

  if (!(tokenx = bk_memx_create(B, sizeof(char), TOKENIZE_STR_FIRST, TOKENIZE_STR_INCR, 0)) ||
      !(splitx = bk_memx_create(B, sizeof(char *), TOKENIZE_FIRST, TOKENIZE_INCR, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate split extended arrays for split\n");
    goto error;
  }

  if (!spliton)
    spliton = BK_WHITESPACE;

  if (BK_FLAG_ISSET(flags, BK_STRING_TOKENIZE_SKIPLEADING))
  {
    if ((tmp = strspn(curloc, spliton)) > 0)
      curloc += tmp;
  }

  /* Go over all characters in source string */
  for(; ; curloc++)
  {
    if (INSTATE(S_SPLIT))
    {
      /* Are we still looking at a separator? */
      if (strchr(spliton, *curloc) && LIMITNOTREACHED)
      {
	/* Is that separator a NULL? */
	if (!*curloc)
	  break;
	/* Are multiple separators the same? */
	if (BK_FLAG_ISSET(flags, BK_STRING_TOKENIZE_MULTISPLIT))
	  continue;

	/* Multiple separators are additional tokens */
	goto tokenizeme;
      }
      else
      {
	GOSTATE(S_BASE);
	/* Fall through for additional processing of this character */
      }
    }

    /* We have seen a '\' and '0' */
    if (INSTATE(S_BSLASH_OCT))
    {
      if ((((*(startseq+1) == '0') ||
	    (*(startseq+1) == '1') ||
	    (*(startseq+1) == '2') ||
	    (*(startseq+1) == '3')) ?
	   (curloc - startseq > 3) :
	   (curloc - startseq > 2)) ||
	  ((*curloc != '0') &&
	   (*curloc != '1') &&
	   (*curloc != '2') &&
	   (*curloc != '3') &&
	   (*curloc != '4') &&
	   (*curloc != '5') &&
	   (*curloc != '6') &&
	   (*curloc != '7')))
      {
	/*
	 * We have found the end-of-octal escape sequence.
	 *
	 * Compute the byte value from the octal characters
	 */
	newchar = 0;
	for (;startseq < curloc;startseq++)
	{
	  newchar *= 8;
	  newchar += *startseq - '0';
	}

	SUBSTATE(S_BSLASH_OCT);			/* Revert to previous state */
	if (!(token = bk_memx_get(B, tokenx, 1, NULL, BK_MEMX_GETNEW)))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not extend array for additional character\n");
	  goto error;
	}
	*token = newchar;
	/* Fall through for subsequent processing of *curloc */
      }
      else
      {
	/* This is an octal character and we have not yet reached the limit */
	continue;
      }
    }

    /* We have seen a backslash */
    if (INSTATE(S_BSLASH))
    {
      newchar = 0;
      startseq = curloc;

      /* ANSI-C backslash sequences? */
      if (BK_FLAG_ISSET(flags, BK_STRING_TOKENIZE_BACKSLASH_INTERPOLATE_CHAR))
      {
	/* Note: \0 is intentionally not supported */
	switch (*curloc)
	{
	case 'n': newchar = '\n'; break;
	case 'r': newchar = '\r'; break;
	case 't': newchar = '\t'; break;
	case 'v': newchar = '\b'; break;
	case 'f': newchar = '\f'; break;
	case 'b': newchar = '\b'; break;
	case 'a': newchar = '\a'; break;
	}

	if (newchar)
	{					/* Found a ANSI-C backslash sequence */
	  SUBSTATE(S_BSLASH);			/* Revert to previous state */

	  if (!(token = bk_memx_get(B, tokenx, 1, NULL, BK_MEMX_GETNEW)))
	  {
	    bk_error_printf(B, BK_ERR_ERR, "Could not extend array for additional character\n");
	    goto error;
	  }
	  *token = newchar;
	  continue;
	}
      }

      /* Octal interpretation\077 */
      if (BK_FLAG_ISSET(flags, BK_STRING_TOKENIZE_BACKSLASH_INTERPOLATE_OCT))
      {
	if (*curloc == '0')
	{
	  SUBSTATE(S_BSLASH);
	  ADDSTATE(S_BSLASH_OCT);
	  continue;
	}
      }

      /* Generic\ quoting */
      if (BK_FLAG_ISSET(flags, BK_STRING_TOKENIZE_BACKSLASH))
      {
	/*
	 * Now, there is a very interesting about what to do with
	 * a zero (end-of-string) here.  Does the backslash win
	 * or does the end-of-string win?  I vote for end-of-string
	 * to support normal C string conventions.
	 */
	if (!*curloc)
	  goto tokenizeme;

	/* Anything else just gets added to the current token */
	SUBSTATE(S_BSLASH);
	goto addnormal;
      }

      /*
       * Backtrack state--we have seen a backslash, but we are not in
       * generic backslash mode.  We must revert back to our normal
       * mode, put the previous backslash into the token, and continue
       * with whatever processing the previous mode would do on the
       * current character.
       */
      if (!(token = bk_memx_get(B, tokenx, 1, NULL, BK_MEMX_GETNEW)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not extend array for additional character\n");
	goto error;
      }
      *token = 0134;
      SUBSTATE(S_BSLASH);
      /* Fall through for subsequent processing */
    }

    /* Non-special state */
    if (INSTATE(S_BASE))
    {
      /* Look for token ending characters */
      if (!*curloc || (strchr(spliton, *curloc) && LIMITNOTREACHED))
      {
      tokenizeme:
	/* Null terminate the new, complete, token */
	if (!(token = bk_memx_get(B, tokenx, 1, NULL, BK_MEMX_GETNEW)))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not extend array for additional character\n");
	  goto error;
	}
	*token = '\0';

	/* Get the full token */
	if (!(token = bk_memx_get(B, tokenx, 0, &toklen, 0)))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not obtain token (huh?)\n");
	  goto error;
	}

	/* Get the token slot */
	if (!(ret = bk_memx_get(B, splitx, 1, NULL, BK_MEMX_GETNEW)))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not extend array for additional token slot\n");
	  goto error;
	}

	/* Duplicate the token into the token slot */
	if (!(*ret = strdup(token)))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not duplicate token: %s\n", strerror(errno));
	  goto error;
	}

	/* Zap the token for a fresh start */
	if (bk_memx_trunc(B, tokenx, 0, 0) < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not zap tokenx (huh?)\n");
	  goto error;
	}

	/* Check for end-of-string */
	if (!*curloc)
	  break;

	GOSTATE(S_SPLIT);
	continue;
      }

      /* Single quote? */
      if (BK_FLAG_ISSET(flags, BK_STRING_TOKENIZE_SINGLEQUOTE) && *curloc == 047)
      {
	GOSTATE(S_SQUOTE);
	continue;
      }

      /* Double quote? */
      if (BK_FLAG_ISSET(flags, BK_STRING_TOKENIZE_DOUBLEQUOTE) && *curloc == 042)
      {
	GOSTATE(S_DQUOTE);
	continue;
      }

      /* Backslash? */
      if (*curloc == 0134 && (BK_FLAG_ISSET(flags, BK_STRING_TOKENIZE_BACKSLASH) ||
			      BK_FLAG_ISSET(flags, BK_STRING_TOKENIZE_BACKSLASH_INTERPOLATE_CHAR) ||
			      BK_FLAG_ISSET(flags, BK_STRING_TOKENIZE_BACKSLASH_INTERPOLATE_OCT)))
      {
	ADDSTATE(S_BSLASH);
	continue;
      }

      /* Must be a normal character--add to working token */
    addnormal:
      if (!(token = bk_memx_get(B, tokenx, 1, NULL, BK_MEMX_GETNEW)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not extend array for additional character\n");
	goto error;
      }
      *token = *curloc;
      continue;
    }

    /* We are in a single quoted token.  Note foo' 'bar is one token. */
    if (INSTATE(S_SQUOTE))
    {
      if (*curloc == 047)
      {
	GOSTATE(S_BASE);
	continue;
      }
      if (*curloc == 0)
      {
	bk_error_printf(B, BK_ERR_NOTICE, "Unexpected end-of-string inside a single quote\n");
	goto tokenizeme;
      }
      goto addnormal;
    }

    /* We are in a double quoted token.  Note foo" "bar is one token. */
    if (INSTATE(S_DQUOTE))
    {
      if (*curloc == 042)
      {
	GOSTATE(S_BASE);
	continue;
      }
      if (*curloc == 0)
      {
	bk_error_printf(B, BK_ERR_NOTICE, "Unexpected end-of-string inside a double quote\n");
	goto tokenizeme;
      }

      /* Backslash? */
      if (*curloc == 0134 && (BK_FLAG_ISSET(flags, BK_STRING_TOKENIZE_BACKSLASH) ||
			      BK_FLAG_ISSET(flags, BK_STRING_TOKENIZE_BACKSLASH_INTERPOLATE_CHAR) ||
			      BK_FLAG_ISSET(flags, BK_STRING_TOKENIZE_BACKSLASH_INTERPOLATE_OCT)))
      {
	ADDSTATE(S_BSLASH);
	continue;
      }

      goto addnormal;
    }

    bk_error_printf(B, BK_ERR_ERR, "Should never reach here (%d)\n", *curloc);
    goto error;
  }

  /* Get the token slot */
  if (!(ret = bk_memx_get(B, splitx, 1, NULL, BK_MEMX_GETNEW)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not extend array for final token slot\n");
    goto error;
  }
  *ret = NULL;					/* NULL terminated array */

  /* Get the first token slot */
  if (!(ret = bk_memx_get(B, splitx, 0, NULL, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not find array for return?!?\n");
    goto error;
  }
  bk_memx_destroy(B, splitx, BK_MEMX_PRESERVE_ARRAY);
  bk_memx_destroy(B, tokenx, 0);
  BK_RETURN(B, ret);
  
 error:
  /*
   * Note - contents of splitx (duplicated tokens) may be leaked
   * no matter what we do, due to realloc.  Thus we do not get
   * juiced to free them on other (typically memory) errors.
   */
  if (tokenx)
    bk_memx_destroy(B, tokenx, 0);
  if (splitx)
    bk_memx_destroy(B, splitx, 0);
  BK_RETURN(B, NULL);
}



/**
 * Destroy the array of strings produced by bk_string_tokenize_split
 *
 *	@param B BAKA Thread/global state
 *	@param tokenized The array of tokens produced by @a bk_string_tokenize_split
 *	@see bk_string_tokenize_split
 */
void bk_string_tokenize_destroy(bk_s B, char **tokenized)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char **cur;

  if (!tokenized)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  for(cur=tokenized; *cur; cur++)
    free(*cur);
  free(tokenized);

  BK_VRETURN(B);
}



/**
 * Rip a string -- terminate it at the first occurrence of the terminator characters.,
 * Typically used with vertical whitespace to nuke the \r\n stuff.
 *
 *	@param B BAKA Thread/global state
 *	@param string String to examine and change
 *	@param terminators String containing characters to terminate source at, on first occurrence--if NULL use vertical whitespace.
 *	@param flags Fun for the future
 *	@return <i>NULL</i> on failure
 *	@return <br><i>modified string</i> on success
 */
char *bk_string_rip(bk_s B, char *string, const char *terminators, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!string)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!terminators) terminators = BK_VWHITESPACE;

  /* Write zero at either first terminator character, or at the trailing \0 */
  *(string + strcspn(string,terminators)) = 0;
  BK_RETURN(B, string);
}



/**
 * Quote a string, with double quotes, for printing, and subsequent split'ing, and
 * returning a newly allocated string which should be free'd by the caller.
 *
 * split should get the following flags to decode
 * BK_STRING_TOKENIZE_DOUBLEQUOTE|BK_STRING_TOKENIZE_BACKSLASH_INTERPOLATE_OCT
 *
 * 	@param B BAKA Thread/global state
 *	@param src Source string to convert
 *	@param needquote Characters which need backslash quoting (double quote if NULL)
 *	@param flags BK_STRING_QUOTE_NULLOK will convert NULL @a src into BK_NULLSTR.  BK_STRING_QUOTE_NONPRINT will quote non-printable characters.
 *	@return <i>NULL</i> on call failure, allocation failure, other failure
 *	@return <br><i>quoted src</i> on success (you must free)
 */
char *bk_string_quote(bk_s B, const char *src, const char *needquote, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *ret = NULL;
  struct bk_memx *outputx;
  char scratch[16];

  if (!src)
  {
    if (BK_FLAG_ISSET(flags, BK_STRING_QUOTE_NULLOK))
    {
      if (!(ret = strdup(BK_NULLSTR)))
	bk_error_printf(B, BK_ERR_ERR, "Could not allocate space for NULL\n");
    }
    else
      bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");

    BK_RETURN(B, ret);		/* "NULL" or NULL */
  }

  if (!needquote) needquote = "\"";

  if (!(outputx = bk_memx_create(B, sizeof(char), strlen(src)+5, TOKENIZE_STR_INCR, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create expandable output space for quotation\n");
    BK_RETURN(B, NULL);
  }

  for(;*src;src++)
  {
    if (*src == '\\' || strchr(needquote, *src) || BK_FLAG_ISSET(flags, BK_STRING_QUOTE_NONPRINT)?!isprint(*src):0)
    {						/* Must convert to octal  */
      snprintf(scratch,sizeof(scratch),"\\0%o",(u_char)*src);
      if (!(ret = bk_memx_get(B, outputx, strlen(scratch), NULL, BK_MEMX_GETNEW)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not extend array for additional character\n");
	goto error;
      }
      memcpy(ret,scratch,strlen(scratch));
    }
    else
    {						/* Normal, just stick in */
      if (!(ret = bk_memx_get(B, outputx, 1, NULL, BK_MEMX_GETNEW)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not extend array for additional character\n");
	goto error;
      }
      *ret = *src;
    }
  }

  if (!(ret = bk_memx_get(B, outputx, 1, NULL, BK_MEMX_GETNEW)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not extend array for additional character\n");
    goto error;
  }
  *ret = 0;

  if (!(ret = bk_memx_get(B, outputx, 0, NULL, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not obtain output string\n");
    goto error;
  }
  bk_memx_destroy(B, outputx, BK_MEMX_PRESERVE_ARRAY);

  BK_RETURN(B, ret);

 error:
  if (outputx)
    bk_memx_destroy(B, outputx, 0);

  BK_RETURN(B, NULL);
}



/**
 * Convert flags (number/bitfield) to a string (ascii hex encoding)
 *  Reverse of @a bk_string_atoflag
 *
 *	@param B BAKA Thread/global state
 *	@param src Source number to convert
 *	@param flags Fun for the future
 *	@return <i>NULL</i> on allocation failure
 *	@return <br><i>string</i> on success (you must free)
 */
char *bk_string_flagtoa(bk_s B, const bk_flags src, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *ret;
  char scratch[16];

  snprintf(scratch, sizeof(scratch), "0x%x",src);

  if (!(ret = strdup(scratch)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create flags string: %s\n",strerror(errno));
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B, ret);
}



/**
 * Convert a string to flags.  Reverse of @a bk_string_flagtoa.
 *
 *	@param B BAKA Thread/global state
 *	@param src Source ascii string to convert
 *	@param dst Copy-out flags
 *	@param flags Fun for the future
 *	@return <i>-1</i> Call failure, not a valid string
 *	@return <br><i>0</i> on success
 *	@return <br>Copy-out <i>dst</i>
 */
int bk_string_atoflag(bk_s B, const char *src, bk_flags *dst, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!dst || !src)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, -1);
  }

  if (bk_string_atou(B, src, dst, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not convert flags string\n");
    BK_RETURN(B, -1);
  }

  BK_RETURN(B, 0);
}



/**
 * Bounded strlen. Return the length the of a string but go no further than
 * the bound. NB: This function return a @a ssize_t not a @a size_t as per
 * @a strlen(3). This is so we can return <i>-1</i>.`
 *	@param B BAKA Thread/global state.
 *	@param s The string to check.
 *	@param max The maximum string size to check.
 *	@returns The <i>length</i> of the string on success. 
 *	@returns <i>-1</i> on failure (including string too long).
 * 	@bugs Does not return the same type as @a strlen(3). See description.
 */
ssize_t /* this is not an error. See description */
bk_strnlen(bk_s B, const char *s, size_t max)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  size_t c=0;

  if (!s)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  for(c=0; *s && c < max; c++)
    s++; // Void

  if (c == max && *s)
    BK_RETURN(B, -1);

  BK_RETURN(B, c);
}



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
static char basis_64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

#define XX      255				///< illegal base64 char
#define EQ      254				///< padding
#define INVALID XX				///< illegal base64 char

/**
 * The mime decode lookup table
 */
static unsigned char index_64[256] = {
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
 *	@param B BAKA Thread/Global state
 *	@param src Source memory to convert
 *	@param eolseq End of line sequence.
 *	@return <i>NULL</i> on error
 *	@return <br><i>encoded string</i> on success which caller must free
 */
char *bk_encode_base64(bk_s B, const bk_vptr *src, const char *eolseq)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *str;					/* string to encode */
  ssize_t len;					/* length of the string */
  const char *eol;				/* the end-of-line sequence to use */
  ssize_t eollen;				/* length of the EOL sequence */
  char *r, *ret;				/* result string */
  ssize_t rlen;					/* length of result string */
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

  /* calculate the length of the result */
  rlen = (len+2) / 3 * 4;			/* encoded bytes */
  if (rlen)
  {
    /* add space for EOL */
    rlen += ((rlen-1) / MAX_LINE + 1) * eollen;
  }
  rlen++;					// Add space for null termination

  /* allocate a result buffer */
  if (!(ret = BK_MALLOC_LEN(r, rlen)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate memory(%d) for result: %s\n", (int) rlen, strerror(errno));
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
    c2 = *str++;
    *r++ = basis_64[c1>>2];
    *r++ = basis_64[((c1 & 0x3)<< 4) | ((c2 & 0xF0) >> 4)];

    if (len > 2)
    {
      c3 = *str++;
      *r++ = basis_64[((c2 & 0xF) << 2) | ((c3 & 0xC0) >>6)];
      *r++ = basis_64[c3 & 0x3F];
    }
    else if (len == 2)
    {
      *r++ = basis_64[(c2 & 0xF) << 2];
      *r++ = '=';
    }
    else /* len == 1 */
    {
      *r++ = '=';
      *r++ = '=';
    }
  }

  if (rlen)
  {						/* append eol to the result string */
    const char *c = eol;
    const char *e = eol + eollen;
    while (c < e)
      *r++ = *c++;
  }
  *r = '\0';					/* NULL terminate */

  BK_RETURN(B, ret);
}



/**
 * Decode a base64 encoded buffer into memory buffer.
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
  rlen = len * 3 / 4;				// Might be too much, but always enough
  rlen++;					// Add space for null termination
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
      unsigned char uc = index_64[(int)*str++];
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




// <TODO>This can be removed ifdef HAVE_STRNDUP once B is gone</TODO>
/**
 * @a strdup at <em>most</em> @a n bytes of data. Returns NULL terminated.
 *
 *	@param B BAKA thread/global state.
 *	@param s Source string.
 *	@param len maximum len;
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>duplicated</i> string on success.
 */
char *
bk_strndup(bk_s B, const char *s, size_t len)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
#ifndef HAVE_STRNDUP
  char *new;

  if (!s)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }
  
  if (!(BK_MALLOC_LEN(new, len+1)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not malloc: %s\n", strerror(errno));
    BK_RETURN(B,NULL);
  }
  
  snprintf(new,len+1, "%s", s);
  BK_RETURN(B,new);
#else  /* !HAVE_STRNDUP */
  BK_RETURN(B, strndup(s, len));
#endif /* !HAVE_STRNDUP */
}



// <TODO>This can be removed ifdef HAVE_MEMMEM once B is gone</TODO>
/**
 * Search for a fixed string within a buffer without exceeding a specified
 * max.  This is like @a strstr(3) but doesn't assume that the supplied
 * haystack is null terminated.  The needle is assumed to be null
 * terminated.
 *
 *	@param B BAKA thread/global state.
 *	@param haystack The buffer in which to search.
 *	@param needle The fixed string to search for. 
 *	@param len The max length to search.
 *	@return <i>NULL</i> on failure.<br>
 *	@return pointer to @a needle in @a haystack on success.
 */
char *
bk_strnstr(bk_s B, const char *haystack, const char *needle, size_t len)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
#ifndef HAVE_MEMMEM
  const char *p;
  const char *q;
  const char *upper_bound;
  size_t nlen;

  if (!haystack || !needle)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  p = haystack;

  nlen = strlen(needle);
  if (len >= nlen)
  {
    upper_bound = haystack + len - nlen;
  
    while(p < upper_bound)
    {
      if (!(q = memchr(p, *needle, upper_bound - p)))
	break;					// Not found

      if (BK_STREQN(q, needle, nlen))
	BK_RETURN(B, (char *)q);      
      
      p = q + 1;
    }
  }

  BK_RETURN(B,NULL);  
#else  /* !HAVE_MEMMEM */
  BK_RETURN(B, memmem(haystack, len, needle, strlen(needle)));
#endif /* !HAVE_MEMMEM */
}



// <TODO>This can be removed ifdef HAVE_MEMRCHR once B is gone</TODO>
/**
 * Search len bytes of buffer for the final occurrence of character.
 *
 *	@param B BAKA thread/global state.
 *	@param buffer The buffer in which to search.
 *	@param character The character to search for. 
 *	@param len The max length to search.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>pos</i> of @a needle on success.
 */
void *
bk_memrchr(bk_s B, const void *buffer, int character, size_t len)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
#ifndef HAVE_MEMRCHR
  const char *p;

  p = buffer;
  p += len;
  while (--p >= (const char *)buffer)
    if (*p == character)
      BK_RETURN(B, (void *)p);  

  BK_RETURN(B, NULL);  
#else  /* !HAVE_MEMRCHR */
  BK_RETURN(B, memrchr(buffer, character, len));
#endif /* !HAVE_MEMRCHR */
}



/**
 * Compare two possibly NUL-terminated strings, ignoring whitespace
 * differences.  This is like @a strncasecmp(3) but does not do case folding;
 * instead it treats any sequences of whitespace characters as identical.  The
 * comparison length of both strings must be provided; strings are considered
 * identical only if all characters in the both strings are matched.
 *
 * Since multiple whitespace characters are equivalent to a single whitespace
 * character, a single length cannot be used for both strings since
 * strnspacecmp("a b", "a \t b", 3) would return non-zero even though the
 * strings are effectively identical.  (Alternately, it would allow reads past
 * the end of the strings).
 *
 *	@param B BAKA thread/global state.
 *	@param s1 The first string
 *	@param s2 The second string
 *	@param len1 Max length of s1
 *	@param len2 Max length of s2
 *	@return <i>negative</i> if s1 compares less than s2<br>
 *	@return <i>0</i> if s1 and s2 are equivalent ignoring whitespace<br>
 *	@return <i>positive</i> if s1 compares greater than s2<br>
 */
int
bk_strnspacecmp(bk_s B, const char *s1, const char *s2, u_int len1, u_int len2)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  u_int i1;
  u_int i2;
  int res;

  for (i1 = i2 = 0; i1 < len1 && i2 < len2; )
  {
    if (isspace(s1[i1]) && isspace(s2[i2]))
    {
      while (++i2 < len2 && isspace(s2[i2]))
	;
      while (++i1 < len1 && isspace(s1[i1]))
	;
    }
    else if ((res = s1[i1] - s2[i2]) || s1[i1] == '\0')
      BK_RETURN(B, res);
    else
    {
      ++i1;
      ++i2;
    }
  }

  // equality requires matching all characters from the first string
  BK_RETURN(B, s1[i1] ? len1 - i1 : 0);
}



#define CHUNK_LEN(cur_len, chunk_len) ((((cur_len) / (chunk_len)) + 1) * (chunk_len))
#define XML_LT_STR		"&lt;"
#define XML_GT_STR		"&gt;"
#define XML_AMP_STR		"&amp;"

/**
 * Convert a string to a valid xml string. The function allocates memory
 * which must be freed with free(3).
 *
 *	@param B BAKA thread/global state.
 *	@param str The string to convert
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>xml string</i> on success.
 */
char *
bk_string_str2xml(bk_s B, const char *str, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *xml = NULL;
  char *p;
  int len, l;
  char c;
  char *tmp;
  
  len = CHUNK_LEN(strlen(str), 1024);
  if (!(xml = malloc(len)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate xml string: %s\n", strerror(errno));
    goto error;
  }
  
  
  p = xml;
  while((c = *str))
  {
    if (!isprint(c))
    {
      char scratch[100];
      snprintf(scratch, 100,  "#x%x", c);
      l = strlen(scratch);
      memcpy(p, scratch, l);
      len -= l;
      p += l;
    }
    else
    {
      switch (c)
      {
      case '<':
	l = strlen(XML_LT_STR);
	memcpy(p, XML_LT_STR, l);
	len -= l;
	p += l;
	break;
	
      case '>':
	l = strlen(XML_GT_STR);
	memcpy(p, XML_GT_STR, l);
	len -= l;
	p += l;
	break;

      case '&':
	l = strlen(XML_AMP_STR);
	memcpy(p, XML_AMP_STR, l);
	len -= l;
	p += l;
	break;

      default:
	*p = c;
	p++;
	len--;
	break;
      }
    }

    if (len < 100)
    {
      len = CHUNK_LEN(len, 1024);
      if (!(tmp = realloc(xml, len)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not realloc xml string: %s\n", strerror(errno));
	goto error;
      }
      xml = tmp;
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




/**
 * Compute the number of textual columns required to print an integer.
 *
 *	@param B BAKA thread/global state
 *	@param num The integer to be printed
 *	@param base The numerical base for printing (2, 4, 8, 10, or 16)
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_string_intcols(bk_s B, int64_t num, u_int base)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  /*
   * <TODO>replace this with array of thresholds for each base and use binary
   * search to find number of columns</TODO>
   */
  static const double logbase[] =
    { 0.0, 0.0, M_LN2, 0.0, 2.0 * M_LN2, 0.0, 0.0, 0.0, 3.0 * M_LN2, 0.0,
      M_LN10, 0.0, 0.0, 0.0, 0.0, 0.0, 4.0 * M_LN2};
  int val;
  
  if (base > 16 || logbase[base] == 0.0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid base %d\n", base);
    BK_RETURN(B, -1);
  }

  if (num == 0)					// avoid computing log(0)
    BK_RETURN(B, 1);    

  if (num < 0)
  {
    val = 1;
    num *= -1;
  }
  else
    val = 0;
    
  val += 1.0 + log(num) / logbase[base];

  BK_RETURN(B, val);  
}





/**
 * Allocate a string based on a printf like format. This algorithm does
 * waste some space. Worst case (size-1), expected case ((size-1)/2) (or
 * something like that. User must free space with free(3).
 *
 *	@param B BAKA thread/global state.
 *	@param chunk Chunk size to use (0 means user the default)
 *	@param flags Flags.
 * 		BK_STRING_ALLOC_SPRINTF_FLAG_STINGY_MEMORY
 *	@param fmt The format string to use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a malloc'ed <i>string</i> on success.
 */
char *
bk_string_alloc_sprintf(bk_s B, u_int chunk, bk_flags flags, const char *fmt, ...)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int n, size = 2048;
  char *p = NULL;
  va_list ap;

  if (!fmt)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (chunk)
    size = chunk;


  if (!(p = malloc (size)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not alloc string: %s\n", strerror(errno));
    goto error;
  }

  while (1) 
  {
    /* Try to print in the allocated space. */
    va_start(ap, fmt);
    n = vsnprintf (p, size, fmt, ap);
    va_end(ap);

    /* If that worked, return the string. */
    if (n > -1 && n < size)
      break;

    /* Else try again with more space. */
    if (n > -1)    /* glibc 2.1 */
      size = n+1; /* precisely what is needed */
    else           /* glibc 2.0 */
      size *= 2;  /* twice the old size */
    if (!(p = realloc (p, size)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not realloc string: %s\n", strerror(errno));
      goto error;
    }
  }

  if (BK_FLAG_ISSET(flags, BK_STRING_ALLOC_SPRINTF_FLAG_STINGY_MEMORY))
  {
    char *tmp;

    if (!(tmp = strdup(p)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not copy string to minimize memory usage: %s\n", strerror(errno));
      goto error;
    }
    free(p);
    p = tmp;
  }

  BK_RETURN(B,p);      


 error:
  if (p)
    free(p);
  BK_RETURN(B,NULL);  
}




/**
 * Generate a buffer which, as far as possible, is guaranteed to be unique. 
 *
 *	@param B BAKA thread/global state.
 *	@param buf The buffer to fill.
 *	@param len The length of the buffer (we will fill it all).
 *	@param Flags flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_string_unique_string(bk_s B, char *buf, u_int len, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *hostname = NULL;
  struct timeval tv;
  bk_MD5_CTX ctx;
  struct bk_randinfo *ri = NULL;
  char md5_str[32];
  char *p = buf;
  u_int32_t randnum;

  if (!buf)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!(ri = bk_rand_init(B, 0, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not initialize random state\n");
    goto error;
  }
  bk_MD5Init(B, &ctx);

  // Start by collecting some host specific info.

  // Get something unique to this host (<TODO> We could do a much better job here </TODO> )
  if (!(hostname = bk_netutils_gethostname(B)))
  {
    bk_error_printf(B, BK_ERR_WARN, "Could not determine hostname (unique string may not be so unique)\n");
    // Forge on.
  }

  bk_MD5Update(B, &ctx, hostname, strlen(hostname));
  free(hostname);
  hostname = NULL;

  // Get something unique within this host (Let's ignore clock steps)
  if (gettimeofday(&tv, NULL) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not determine the time of day (unique string may not be so unique)\n");
    // Forge on.
  }
  bk_MD5Update(B, &ctx, (char *)&tv, sizeof(tv));

  // Throw in a little more entropy just for kicks.
  randnum = bk_rand_getword(B, ri, NULL, 0);
  bk_MD5Update(B, &ctx, (char *)&randnum, sizeof(randnum));

  bk_MD5Final(B, &ctx);
  
  if (bk_MD5_extract_printable(B, md5_str, &ctx, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not extract md5 information\n");
    goto error;
  }

  while(len)
  {
    int curlen = MAX(len, 16);
    
    bk_MD5Init(B, &ctx);
    
    // OK use previous output (so the "seed" stuff at the top carries forward)
    bk_MD5Update(B, &ctx, md5_str, sizeof(md5_str)-1);

    // And throw in some more entropy
    randnum = bk_rand_getword(B, ri, NULL, 0);
    bk_MD5Update(B, &ctx, (char *)&randnum, sizeof(randnum));

    bk_MD5Final(B, &ctx);

    if (bk_MD5_extract_printable(B, md5_str, &ctx, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not extract md5 information\n");
      goto error;
    }
    
    memcpy(p, md5_str, curlen);
    len -= curlen;
    p += curlen;
  }

  bk_rand_destroy(B, ri, 0);
  BK_RETURN(B,0);  
  
 error:
  if (hostname)
    free(hostname);
  if (ri)
    bk_rand_destroy(B, ri, 0);
  BK_RETURN(B,-1);  
}




/**
 * Convert a string to a double in the BAKA way (ie with copyout)
 *
 *	@param B BAKA thread/global state.
 *	@param string The input string to convert.
 *	@param value The converted value (only valid if return value is 0).
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_string_atod(bk_s B, const char *string, double *value, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  double tmp;
  char *end = NULL;
  int ret = 0;
  
  if (!string || !value)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  // Just make sure this is set.
  *value = 0.0;

  // Set errno, just to be sure.
  errno = 0;
  tmp = strtod(string, &end);
  
  if (tmp == 0.0)
  {
  // Potential error has occured. See strtod man page for expmanations
    if (!end || string == end || errno)
    {
      bk_error_printf(B, BK_ERR_ERR, "Conversion from string to double failed\n");
      ret = -1;
    }
  }
  else if ((tmp == HUGE_VAL || tmp == -HUGE_VAL) && errno)
  {
    bk_error_printf(B, BK_ERR_ERR, "Conversion from string to double failed\n");
    ret = -1;
  }
  else if (isinf(tmp) || isnan(tmp))
  {
    /*
     * It's unclear to jtt that we need this; it's unclear to seth that
     * we don't. We err on the side of safety (and portability :-))
     */
    bk_error_printf(B, BK_ERR_ERR, "Conversion from string to double failed\n");
    ret = -1;
  }
  else
  {
    *value = tmp;
  }

  BK_RETURN(B,ret);
}




/**
 * Convert a string to a float in the BAKA way.
 *
 *	@param B BAKA thread/global state.
 *	@param string The input string to convert.
 *	@param value The converted value (only valid if return value is 0).
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_string_atof(bk_s B, const char *string, float *value, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  double tmp;
  int ret = 0;

  if (!string || !value)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if ((ret = bk_string_atod(B, string, &tmp, flags)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not convert string to float\n");
    ret = -1;
  }
  else if (tmp < -FLT_MAX || tmp > FLT_MAX
	   || (tmp != 0.0 && tmp < FLT_MIN && tmp > -FLT_MIN))
  {
    ret = -1;
  }
  else
  {
    *value = tmp;
  }
  
  BK_RETURN(B,ret);  
}



/**
 * Search a region of memory for any of a set of characters (naively)
 *
 * @param B Baka thread/global environment
 * @param s Memory range we are searching
 * @param accept Characters we are searching for
 * @return <i>NULL</i> if character set does not appear, or other error
 * @return <BR><i>pointer to first match</i> if character set does appear
 */
void *bk_mempbrk(bk_s B, bk_vptr *s, bk_vptr *acceptset)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  void *ret = NULL;
  u_int cntr;

  for (cntr = 0; cntr < s->len; cntr++)
  {
    if (memchr(acceptset->ptr, ((char *)s->ptr)[cntr], acceptset->len))
    {
      ret = (char *)s->ptr + cntr;
      break;
    }
  }

  BK_RETURN(B, ret);
}
