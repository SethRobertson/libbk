#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_string.c,v 1.108 2004/05/28 21:00:28 jtt Exp $";
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
 * Random useful string functions
 */

#include <libbk.h>
#include "libbk_internal.h"



/**
 * @name bk_str_registry
 * This the container for the registry.
 */
struct bk_str_registry
{
  bk_flags				bsr_flags; ///< Everyone needs flags. (NB Shares flags space with bk_str_registry_element
  struct bk_str_registry_element **	bsr_registry; ///< Array of registry items.
  u_int					bsr_next_index;	///< The next registry index to assign.
  u_int					bsr_registrysz;	///< The current allocated size of the registry.
#ifdef BK_USING_PTHREADS
  pthread_mutex_t			bsr_lock; ///< Insert lock
#endif /* BK_USING_PTHREADS */
};



/**
 * @name bk_str_registry_element
 * This is the structure which maps a string to an identifier
 */
struct bk_str_registry_element
{
  bk_flags		bsre_flags;		///< Everyone needs flags. (NB Shares flag space with bk_str_registry)
  const char *		bsre_str;		///< The saved string.
  bk_str_id_t		bsre_id;		///< The id of this string.
  u_int			bsre_ref;		///< Reference count
#ifdef BK_USING_PTHREADS
  pthread_mutex_t	bsre_lock;		///< Registry Lock
#endif /* BK_USING_PTHREADS */

};



#define TOKENIZE_FIRST		8		///< How many we will start with
#define TOKENIZE_INCR		4		///< How many we will expand if we need more
#define TOKENIZE_STR_FIRST	16		///< How many we will start with
#define TOKENIZE_STR_INCR	16		///< How many we will expand
#define MAXVARIABLESIZE		1024		///< Maximum size of a variable

#define S_SQUOTE		0x1		///< In single quote
#define S_DQUOTE		0x2		///< In double quote
#define S_VARIABLE		0x4		///< In $variable (saw at least $)
#define S_BSLASH		0x8		///< Backslash found
#define S_BSLASH_OCT		0x10		///< Backslash octal
#define S_SPLIT			0x20		///< In split character
#define S_BASE			0x40		///< Base state
#define S_VARIABLEDELIM		0x80		///< In ${variable} (saw at least ${)
#define INSTATE(x)		(state & (x))	///< Are we in this state
#define GOSTATE(x)		state = x	///< Become this state
#define ADDSTATE(x)		state |= x	///< Superstate (typically variable in dquote)
#define SUBSTATE(x)		state &= ~(x)	///< Get rid of superstate

/**
 * Check to see if the limit on numbers of tokens has been reached or not.
 * Yes, limit>1 and limit-- will always have the same truth value.  Don't ask?
 */
#define LIMITNOTREACHED	(!limit || (limit > 1 && limit--))

static struct bk_str_registry_element *bsre_create(bk_s B, const char *str, bk_flags flags);
static void bsre_destroy(bk_s B, struct bk_str_registry_element *bsre);


/**
 * Hash a string into tiny little bits two different ways.
 *
 * Not covered under the LGPL
 *
 * No 'B' passed in so that this can be used in CLC comparison functions.
 * (Huh?  That doesn't make sense.  The API is different.  SJR admits that
 * that it doesn't need it, but the argument made here is just *wrong*)
 *
 * THREADS: MT-SAFE
 *
 *	@param k The string to be hashed
 *	@param flags BK_HASH_MODULUS perform modulus by large prime<br>
 *	BK_HASH_V2 use better, but slower hashing function
 *	@return <i>hash</i> of number
 */
