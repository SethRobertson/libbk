#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_string.c,v 1.3 2001/08/27 03:10:23 seth Exp $";
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

#include <libbk.h>
#include "libbk_internal.h"


#define TOKENIZE_FIRST		8		/* How many we will start with */
#define TOKENIZE_INCR		4		/* How many we will expand if we need more */
#define TOKENIZE_STR_FIRST	16		/* How many we will start with */
#define TOKENIZE_STR_INCR	16		/* How many we will expand */

#define S_BASE			0x0		/* Base state */
#define S_SQUOTE		0x1		/* In single quote */
#define S_DQUOTE		0x2		/* In double quote */
#define S_VARIABLE		0x4		/* In variable */
#define S_BSLASH		0x8		/* Backslash found */
#define S_BSLASH_OCT		0x10		/* Backslash octal */
#define S_SPLIT			0x20		/* In split character */
#define INSTATE(x)		(state & (x))	/* Are we in this state */
#define GOSTATE(x)		state = x	/* Become this state  */
#define ADDSTATE(x)		state |= x	/* Superstate (typically variable in dquote) */
#define SUBSTATE(x)		state &= ~(x)	/* Get rid of superstate */



static int bk_string_atou_int(bk_s B, char *string, u_int32_t *value, int *sign, bk_flags flags);




/*
 * Hash a string
 *	
 * The Practice of Programming: Kernighan and Pike: 2.9
 *
 * Note we may not be applying modulus in this function since this is
 * normally used by CLC functions, CLC will supply its own modulus and
 * it is bad voodoo to double modulus if the moduluses are different.
 */
u_int 
bk_strhash(char *a, bk_flags flags)
{
  const u_int M = (unsigned)37U;		/* Multiplier */
  const u_int P = (unsigned)2147486459U;	/* Arbitrary large prime */
  u_int h;

  for (h = 0; *a; a++)
    h = h * M + *a;

  if (flags & BK_STRHASH_NOMODULUS)
    return(h);
  else
    return(h % P);
}



/*
 * Print a binary buffer into human readable form.  Limit 65K bytes.
 * Allocates and returns a character buffer of sufficient size.
 * Caller is required to free this buffer.
 *
 * size = (n/16) * (prefix+61) + strlen(intro) + 10
 *
 * AAA 1234 5678 1234 5678 1234 5678 1234 5678 0123456789abcdef
 */
char *bk_string_printbuf(bk_s B, char *intro, char *prefix, bk_vptr *buf, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  const u_int bytesperline = 16;			/* Must be multiple of bytes per group */
  const u_int hexaddresscols = 4;
  const u_int addressbitspercol = 16;
  const u_int bytespergroup = 2;
  const u_int colsperbyte = 2;
  const u_int colsperline = hexaddresscols + 1 + (bytesperline / bytespergroup) * (colsperbyte + 1) + 1 + bytesperline + 1;
  u_int64_t maxaddress;
  u_int32_t nextaddr, addr, addrgrp, nextgrp, addrbyte, len, curlen;
  char *ret;
  char *cur;

  if (!buf)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,NULL);
  }

  for(len=0,maxaddress=1;len<hexaddresscols;len++) maxaddress *= addressbitspercol;
  if (buf->len >= maxaddress)
  {
    bk_error_printf(B, BK_ERR_ERR, "Buffer size too large to print\n");
    BK_RETURN(B,NULL);
  }

  if (!intro) intro="";
  if (!prefix) prefix="";

  curlen = len = (buf->len / bytesperline) * (strlen(prefix)+colsperline) + strlen(intro) + 2;
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
	  snprintf(cur,curlen,"%02x",((char *)buf->ptr)[addrbyte]);
	curlen -= strlen(cur); cur += strlen(cur);
      }
      strcpy(cur," "); curlen -= strlen(cur); cur += strlen(cur);
    }
    strcpy(cur," "); curlen -= strlen(cur); cur += strlen(cur);

    /* For each byte in line */
    for(addrbyte = addrgrp; addrbyte < nextaddr; addrbyte++)
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



