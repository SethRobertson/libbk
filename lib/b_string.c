#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_string.c,v 1.18 2001/12/11 20:06:47 jtt Exp $";
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

/**
 * @file
 * String utilites
 */


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


static int bk_string_atoull_int(bk_s B, char *string, u_int64_t *value, int *sign, bk_flags flags);




/**
 * Hash a string
 *	
 * The Practice of Programming: Kernighan and Pike: 2.9 (i.e. not covered
 * under LGPL)
 *
 * Note we may not be applying modulus in this function since this is
 * normally used by CLC functions, CLC will supply its own modulus and
 * it is bad voodoo to double modulus if the moduluses are different.
 *
 *	@param a The string to be hashed
 *	@param flags Whether we want this value to be moduloused by a large prime
 *	@return <i>hash</i> of number
 */
u_int 
bk_strhash(char *a, bk_flags flags)
{
  const u_int M = (unsigned)37U;		/* Multiplier */
  const u_int P = (unsigned)2147486459U;	/* Arbitrary large prime */
  u_int h;

  for (h = 0; *a; a++)
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
 *	@param flags Fun for the ruture
 *	@return <i>NULL</i> on callfailure, allocation failure, size failure, etc
 *	@return <br><i>string</i> representing the buffer on success
 */
char *bk_string_printbuf(bk_s B, char *intro, char *prefix, bk_vptr *buf, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  const u_int bytesperline = 16;		// Must be multiple of bytes per group--the maximum number of source bytes displayed on one output line
  const u_int hexaddresscols = 4;		// The number of columns outputting the address takes
  const u_int addressbitspercol = 16;		// The base of the address
  const u_int bytespergroup = 2;		// The number of source bytes outputted per space seperated group
  const u_int colsperbyte = 2;			// The number of columns of output it takes to output one bytes of source data
  const u_int colsperline = hexaddresscols + 1 + (bytesperline / bytespergroup) * (colsperbyte * bytespergroup + 1) + 1 + bytesperline + 1; // Number of output columns for a single line
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
    snprintf(cur,curlen,"%04x ",addr);  curlen -= strlen(cur); cur += strlen(cur);

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
  ret[len] = 0;

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
 *	@return <br><i>>0</i> on non-null terminated number--best effort number conversion still performed
 */
int bk_string_atou(bk_s B, char *string, u_int32_t *value, bk_flags flags)
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
 *	@return <br><i>>0</i> on non-null terminated number--best effort number conversion still performed
 */
int bk_string_atoi(bk_s B, char *string, int32_t *value, bk_flags flags)
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
 *	@return <br><i>>0</i> on non-null terminated number--best effort number conversion still performed
 */
int bk_string_atoull(bk_s B, char *string, u_int64_t *value, bk_flags flags)
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
 *	@return <br><i>>0</i> on non-null terminated number--best effort number conversion still performed
 */
int bk_string_atoill(bk_s B, char *string, int64_t *value, bk_flags flags)
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
 *	@return <br><i>>0</i> on non-null terminated number--best effort number conversion still performed
 */
static int bk_string_atoull_int(bk_s B, char *string, u_int64_t *value, int *sign, bk_flags flags)
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

  /* Skip over leadings space */
  for(;*string && isspace(*string);string++)
    ; // Void

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

  /* Base determiniation */
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
 *	@param spliton The string containing the character(s) which seperate tokens
 *	@param variabledb Future expansion--variable substitution.  Set to NULL for now.
 *	@param flags BK_STRING_TOKENIZE_SKIPLEADING, if set will cause
 *		leading seperator characters to be ignored; otherwise,
 *		an initial zero-length token will be generated.
 *		BK_STRING_TOKENIZE_MULTISPLIT, if set, will cause
 *		multiple seperators in a row to be treated as the same
 *		seperator; otherwise, zero-length tokens will be
 *		generated for every subsequent seperator character.
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
 *		treated specially (modula BACKSLASH_INTERPOLATE).
 *		BK_STRING_TOKENIZE_SINGLEQUOTE, if set, will cause
 *		single quotes to create a string in which the
 *		seperator character, backslashes, and other magic
 *		characters may exist without interpolation; otherwise
 *		single quote is not treated specially.
 *		BK_STRING_TOKENIZE_DOUBLEQUOTE, if set, will cause
 *		double quotes to create a string in which the
 *		seperator character may exist without causing
 *		tokenization seperation (backslashes are still magic
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
char **bk_string_tokenize_split(bk_s B, char *src, u_int limit, char *spliton, void *variabledb, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char **ret;
  char *curloc = src;
  int tmp;
  u_int toklen, state = S_BASE;
  char *startseq = NULL, *token;
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
  for(curloc = src; ; curloc++)
  {
    if (INSTATE(S_SPLIT))
    {
      /* Are we still looking at a seperator? */
      if (strchr(spliton, *curloc) && LIMITNOTREACHED)
      {
	/* Are multiple seperators the same? */
	if (BK_FLAG_ISSET(flags, BK_STRING_TOKENIZE_MULTISPLIT))
	  continue;

	/* Multiple seperators are additonal tokens */
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
 * Rip a string -- terminate it at the first occurance of the terminator characters.,
 * Typipcally used with vertical whitespace to nuke the \r\n stuff.
 *
 *	@param B BAKA Thread/global state
 *	@param string String to examine and change
 *	@param terminators String containing characters to terminate source at, on first occurance--if NULL use vertical whitespace.
 *	@param flags Fun for the future
 *	@return <i>NULL</i> on failure
 *	@return <br><i>modified string</i> on success
 */
char *bk_string_rip(bk_s B, char *string, char *terminators, bk_flags flags)
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
 *	@param needquote Characters which need backslash qutoing (double quote if NULL)
 *	@param flags BK_STRING_QUOTE_NULLOK will convert NULL @a src into BK_NULLSTR.  BK_STRING_QUOTE_NONPRINT will quote non-printable characters.
 *	@return <i>NULL</i> on call failure, allocation failure, other failure
 *	@return <br><i>quoted src</i> on success (you must free)
 */
char *bk_string_quote(bk_s B, char *src, char *needquote, bk_flags flags)
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
    bk_error_printf(B, BK_ERR_ERR, "Could not create expandable output space for quotization\n");
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
char *bk_string_flagtoa(bk_s B, bk_flags src, bk_flags flags)
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
int bk_string_atoflag(bk_s B, char *src, bk_flags *dst, bk_flags flags)
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
bk_strnlen(bk_s B, char *s, ssize_t max)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  ssize_t c=0;

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
 * Convert raw memory coding to and from an efficient portable representation (MIMEB64)
 * ala RFC2045
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
 * seperate each line ("\n" used if the eol string is NULL).  If the eol
 * sequence is non-NULL, it will additionally be used to terminate the last
 * line.
 *
 *	@param B BAKA Thread/Global state
 *	@param src Source memory to convert
 *	@param eolseq End of line sequence.
 *	@return <i>NULL</i> on error
 *	@return <br><i>encoded string</i> on success which caller must free
 */
char *bk_encode_base64(bk_s B, bk_vptr *src, char *eolseq)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *str;					/* string to encode */
  ssize_t len;					/* length of the string */
  char *eol;					/* the end-of-line sequence to use */
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
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate memory(%d) for result: %s\n", rlen, strerror(errno));
    BK_RETURN(B, NULL);
  }

  /* encode */
  for (chunk=0; len > 0; len -= 3, chunk++)
  {
    if (chunk == (MAX_LINE/4))
    {
      char *c = eol;
      char *e = eol + eollen;
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
    char *c = eol;
    char *e = eol + eollen;
    while (c < e)
      *r++ = *c++;
  }
  *r = '\0';					/* NULL terminate */

  BK_RETURN(B, ret);
}



/**
 * Decode a base64 encoded string into memory buffer.
 *
 *	@param B BAKA Thread/Global state
 *	@param src Source memory to convert
 *	@param eolseq End of line sequence.
 *	@return <i>NULL</i> on error
 *	@return <br><i>decoded buffer</i> on success which caller must free (both bk_vptr and embedded string)
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
	    goto thats_it;

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

    if (c[2] == EQ)
      break;
    *r++ = ((c[1] & 0x0F) << 4) | ((c[2] & 0x3C) >> 2);

    if (c[3] == EQ)
      break;
    *r++ = ((c[2] & 0x03) << 6) | c[3];
  }

 thats_it:
  *r = '\0';
  ret->len = strlen(ret->ptr);

  BK_RETURN(B, ret);
}
// @}




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
bk_strndup(bk_s B, const char *s, u_int len)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *new;

  if (!s)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }
  
  if (!(BK_CALLOC_LEN(new, len)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not malloc: %s\n", strerror(errno));
    BK_RETURN(B,NULL);
  }
  
  snprintf(new,len, "%s", s);
  BK_RETURN(B,new);
}