u_int
bk_strhash(const char *k, bk_flags flags)
{
  const u_int32_t P = 2147486459U;		// Arbitrary large prime
  u_int h;

  if (BK_FLAG_ISCLEAR(flags, BK_HASH_V2))
  {
    /*
     * Hash a string.
     *
     * The Practice of Programming: Kernighan and Pike: 2.9 (i.e. not covered
     * under LGPL)
     *
     * Note we may not be applying modulus in this function since this is
     * often used by CLC functions, CLC will supply its own modulus and
     * it is bad voodoo to double modulus if the moduli are different.
     */
    const u_int M = 37U;			// Multiplier

    for (h = 17; *k; k++)
      h = h * M + *k;
  }
  else
  {
    /*
     * Hash a string into tiny little bits.
     *
     * Not covered under the normal BK license.
     *
     * Returns a 32-bit value.  Every bit of the key affects every bit of
     * the return value.  Every 1-bit and 2-bit delta achieves avalanche.
     * About 6*len+35 instructions.
     *
     * The best hash table sizes are powers of 2.  There is no need to do
     * mod by a prime (mod is sooo slow!).  If you need less than 32 bits,
     * use a bitmask.
     *
     * By Bob Jenkins, 1996.  bob_jenkins@burtleburtle.net.  You may use this
     * code any way you wish, private, educational, or commercial.  It's free.
     *
     * See http://burtleburtle.net/bob/hash/evahash.html
     * Use for hash table lookup, or anything where one collision in 2^^32 is
     * acceptable.  Do NOT use for cryptographic purposes.
     */

    /*
     * mix -- mix 3 32-bit values reversibly.
     * For every delta with one or two bits set, and the deltas of all three
     *   high bits or all three low bits, whether the original value of a,b,c
     *   is almost all zero or is uniformly distributed,
     * * If mix() is run forward or backward, at least 32 bits in a,b,c
     *   have at least 1/4 probability of changing.
     * * If mix() is run forward, every bit of c will change between 1/3 and
     *   2/3 of the time.  (Well, 22/100 and 78/100 for some 2-bit deltas.)
     * mix() was built out of 36 single-cycle latency instructions in a
     *   structure that could supported 2x parallelism, like so:
     *       a -= b;
     *       a -= c; x = (c>>13);
     *       b -= c; a ^= x;
     *       b -= a; x = (a<<8);
     *       c -= a; b ^= x;
     *       c -= b; x = (b>>13);
     *       ...
     *   Unfortunately, superscalar Pentiums and Sparcs can't take advantage
     *   of that parallelism.  They've also turned some of those single-cycle
     *   latency instructions into multi-cycle latency instructions.  Still,
     *   this is the fastest good hash I could find.  There were about 2^^68
     *   to choose from.  I only looked at a billion or so.
     */
#define mix(a,b,c)				\
{						\
  a -= b; a -= c; a ^= (c>>13);			\
  b -= c; b -= a; b ^= (a<<8);			\
  c -= a; c -= b; c ^= (b>>13);			\
  a -= b; a -= c; a ^= (c>>12);			\
  b -= c; b -= a; b ^= (a<<16);			\
  c -= a; c -= b; c ^= (b>>5);			\
  a -= b; a -= c; a ^= (c>>3);			\
  b -= c; b -= a; b ^= (a<<10);			\
  c -= a; c -= b; c ^= (b>>15);			\
}

    register u_int32_t length = strlen(k);	// the length of the key
    register u_int32_t a,b,len;

  // Set up the internal state
    len = length;
    a = b = 0x9e3779b9;				// the golden ratio; an arbitrary value
    h = 0x8c7eaa15;				// First 7 primes: an arbitrary value

    // Handle most of the key
    while (len >= 12)
    {
      a += (k[0] +((u_int32_t)k[1]<<8) +((u_int32_t)k[2]<<16) +((u_int32_t)k[3]<<24));
      b += (k[4] +((u_int32_t)k[5]<<8) +((u_int32_t)k[6]<<16) +((u_int32_t)k[7]<<24));
      h += (k[8] +((u_int32_t)k[9]<<8) +((u_int32_t)k[10]<<16)+((u_int32_t)k[11]<<24));
      mix(a,b,h);
      k += 12; len -= 12;
    }

    // Handle the last 11 bytes
    h += length;
    switch(len)					// all the case statements fall through
    {
    case 11: h+=((u_int32_t)k[10]<<24);
    case 10: h+=((u_int32_t)k[9]<<16);
    case 9 : h+=((u_int32_t)k[8]<<8);
      // the first byte of h is reserved for the length
    case 8 : b+=((u_int32_t)k[7]<<24);
    case 7 : b+=((u_int32_t)k[6]<<16);
    case 6 : b+=((u_int32_t)k[5]<<8);
    case 5 : b+=k[4];
    case 4 : a+=((u_int32_t)k[3]<<24);
    case 3 : a+=((u_int32_t)k[2]<<16);
    case 2 : a+=((u_int32_t)k[1]<<8);
    case 1 : a+=k[0];
      // case 0: nothing left to add
    }
    mix(a,b,h);
  }
#undef mix

  if (BK_FLAG_ISCLEAR(flags, BK_HASH_MODULUS))
    return(h);
  else
    return(h % P);
}



/**
 * Hash a buffer.
 *
 * Special case small buffers (lengths 1, 2, 4, or 8) on the assumption that
 * these are actually byte/short/int/long or double values rather than strings.
 * For other lengths, string hashing is used.
 *
 * <WARNING>For numeric values, this prevents endian bias in the hash, but for
 * IPv4 addresses or string types, it creates an endian bias; to prevent that,
 * the BK_HASH_STRING flag should be passed.  For CLC hash tables, this isn't
 * an issue, but for hash tables that may be exported (e.g. FAD hash models)
 * this is significant.</WARNING>
 *
 * THREADS: MT-SAFE
 *
 *	@param b The buffer to be hashed
 *	@param flags BK_HASH_MODULUS perform modulus by large prime<br>
 *	BK_HASH_STRING string hash all buffers, even for small powers of 2
 *	@return <i>hash</i> of number
 *
 * <TODO>BK_HASH_V2 use better, but slower hashing function, e.g. as above or
 * http://www.concentric.net/~Ttwang/tech/inthash.htm for numerics</TODO>
 */
u_int
bk_bufhash(const struct bk_vptr *b, bk_flags flags)
{
  const u_int M = 37U;				// Multiplier
  const u_int P = 2147486459U;			// Arbitrary large prime
  u_int h;
  size_t i;
  const char *p = (const char *)b->ptr;

  switch (b->len)
  {
  case 1:
    h = *p;
    break;

  case 2:
    h = *(const short *)p;
    break;

  case 4:
    h = *(const int *)p;
    break;

  case 8:
    h = ((const int *)p)[0] ^ ((const int *)p)[1];
    break;

  default:
    for (h = 17, i = 0; i < b->len; p++, i++)
      h = h * M + *p;
    break;
  }

  if (BK_FLAG_ISCLEAR(flags, BK_HASH_MODULUS))
    return(h);
  else
    return(h % P);
}