/*
 * Convert ascii string to unsigned int32
 *
 * Returns -1 on error
 * Returns 0 on success
 * Returns pos on invalid string (as much as we have decoded is returned)
 */
int bk_string_atou(bk_s B, char *string, u_int32_t *value, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libsos");
  int sign = 0;
  int ret = bk_string_atou_int(B, string, value, &sign, flags);

  if (sign < 0)
    *value = 0;

  BK_RETURN(B, ret);
}



/*
 * Convert ascii string to unsigned int32 with sign extension
 *
 * Returns -1 on error
 * Returns 0 on success
 * Returns pos on invalid string (as much as we have decoded is returned)
 */
static int bk_string_atou_int(bk_s B, char *string, u_int32_t *value, int *sign, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libsos");
  signed char decode[256];
  int base;
  u_int tmp;
  int neg = 1;

  if (!string || !value)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, -1);
  }

  for(tmp=0;tmp<sizeof(decode);tmp++)
    decode[tmp] = -1;
  for(tmp='0';tmp<='9';tmp++)
    decode[tmp] = tmp - '0';
  for(tmp='A';tmp<='Z';tmp++)
    decode[tmp] = tmp - 'A' + 10;
  for(tmp='a';tmp<='z';tmp++)
    decode[tmp] = tmp - 'a' + 10;

  /* Skip over leadings space */
  for(;*string && isspace(*string);string++) ;

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
      base = 16; string += 2; break;
    case 'X':
      base = 16; string += 2; break;
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
    int x = decode[(int)*string];

    /* Is this the end of the number? */
    if (x < 0 || x >= base)
      break;

    *value *= base;
    *value += x;
  }
  *sign = neg;
  BK_RETURN(B, *string);
}



/*
 * Convert ascii string to unsigned int
 */
int bk_string_atoi(bk_s B, char *string, int32_t *value, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libsos");
  int sign = 0;
  u_int32_t tmp;
  int ret = bk_string_atou_int(B, string, &tmp, &sign, flags);

  *value = tmp;

  /* Not enough bits -- I guess this is still a pos */
  if (*value < 0)
    *value = 0;

  *value *= sign;

  BK_RETURN(B, ret);
}



/*
 * Tokenize a string into an array of the tokens in the string
 *
 * Yeah, this function uses gotos a bit more than I really like, but
 * avoiding the code duplication is pretty important too.
 */
char **bk_string_tokenize_split(bk_s B, char *src, u_int limit, char *spliton, void *variabledb, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libsos");
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

  if (!(tokenx = bk_memx_create(B, sizeof(char), TOKENIZE_STR_FIRST, TOKENIZE_STR_INCR, 0)) ||
      !(splitx = bk_memx_create(B, sizeof(char *), TOKENIZE_FIRST, TOKENIZE_INCR, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate split extended arrays for split\n");
    goto error;
  }

  if (spliton) spliton = BK_STRING_TOKENIZE_WHITESPACE;

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
      if (strchr(spliton, *curloc))
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
	  (!(*curloc != '0') &&
	   !(*curloc != '1') &&
	   !(*curloc != '2') &&
	   !(*curloc != '3') &&
	   !(*curloc != '4') &&
	   !(*curloc != '5') &&
	   !(*curloc != '6') &&
	   !(*curloc != '7')))
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

	SUBSTATE(S_BSLASH);			/* Revert to previous state */
	goto addnewchar;			/* Actually add the character */
      }

      /* This is an octal character and we have not yet reached the limit */
      continue;
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

	addnewchar:
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
      if (!*curloc || strchr(spliton, *curloc))
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
	GOSTATE(S_BSLASH);
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



/*
 * Destroy the array of strings produced by bk_string_tokenize_split
 */
void bk_string_tokenize_destroy(bk_s B, char **tokenized)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libsos");

  if (!tokenized)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  for(; *tokenized; tokenized++)
    free(*tokenized);
  free(tokenized);

  BK_VRETURN(B);
}