/**
 * Print a binary buffer into human readable form.  Limit 65K bytes.
 * Allocates and returns a character buffer of sufficient size.
 * Caller is required to free this buffer.
 * size = (n/16) * (prefix+61) + strlen(intro) + 10
 *
 * Example output line:
 *
 * PREFIXAddress  Hex dump of data                     ASCII interpretation
 * PREFIXAddr 1234 5678 1234 5678 1234 5678 1234 5678  qwertu...&*ptueo
 *
 * THREADS: MT-SAFE
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
 * Tokenize a string into an array of the tokens in the string
 *
 * Yeah, this function uses gotos a bit more than I really like, but
 * avoiding the code duplication is pretty important too.
 *
 * If the variable database variables are set, variable expansion of the form
 * $[A-Za-z0-9_]+ and ${[A-Za-z0-9_]+} will take place, in unquoted and double-quoted
 * strings.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state
 *	@param src Source string to tokenize
 *	@param limit Maximum number of tokens to generate--last token contains "rest" of string.  Zero for unlimited.
 *	@param spliton The string containing the character(s) which separate tokens
 *	@param kvht_vardb Key-value hash table for variable substitution
 *	@param variabledb Environ-style environment for variable substitution
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
 *		quotes are not treated specially.  See @a libbk.h for
 *		details. You should call @a bk_string_tokenize_destroy
 *		to free up the generated array.
 *		BK_STRING_TOKENIZE_CONF_EXPAND if set will allow you to
 *		expand keys using values from you bk_conf.
 *	@return <i>NULL</i> on call failure, allocation failure, other failure
 *	@return <br><i>null terminated array of token strings</i> on success.
 */
char **bk_string_tokenize_split(bk_s B, const char *src, u_int limit, const char *spliton, const dict_h kvht_vardb, const char **variabledb, bk_flags flags)
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
  char varspace[MAXVARIABLESIZE];

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
  retry:
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

    /* We have seen a dollar sign */
    if (INSTATE(S_VARIABLE))
    {
      if (*curloc == '{' && (curloc == (startseq + 1)))
      {
	SUBSTATE(S_VARIABLE);
	ADDSTATE(S_VARIABLEDELIM);
	continue;
      }

      if (((*curloc >= 'A') && (*curloc <= 'Z')) ||
	  ((*curloc >= 'a') && (*curloc <= 'z')) ||
	  ((*curloc >= '0') && (*curloc <= '9')) ||
	  (*curloc == '_'))
      {
	// Still in variable
	continue;
      }

      // At end of variable
      if (curloc == (startseq + 1))
      {
	// Variable was empty "$" -> ""
      }
      else
      {
	startseq++;
	if ((curloc - startseq + 1) < MAXVARIABLESIZE)
	{
	  const char *replace = NULL;
	  int len;

	  memcpy(varspace,startseq,curloc-startseq);
	  varspace[curloc-startseq] = 0;

	envreplace:
	  replace = NULL;
	  if (!replace && kvht_vardb)
	  {
	    replace = ht_search(kvht_vardb,varspace);
	  }
	  if (!replace && variabledb)
	  {
	    char **tmpenv = environ;
	    environ = (char **)variabledb;
	    replace = bk_getenv(B, varspace);
	    environ = tmpenv;
	  }

	  if (!replace && BK_FLAG_ISSET(flags, BK_STRING_TOKENIZE_CONF_EXPAND))
	  {
	    replace = BK_GWD(B, varspace, NULL);
	  }

	  if (replace && (len = strlen(replace)))
	  {
	    if (!(token = bk_memx_get(B, tokenx, len, NULL, BK_MEMX_GETNEW)))
	    {
	      bk_error_printf(B, BK_ERR_ERR, "Could not extend array for additional character\n");
	      goto error;
	    }
	    memcpy(token,replace,len);
	  }
	}
	// Iff variable too big->replace with empty string
      }
      SUBSTATE(S_VARIABLE);
      SUBSTATE(S_VARIABLEDELIM);
      startseq = NULL;
      goto retry;				// Must handle terminating character in previous state
    }

    /* We have seen a dollar sign */
    if (INSTATE(S_VARIABLEDELIM))
    {
      if (((*curloc >= 'A') && (*curloc <= 'Z')) ||
	  ((*curloc >= 'a') && (*curloc <= 'z')) ||
	  ((*curloc >= '0') && (*curloc <= '9')) ||
	  (*curloc == '_'))
      {
	// Still in variable
	continue;
      }

      // At end of variable
      if (*curloc == '}')
      {
	startseq += 2;

	if ((curloc-startseq) < MAXVARIABLESIZE)
	{
	  memcpy(varspace,startseq,curloc-startseq);
	  varspace[curloc-startseq] = 0;
	  curloc++;				// envreplace will rerun current character
	  goto envreplace;
	}
      }

      // If we got here, it is an illegal variable, we replace with empty string
      SUBSTATE(S_VARIABLEDELIM);
      startseq = NULL;
      goto retry;				// Must handle terminating character in previous state
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

      /* Variable? */
      if ((kvht_vardb || variabledb) && *curloc == 044)
      {
	ADDSTATE(S_VARIABLE);
	startseq = curloc;
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

      /* Variable? */
      if ((kvht_vardb || variabledb) && *curloc == 044)
      {
	ADDSTATE(S_VARIABLE);
	startseq = curloc;
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
 * THREADS: MT-SAFE (as long as tokenized is thread private)
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
 * THREADS: MT-SAFE
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
 * Quote a string, with double quotes, for printing, and subsequent split'ing,
 * and returning a newly allocated string which should be free'd by the caller.
 *
 * split should get the following flags to decode
 * BK_STRING_TOKENIZE_DOUBLEQUOTE|BK_STRING_TOKENIZE_BACKSLASH_INTERPOLATE_OCT
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state
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
    if (*src == '\\' || strchr(needquote, *src) || (BK_FLAG_ISSET(flags, BK_STRING_QUOTE_NONPRINT)?!isprint(*src):0))
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
 * Bounded strlen. Return the length the of a string but go no further than
 * the bound. NB: This function returns a @a ssize_t not a @a size_t as per
 * @a strlen(3).
 *
 * This is so it can return <i>-1</i> when there is no NUL, even at s[max].
 *
 * That's right, if s is not at least max+1 characters long, this function may
 * return indeterminate results, possibly -1, possibly max, possibly (if you
 * are very very very unlucky) SIGSEGV.  If your string is not at least max+1
 * characters long, use GNU glibc strnlen() which has reasonable semantics, and
 * doesn't feel that a function that never returns an error code is somehow
 * inferior to one that sometimes does.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state.
 *	@param s The string to check.
 *	@param max (One less than) the maximum string size to check.
 *	@returns The <i>length</i> of the string on success.
 *	@returns <i>-1</i> on failure (including string too long).
 *	@bugs Does not return the same type as @a strlen(3). See description.
 *	@bugs May return indeterminate result if sizeof(s) == max
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



// <TODO>This can be removed ifdef HAVE_STRNDUP once B is gone</TODO>
/**
 * @a strdup at <em>most</em> @a n bytes of data. Returns NULL terminated.
 *
 * THREADS: MT-SAFE
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

  // <TODO>Should this check len against strlen(s)?  Size/space</TODO>
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



/**
 * Search for a fixed string within a buffer without exceeding a specified
 * max.  This is like @a strstr(3) but doesn't assume that the supplied
 * haystack is null terminated.  The needle is assumed to be null
 * terminated.
 *
 * THREADS: MT-SAFE
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
    upper_bound = haystack + len + 1 - nlen;

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
  if (!haystack || !needle)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B, memmem(haystack, len, needle, strlen(needle)));
#endif /* !HAVE_MEMMEM */
}



/**
 * Search for a bounded string within a null-terminated buffer.  This is like
 * @a strstr(3) but the needle is not assumed to be null terminated.  The
 * haystack is assumed to be null terminated.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param haystack The string in which to search.
 *	@param needle The buffer to search for.
 *	@param nlen The length of needle.
 *	@return <i>NULL</i> on failure.<br>
 *	@return pointer to @a needle in @a haystack on success.
 */
char *
bk_strstrn(bk_s B, const char *haystack, const char *needle, size_t nlen)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
#ifndef HAVE_MEMMEM
  const char *p;
  const char *q;
  const char *upper_bound;
  size_t len;

  if (!haystack || !needle)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  p = haystack;

  len = strlen(haystack);
  if (len >= nlen)
  {
    upper_bound = haystack + len + 1 - nlen;

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
  if (!haystack || !needle)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B, memmem(haystack, strlen(haystack), needle, nlen));
#endif /* !HAVE_MEMMEM */
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
 * THREADS: MT-SAFE
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



// <TODO>This can be removed ifdef HAVE_MEMRCHR once B is gone</TODO>
/**
 * Search len bytes of buffer for the final occurrence of character.
 *
 * THREADS: MT-SAFE
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
 * Compare two arbitrary byte strings.  This is like @a memcmp(3) but
 * handles vptrs of different lengths in much the same way that @a strncmp(3)
 * does.  If the byte strings you will be comparing are known to be of the same
 * length, you may prefer to use memcmp instead, unless you want the string
 * difference return value that this function provides.
 *
 * The return code from this function is negative if the first string is
 * lexicographically less than the second, and positive if the the first string
 * is lexicographically greater than the second.  Furthermore, the magnitude of
 * the return code is a string distance measurement that obeys the following
 * invariants, given 3 strings a, b, and c, all of length <= 8 MB (2^23 bytes):
 *
 *	(a - b) == -1 * (b - a)
 *	(a - c) > (a - b) => (c - b) > 0
 *	(a - b) > 0 => (a - c) >= (b - c)
 *	(a - b) > 0 => (c - b) >= (c - a)
 *
 * Note: (a - b) > 0 does not imply(a - c) > (b - c), nor (c - a) < (c - b),
 * e.g. "ba" - "bb" > 0, but yet "a" - "ba" == "a" - "bb", and furthermore,
 * "ab" - "ba" == "ab" - "bb", so this string difference is only accurate to an
 * "order of magnitude", however, that's the best you can do without a
 * string-valued difference.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param b1 The first byte string
 *	@param b2 The second byte string
 *	@param len1 Length of b1
 *	@param len2 Length of b2
 *	@return <i>negative</i> if b1 compares less than b2<br>
 *	@return <i>0</i> if b1 and b2 are exactly equal<br>
 *	@return <i>positive</i> if b1 compares greater than b2<br>
 */
int
bk_memdiff(bk_s B, const void *b1, const void *b2, u_int len1, u_int len2)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  const u_char *s1 = b1;
  const u_char *s2 = b2;
  u_int i;
  int res = 0;

  for (i = 0; i < len1 && i < len2; i++)
  {
    if ((res = s1[i] - s2[i]))
    {
      i++;
      break;
    }
  }

  // string index difference i=0 => -2^31-1, i=2^23-1 => 0
  i = INT_MIN + (256 * (i + 1));

  if (!res)
  {
    if (len1 == len2)
      BK_RETURN(B, 0);

    if (len1 > len2)
      i = -i;
  }
  else if (res > 0)
    i = -i;

  BK_RETURN(B, i + res);
}



/**
 * Allocate a string based on a printf like format. This algorithm does
 * waste some space. Worst case (size-1), expected case ((size-1)/2) (or
 * something like that. User must free space with free(3).
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param chunk Chunk size to use (0 means user the default)
 *	@param flags Flags.
 *		BK_STRING_ALLOC_SPRINTF_FLAG_STINGY_MEMORY
 *	@param fmt The format string to use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a malloc'ed <i>string</i> on success.
 */
char *
bk_string_alloc_sprintf(bk_s B, u_int chunk, bk_flags flags, const char *fmt, ...)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  va_list ap;
  char *ret = NULL;

  va_start(ap, fmt);
  ret = bk_string_alloc_vsprintf(B, chunk, flags, fmt, ap);
  va_end(ap);

  BK_RETURN(B, ret);
}



/**
 * Allocate a string based on a vprintf like format. This algorithm does
 * waste some space. Worst case (size-1), expected case ((size-1)/2) (or
 * something like that. User must free space with free(3).
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param chunk Chunk size to use (0 means user the default)
 *	@param flags Flags.
 *		BK_STRING_ALLOC_SPRINTF_FLAG_STINGY_MEMORY
 *	@param fmt The format string to use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a malloc'ed <i>string</i> on success.
 */
char *
bk_string_alloc_vsprintf(bk_s B, u_int chunk, bk_flags flags, const char *fmt, va_list ap)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int n, size = 2048;
  char *p = NULL;
  char *tmpp = NULL;

  if (!fmt)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (chunk)
    size = chunk;

  if (!(p = malloc(size)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not alloc string: %s\n", strerror(errno));
    goto error;
  }

  while (1)
  {
    /* Try to print in the allocated space. */
    n = vsnprintf(p, size, fmt, ap);

    /* If that worked, return the string. */
    if (n > -1 && n < size)
      break;

    /* Else try again with more space. */
    if (n > -1)    /* glibc 2.1 */
      size = n+1; /* precisely what is needed */
    else           /* glibc 2.0 */
      size *= 2;  /* twice the old size */
    if (!(tmpp = realloc (p, size)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not realloc string: %s\n", strerror(errno));
      goto error;
    }
    p = tmpp;
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
 * Appends the src string to the dest vstr overwriting the '\0' character at
 * the end of dest, and then adds a terminating '\0' character.  The strings
 * may not overlap, and the dest vstr needn't have enough space for the result.
 * Extra space will be allocated according the the chunk argument.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags.
 *		BK_VSTR_CAT_FLAG_STINGY_MEMORY
 *	@param fmt The format string to use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_vstr_cat(bk_s B, bk_flags flags, bk_vstr *dest, const char *src_fmt, ...)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int n;
  char *p;
  int size;
  va_list ap;

  if (!dest || !dest->ptr)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid Arguments\n");
    goto error;
  }

  // try with available space
  while (1)
  {
    int available = dest->max - dest->cur;

    /* Try to print in the allocated space. */
    va_start(ap, src_fmt);
    n = vsnprintf (dest->ptr + dest->cur, available + 1, src_fmt, ap);
    va_end(ap);

    /* If that worked, return the string. */
    if ((n > -1) && (n <= available))
    {
      // update length counter in vstr
      dest->cur += n;
      break;
    }

    /* Else try again with more space. */
    if ((n > -1) && BK_FLAG_ISSET(flags, BK_VSTR_CAT_FLAG_STINGY_MEMORY))    /* glibc 2.1 */
    {
      size = n+1; /* precisely what is needed */
    }
    else           /* glibc 2.0 */
    {
      size = (dest->max + 1) * 2;  /* twice the old size */
    }
    if (!(p = realloc (dest->ptr, size)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not realloc string: %s\n", strerror(errno));
      goto error;
    }
    dest->ptr = p;
    dest->max = size - 1;			// don't include NULL space
  }

  if (BK_FLAG_ISSET(flags, BK_VSTR_CAT_FLAG_STINGY_MEMORY))
  {
    char *tmp;

    if (!(tmp = strdup(dest->ptr)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not copy string to minimize memory usage: %s\n", strerror(errno));
      goto error;
    }
    free(dest->ptr);
    dest->ptr = tmp;

    dest->max = dest->cur;
  }

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}



/**
 * Generate a buffer which, as far as possible, is guaranteed to be
 * unique by the power of entropy.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param buf The buffer to fill.
 *	@param len The length of the buffer (we will fill it all).
 *	@param Flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_string_unique_string(bk_s B, char *buf, u_int len, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_truerandinfo *ri = NULL;
  char *p = buf;
  u_int32_t val;

  if (!buf)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  // <TODO>Figure out some way to allow use of global random pool</TODO>
  if (!(ri = bk_truerand_init(B, 0, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not initialize random state\n");
    goto error;
  }

  while(len)
  {
    int curlen;
    int eos_space;

    if (len > sizeof(val)*2)
    {
      curlen = sizeof(val)*2;
      eos_space = 1;
    }
    else
    {
      curlen = MIN(len, sizeof(val)*2);
      eos_space = 0;
    }

    val = bk_truerand_getword(B, ri, NULL, 0);

    snprintf(p, curlen + eos_space, "%08x", val);

    len -= curlen;
    p += curlen;
  }

  bk_truerand_destroy(B, ri);
  BK_RETURN(B,0);

 error:
  if (ri)
    bk_truerand_destroy(B, ri);
  BK_RETURN(B,-1);
}



/**
 * Search a region of memory for any of a set of characters (naively)
 *
 * THREADS: MT-SAFE
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



/**
 * Initialize the string registry.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a bk_str_registry on success.
 */
bk_str_registry_t
bk_string_registry_init(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_str_registry *bsr = NULL;

  if (!(BK_CALLOC(bsr)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate string registry: %s\n", strerror(errno));
    goto error;
  }

  if (!(BK_MALLOC_LEN(bsr->bsr_registry, (sizeof(*bsr->bsr_registry) * 1024))))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate initial chunk for string registry: %s\n", strerror(errno));
    goto error;
  }
  bsr->bsr_registrysz = 1024;
  // We purposefully leave 0 unregistered. We *could* program around this, but why bother?
  bsr->bsr_next_index = 1;
  bsr->bsr_registry[0] = NULL;

  BK_RETURN(B,bsr);

 error:
  if (bsr)
    bk_string_registry_destroy(B, bsr);
  BK_RETURN(B,NULL);
}



/**
 * Destroy a string registry
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param bsr The registry to fully destroy.
 */
void
bk_string_registry_destroy(bk_s B, bk_str_registry_t bsr)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  u_int cnt;

  if (!bsr)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (bsr->bsr_registry)
  {
    for(cnt = 0; cnt < bsr->bsr_next_index; cnt++)
    {
      if (bsr->bsr_registry[cnt])
	bsre_destroy(B, bsr->bsr_registry[cnt]);
    }

    free(bsr->bsr_registry);
  }

  free(bsr);
  BK_VRETURN(B);
}



/**
 * Create a bsre
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static struct bk_str_registry_element *
bsre_create(bk_s B, const char *str, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_str_registry_element *bsre = NULL;

  if (!str)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!(BK_CALLOC(bsre)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bsre: %s\n", strerror(errno));
    goto error;
  }
#ifdef BK_USING_PTHREADS
  pthread_mutex_init(&bsre->bsre_lock, NULL);
#endif /* BK_USING_PTHREADS */

  if (BK_FLAG_ISSET(flags, BK_STR_REGISTRY_FLAG_COPY_STR))
  {
    if (!(bsre->bsre_str = strdup(str)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not strdup string: %s\n", strerror(errno));
      goto error;
    }
  }
  else
  {
    bsre->bsre_str = str;
  }

  bsre->bsre_flags = flags;
  bsre->bsre_ref = 0;

  BK_RETURN(B,bsre);

 error:
  if (bsre)
    bsre_destroy(B, bsre);
  BK_RETURN(B,NULL);
}



/**
 * Destroy a bsre
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param bsre The structure to destroy.
 */
static void
bsre_destroy(bk_s B, struct bk_str_registry_element *bsre)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bsre)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (BK_FLAG_ISSET(bsre->bsre_flags, BK_STR_REGISTRY_FLAG_COPY_STR) &&
      bsre->bsre_str)
  {
    free((void *)bsre->bsre_str);
  }

#ifdef BK_USING_PTHREADS
  pthread_mutex_destroy(&bsre->bsre_lock);
#endif /* BK_USING_PTHREADS */

  free(bsre);
  BK_VRETURN(B);
}



/**
 * Retrieve a string id without "registering" it. This function should be
 * used with caution. Don't cache the value too long as it may become
 * invalid.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param handle the registry handle to use.
 *	@param str The string to insert
 *	@param flag Flags.
 *	@return <i>0</i> on FAILURE (!! THIS IS NOT NORMAL FOR LIBBK !!).<br>
 *	@return <i>positive</i> on success.
 */
bk_str_id_t
bk_string_registry_idbystr(bk_s B, bk_str_registry_t bsr, const char *str, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_str_registry_element *bsre = NULL;
  u_int cnt;

  if (!bsr || !str)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, 0);
  }

  for (cnt = 0; cnt < bsr->bsr_next_index; cnt++)
  {
    bsre = bsr->bsr_registry[cnt];
    if (bsre && BK_STREQ(bsre->bsre_str, str))
      BK_RETURN(B, bsre->bsre_id);
  }

  BK_RETURN(B, 0);
}



/**
 * Delete a string from the registry
 *
 * THREADS: MT-SAFE (assuming different handle)
 * THREADS: THREAD-REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param handle The handle on the registry.
 *	@param str The string to delete.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_string_registry_delete_str(bk_s B, bk_str_registry_t bsr, const char *str, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_str_registry_element *bsre;
  u_int cnt;

  if (!bsr || !str)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  for(cnt = 0; cnt < bsr->bsr_next_index; cnt++)
  {
    bsre = bsr->bsr_registry[cnt];
    if (bsre && BK_STREQ(bsre->bsre_str, str))
      break;
  }

  BK_RETURN(B,bk_string_registry_delete_id(B, bsr, cnt, flags));
}



/**
 * Delete a registry object via its id.
 *
 *	@param B BAKA thread/global state.
 *	@param handle The handle on the registry.
 *	@param id The index of the entry to delete
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_string_registry_delete_id(bk_s B, bk_str_registry_t bsr, bk_str_id_t id, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_str_registry_element *bsre;
  int ret;

  if (!bsr || !id) // id == 0 is reserved and never assigned.
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (id >= bsr->bsr_next_index || !(bsre = bsr->bsr_registry[id]))
  {
    bk_error_printf(B, BK_ERR_WARN, "Registry element %d either does not exist or has already been deleted\n", id);
    // We call this success.
    BK_RETURN(B,0);
  }

  BK_SIMPLE_LOCK(B, &bsre->bsre_lock);

  ret = --bsre->bsre_ref;

  BK_SIMPLE_UNLOCK(B, &bsre->bsre_lock);

  if (ret > 0)
  {
    /* Someone is still using this, so we're done */
    BK_RETURN(B,0);
  }

  bk_debug_printf_and(B, 1, "%d !=> %s\n", bsre->bsre_id, bsre->bsre_str);

  bsre_destroy(B, bsre);

  BK_SIMPLE_LOCK(B, &bsr->bsr_lock);

  bsr->bsr_registry[id] = NULL;
  /*
   * If we're actually deleting the entry with the highest index then we
   * actually "recover" the space (by descrementing next_index). We *only*
   * special case the top index for two reasons: 1) it's easy to recover
   * this space ("recovering" an index less then the highest can only be
   * done by a search at insert time) and 2) it (more or less) covers the
   * "common" case where an object was registered during its creation but
   * then, owing to some chance, has its creation rolled back. This is the
   * most likely cause of a delete occuring before the total destruction of
   * the registry.
   *
   * And actually while we're at it we pick up any deleted entries which
   * happen to be adjacent to the end. Why not?
   */

  if (id == bsr->bsr_next_index-1)
    while (!bsr->bsr_registry[id--])
      bsr->bsr_next_index--;

  BK_SIMPLE_UNLOCK(B, &bsr->bsr_lock);

  BK_RETURN(B,0);
}



/**
 * Obtain the ID of a string which has been inserted into the
 * registry.
 *
 * This is kinda, err, slow if there are many items in the list.
 *
 * Update: Well it's only slow if you have use @a name. If you already know
 * the @a id, meaning someone else has registered the string and you are
 * only "taking out a lock" on it, then this function is pretty darn
 * fast. If you do not know the @a id, you pass 0 (which is reserved as
 * never used by the registry); if you pass in *both* @a name and @a id, @a
 * id will be preferred as it (to reiterate) is much faster.
 *
 * THREADS: MT-SAFE (assuming different handle)
 * THREADS: THREAD-REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param handle The registry to search within.
 *	@param str The string to search for.
 *	@param id The id to use.
 *	@param flags BK_STR_REGISTRY_FLAG_COPY_STR to copy string arg<br>
 *	BK_STR_REGISTRY_FLAG_FORCE to force insertion when string is already
 *	present in, or if non-zero id is absent from, registry
 *	@return <i>0</i> on FAILURE (!! NB THIS IS NOT NORMAL FOR LIBBK !!).<br>
 *	@return <i>positive</i> on success.
 */
bk_str_id_t
bk_string_registry_insert(bk_s B, bk_str_registry_t bsr, const char *str, bk_str_id_t id, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_str_registry_element *bsre = NULL;
  int locked = 0;
  int forced = BK_FLAG_ISSET(flags, BK_STR_REGISTRY_FLAG_FORCE);

  if (!bsr || !(str || id))
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, 0);
  }

  BK_SIMPLE_LOCK(B, &bsr->bsr_lock);
  locked = 1;

  // if forced and non-zero id, set next index to one more than max(id,next)
  if ((id >= bsr->bsr_next_index) && forced)
  {
    u_int i = bsr->bsr_next_index;

    bsr->bsr_next_index = id + 1;

    while (i < bsr->bsr_registrysz && i <= id)
      bsr->bsr_registry[i++] = NULL;
  }

  /*
   * Assuming that we're not just updating an existing node and charging right
   * ahead with allocating more space for an insert if needed may be a touch
   * aggressive, but it results in at *most* one extra realloc(3).
   */
  if (bsr->bsr_next_index >= bsr->bsr_registrysz)
  {
    struct bk_str_registry_element **tmp;
    u_int i = bsr->bsr_registrysz;

    bsr->bsr_registrysz = bsr->bsr_next_index + 1024;
    if (!(tmp = realloc(bsr->bsr_registry, sizeof(*bsr->bsr_registry)
			* (bsr->bsr_registrysz))))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not expand string registry: %s\n", strerror(errno));
      goto error;
    }
    bsr->bsr_registry = tmp;

    while (i < bsr->bsr_next_index)
      bsr->bsr_registry[i++] = NULL;
  }

  if (!id)
  {
    u_int cnt;

    // search for existing entry (unless forced)
    if (!forced)
    {
      for(cnt = 0; cnt < bsr->bsr_next_index; cnt++)
      {
	bsre = bsr->bsr_registry[cnt];
	if (bsre && BK_STREQ(str, bsre->bsre_str))
	  break;
      }
    }
    else			// allocate next id if FORCE flag is passed
      cnt = bsr->bsr_next_index;

    if (cnt == bsr->bsr_next_index)
    {
      if (!(bsre = bsre_create(B, str, flags)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not create string registry element\n");
	goto error;
      }
      bsre->bsre_id = bsr->bsr_next_index;
      bsr->bsr_registry[bsr->bsr_next_index] = bsre;
      bsr->bsr_next_index++;

      bk_debug_printf_and(B, 1, "%d ==> %s\n", bsre->bsre_id, bsre->bsre_str);
    }
  }
  else if (id >= bsr->bsr_next_index || (!(bsre = bsr->bsr_registry[id])
					 && !forced))
  {
    bk_error_printf(B, BK_ERR_ERR, "Registry object %d does not exist or has been deleted\n", id);
    BK_SIMPLE_UNLOCK(B, &bsr->bsr_lock);
    BK_RETURN(B,0);
  }
  else if (forced && !bsre)
  {
    if (!(bsre = bsre_create(B, str, flags)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create string registry element\n");
      goto error;
    }
    bsre->bsre_id = id;
    bsr->bsr_registry[id] = bsre;

    bk_debug_printf_and(B, 1, "%d ==> %s\n", bsre->bsre_id, bsre->bsre_str);
  }

  // Lock the bsre before releasing the bsr which contains it.
  BK_SIMPLE_LOCK(B, &bsre->bsre_lock);

  BK_SIMPLE_UNLOCK(B, &bsr->bsr_lock);
  locked = 0;

  bsre->bsre_ref++;

  BK_SIMPLE_UNLOCK(B, &bsre->bsre_lock);

  if (str && !BK_STREQ(str, bsre->bsre_str))
  {
    bk_error_printf(B, BK_ERR_ERR, "Registry id %d ==> %s (not %s!)\n",
		    bsre->bsre_id, bsre->bsre_str, str);
    goto error;
  }

  BK_RETURN(B,bsre->bsre_id);

 error:
  if (bsre)
  {
    // If we've inserted id, do a full delete, otherwise just nuke the bsre element.
    if (bsre->bsre_id)
      bk_string_registry_delete_id(B, bsr, bsre->bsre_id, 0);
    else
      bsre_destroy(B, bsre);
  }

  if (locked)
    BK_SIMPLE_UNLOCK(B, &bsr->bsr_lock);

  BK_RETURN(B,0);
}



/**
 * This is exactly like calling bk_string_registry_insert() with the @a id
 * > 0 and @a str == NULL, but it <em>returns</em> a string.
 *
 *	@Param B BAKA thread/global state.
 *	@param handle The registry to search within.
 *	@param id The id to search for.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>saved string</i> on success.
 */
const char *
bk_string_registry_register_by_id(bk_s B, bk_str_registry_t bsr, bk_str_id_t id, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_str_registry_element *bsre = NULL;

  if (!bsr || !id)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }
    
  BK_SIMPLE_LOCK(B, &bsr->bsr_lock);

  if (id >= bsr->bsr_next_index || !(bsre = bsr->bsr_registry[id]))
  {
    bk_error_printf(B, BK_ERR_ERR, "Registry object %d does not exist or has been deleted\n", id);
    BK_SIMPLE_UNLOCK(B, &bsr->bsr_lock);
    BK_RETURN(B, NULL);
  }
  
  // Lock the bsre before releasing the bsr which contains it.
  BK_SIMPLE_LOCK(B, &bsre->bsre_lock);

  BK_SIMPLE_UNLOCK(B, &bsr->bsr_lock);
 
  bsre->bsre_ref++;

  BK_SIMPLE_UNLOCK(B, &bsre->bsre_lock);

  bk_debug_printf_and(B, 1, "%d ==> %s\n", id, bsre->bsre_str);

  BK_RETURN(B, bsre->bsre_str);  
}



/**
 * Return string with variables expanded from both enviornment and bk_conf,
 * optionally freed.  Allows backslash quoting ala BK_STRING_TOKENIZE_BACKSLASH.
 *
 * THREADS: MT-SAFE
 *
 * @param B Baka Thread/global environment
 * @param src Source string
 * @param kvht_vardb Key Value hash table for variables
 * @param envdb environ(5) style environment
 * @param flags BK_STRING_EXPAND_FREE to free source string (whether successful or not!!!)
 * @return <b>NULL</b> on call failure, allocation failure
 * @return <br><b>expanded string</b> on success
 */
char *bk_string_expand(bk_s B, char *src, const dict_h kvht_vardb, const char **envdb, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *ret = NULL;
  char **tokens = NULL;

  if (!src || (!kvht_vardb && !envdb))
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    goto error;
  }

  if (!(tokens = bk_string_tokenize_split(B, src, 1, NULL, kvht_vardb, envdb, BK_STRING_TOKENIZE_BACKSLASH | BK_STRING_TOKENIZE_CONF_EXPAND)) ||
      !(ret = tokens[0]))
  {
    bk_error_printf(B, BK_ERR_ERR, "Tokenization couldn't perform actual expansion\n");
    goto error;
  }
  tokens[0] = NULL;				// <TRICKY>Preserve contents across destroy</TRICKY>
  bk_string_tokenize_destroy(B, tokens);

  if (BK_FLAG_ISSET(flags, BK_STRING_EXPAND_FREE))
    free(src);

  BK_RETURN(B, ret);

 error:
  if (BK_FLAG_ISSET(flags, BK_STRING_EXPAND_FREE))
    free(src);

  if (tokens)
    bk_string_tokenize_destroy(B, tokens);

  if (ret)
    free(ret);

  BK_RETURN(B, NULL);
}



/**
 * Quote a string for use in CSV (comma separated value) mode. The @a
 * quote_str contains the list of characters which require quoting. To date
 * only double quote ("), single quote ('), and newline (\n) are
 * valid. Quotes are "quoted" by doubling them; newline is quoted by
 * replacing it with the strin \n. If the @a quote_str == NULL then the
 * default bahavior of quoting only double quotes is assumed. To quote *no
 * charater* pass in an empty @a quote_str. If the output is truncated, no
 * attempt is made to determine how much space will be needed. The caller
 * simply has to retry with increasingly higher values untl the function
 * succeeds.
 *
 *	@param B BAKA thread/global state.
 *	@param in_str The input string to quote.
 *	@param in_len The length of the input
 *	@param out_str The output (ie quoted) string.
 *	@param out_len The length of the output string.
 *	@param quote_str The string of characters to quote.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i> >0 </i> the number of bytes used output.
 */
int
bk_string_csv_quote(bk_s B, const char *in_str, int in_len, char *out_str, int out_len, const char *quote_str, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int in_index = 0;
  int out_index = 0;

  if (!in_str || !out_str)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    goto error;
  }

  if (!quote_str)
    quote_str = "\"";

  for (in_index = 0; in_index < in_len && out_index < out_len; in_index++)
  {
    char c = in_str[in_index];
    if (strchr(quote_str, c))
    {
      switch(c)
      {
      case '\n':
	out_str[out_index++] = '\\';
	if (out_index == out_len)
	  goto done;
	out_str[out_index++] = 'n';
	break;
	
      case '"':
	out_str[out_index++] = '"';
	if (out_index == out_len)
	  goto done;
	out_str[out_index++] = '"';
	break;

      case '\'':
	out_str[out_index++] = '\'';
	if (out_index == out_len)
	  goto done;
	out_str[out_index++] = '\'';
	break;

      default:
	out_str[out_index++] = c;
	break;
      }
    }
    else
    {
      out_str[out_index++] = c;
    }
  }

  // Insert NUL if space allows, but do *NOT* increment out_index (we don't want to count NUL)
  if (out_index < out_len)
    out_str[out_index] = '\0';

 done:
  BK_RETURN(B, out_index);  

 error:
  BK_RETURN(B, -1);  
}
