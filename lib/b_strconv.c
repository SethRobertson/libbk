#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_strconv.c,v 1.15 2003/12/02 23:17:20 dupuy Exp $";
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
 * String conversion functions
 */

#include <libbk.h>
#include "libbk_internal.h"
#include <limits.h>


#ifndef NAN
#define NAN acosh(0.0)				// 0.0/0.0 causes gcc warnings
#endif

#ifndef INFINITY
#define INFINITY HUGE_VAL			// 1.0/0.0 causes gcc warnings
#endif

#define FLAG_BEGIN  '['
#define FLAG_END    ']'
#define FLAG_APPROX '~'
#define FLAG_SEP    ','



static int bk_string_atou64_int(bk_s B, const char *string, u_int64_t *value, int *sign, bk_flags flags);




/**
 * Convert ascii string to unsigned int32
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state
 *	@param string String to convert
 *	@param value Copy-out of number the string represents
 *	@param flags Fun for the future.
 *	@see bk_string_atou64_int
 *	@return <i>-1</i> on error (string not converted)
 *	@return <br><i>0</i> on success
 *	@return <br><i>positive</i> on non-null terminated number--best effort
 *	number conversion still performed
 */
int bk_string_atou32(bk_s B, const char *string, u_int32_t *value, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int sign = 0;
  u_int64_t tmp;
  int ret = bk_string_atou64_int(B, string, &tmp, &sign, flags);

  if (ret >= 0)
  {
    if (sign < 0 || tmp > UINT32_MAX)
    {
      bk_error_printf(B, BK_ERR_ERR, "%s outside range of u_int32_t\n", string);
      ret = -1;
    }
  }

  if (ret >= 0)
    *value = tmp;

  BK_RETURN(B, ret);
}



/**
 * Convert ascii string to signed int32
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state
 *	@param string String to convert
 *	@param value Copy-out of number the string represents
 *	@param flags Fun for the future.
 *	@see bk_string_atou64_int
 *	@return <i>-1</i> on error (string not converted)
 *	@return <br><i>0</i> on success
 *	@return <br><i>positive</i> on non-null terminated number--best effort
 *	number conversion still performed
 */
int bk_string_atoi32(bk_s B, const char *string, int32_t *value, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int sign = 0;
  u_int64_t tmp;
  int ret = bk_string_atou64_int(B, string, &tmp, &sign, flags);

  if (ret >= 0)
  {
    if (sign == -1 && tmp == -(int64_t)INT32_MIN) // min is -(max + 1)
      sign = 1;					// wrap obviates sign convert
    else if (tmp > INT32_MAX)
    {
      bk_error_printf(B, BK_ERR_ERR, "%s outside range of int32_t\n", string);
      ret = -1;
    }
  }

  if (ret >= 0)
  {
    *value = tmp;
    *value *= sign;
  }

  BK_RETURN(B, ret);
}



/**
 * Convert ascii string to time_t.  This is the same as bk_string_atoi32 for now,
 * but will keep us from casting to int32 all over the place.  We'll be happy
 * about this if this code still exists in 2038.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state
 *	@param string String to convert
 *	@param value Copy-out of number the string represents
 *	@param flags Fun for the future.
 *	@see bk_string_atou64_int
 *	@return <i>-1</i> on error (string not converted)
 *	@return <br><i>0</i> on success
 *	@return <br><i>positive</i> on non-null terminated number--best effort
 *	number conversion still performed
 */
int bk_string_atot(bk_s B, const char *string, time_t *value, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int sign = 0;
  u_int64_t tmp;
  int ret = bk_string_atou64_int(B, string, &tmp, &sign, flags);

  if (ret >= 0)
  {
    if (sign == -1 && tmp == -(int64_t)INT32_MIN) // min is -(max + 1)
      sign = 1;					// wrap obviates sign convert
    else if (tmp > INT32_MAX)
    {
      bk_error_printf(B, BK_ERR_ERR, "%s outside range of int32_t\n", string);
      ret = -1;
    }
  }

  if (ret >= 0)
  {
    *value = tmp;
    *value *= sign;
  }

  BK_RETURN(B, ret);
}



/**
 * Convert ascii string to unsigned int64
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state
 *	@param string String to convert
 *	@param value Copy-out of number the string represents
 *	@param flags Fun for the future.
 *	@see bk_string_atou64_int
 *	@return <i>-1</i> on error (string not converted)
 *	@return <br><i>0</i> on success
 *	@return <br><i>positive</i> on non-null terminated number--best effort
 *	number conversion still performed
 */
int bk_string_atou64(bk_s B, const char *string, u_int64_t *value, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int sign = 0;
  u_int64_t tmp;
  int ret = bk_string_atou64_int(B, string, &tmp, &sign, flags);

  if (sign < 0)					// not a valid unsigned
  {
    bk_error_printf(B, BK_ERR_ERR, "%s outside range of u_int64_t\n", string);
    ret = -1;
  }

  if (ret >= 0)
    *value = tmp;

  BK_RETURN(B, ret);
}



/**
 * Convert ascii string to signed int64
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state
 *	@param string String to convert
 *	@param value Copy-out of number the string represents
 *	@param flags Fun for the future.
 *	@see bk_string_atou64_int
 *	@return <i>-1</i> on error (string not converted)
 *	@return <br><i>0</i> on success
 *	@return <br><i>positive</i> on non-null terminated number--best effort
 *	number conversion still performed
 */
int bk_string_atoi64(bk_s B, const char *string, int64_t *value, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int sign = 0;
  u_int64_t tmp;
  int ret = bk_string_atou64_int(B, string, &tmp, &sign, flags);

  if (ret >= 0)
  {
    if (sign == -1 && (int64_t)tmp == INT64_MIN) // min is -(max + 1)
      sign = 1;					// wrap obviates sign convert
    else if (0 > (int64_t)tmp)
    {
      bk_error_printf(B, BK_ERR_ERR, "%s outside range of int64_t\n", string);
      ret = -1;
    }
  }

  if (ret >= 0)
  {
    *value = tmp;
    *value *= sign;
  }

  BK_RETURN(B, ret);
}



/**
 * Convert ascii string to unsigned int64 with sign extension.  Uses
 * the normal convention of leading "0x" to detect hexadecimal numbers
 * and "0" to detect octal numbers.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state
 *	@param string String to convert
 *	@param value Copy-out of number the string represents
 *	@param sign Copy-out sign of number converted
 *	@param flags Fun for the future.
 *	@see bk_string_atou64_int
 *	@return <i>-1</i> on call failure
 *	@return <br><i>0</i> on success
 *	@return <br><i>positive</i> on non-null terminated number--best effort
 *	number conversion still performed
 */
static int bk_string_atou64_int(bk_s B, const char *string, u_int64_t *value, int *sign, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  signed char decode[256];
  int base;
  u_int tmp;
  u_int64_t val;
  int neg = 1;
  int digits = -1;

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

  // skip over leading space
  while (isspace(*string))
    string++;

  // empty/whitespace string is a best-effort zero
  if (!*string)
  {
    *value = 0;
    *sign = 1;
    BK_RETURN(B, 1);
  }

  // sign determination
  switch(*string)
  {
  case '-':
    neg = -1; string++; break;
  case '+':
    neg = 1; string++; break;
  }

  // base determination
  if (*string == '0')
  {
    switch(*(string+1))
    {
    case 'x':
    case 'X':
      digits = 1;
      base = 16; string += 2; break;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
      digits = 0;
      base = 8; string += 1; break;
    default:
      base = 10; break;
    }
  }
  else
    base = 10;

  val = 0;

  while (*string)
  {
    u_int64_t oldval = val;
    int x = decode[*(unsigned char *)string++];

    // is this the end of the number?
    if (x < 0 || x >= base)
    {
      string--;					// point back at "end" char
      break;
    }

    digits = 0;

    val *= base;
    val += x;

    if (val < oldval)				// cheezy overflow detection
    {
      bk_error_printf(B, BK_ERR_ERR, "%s outside range of u_int64_t\n", string);
      BK_RETURN(B, -1);
    }
  }

  if (digits < 0)
    BK_RETURN(B, -1);				// we saw trash, but no digits

  *sign = neg;
  *value = val;

  // skip over trailing space as well
  while (isspace(*string))
    string++;

  // digits == 1 iff we saw only "0x"
  BK_RETURN(B, digits ? 1 : (*string != '\0'));
}



/**
 * Convert flags to a string.
 *
 * Will use symbolic flags as provided by %b-style names if there is enough
 * room; always appends hex encoding.  If there are not names for all bits set,
 * a tilde '~' will be used to indicate that hex encoding is authoritative, and
 * symbolic names are only comments.  Reverse of @a bk_string_atoflag().
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state
 *	@param src Source flags to convert
 *	@param dst Copy-out string
 *	@param len Length of copy-out buffer
 *	@param names Flag names, encoded as "\1flagbit1\2bittwo\3bitthree" etc.
 *	@param flags Future meta-ness.
 *	@return <i>-1</i> Call failure, not enough room
 *	@return <br><i>0</i> on success
 *	@return <br>Copy-out <i>dst</i>
 */
int bk_string_flagtoa(bk_s B, bk_flags src, char *dst, size_t len, const char *names, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  bk_flags in;
  char *out;
  size_t outlen;
  int anybits = 0;
  int ret;

#define OUT(char) do {*out++ = (char); if (!--outlen) goto justhex;} while (0)

  if (!dst || !len || !src)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, -1);
  }

  if (names)					// try symbolic representation
  {
    int bit;

    in = src;
    out = dst;
    outlen = len;

    while ((bit = *names++) != 0)
    {
      bit = 1 << (bit - 1);

      if (BK_FLAG_ISSET(in, bit))
      {
	int c;

	OUT(anybits ? FLAG_SEP : FLAG_BEGIN);
	anybits = 1;

	for ( ; (c = *names) > 32; names++)
	  OUT(c);

	if (names[-1] <= 32)			// stupid coder wrote "\1\2"
	{
	  bk_error_printf(B, BK_ERR_WARN, "Invalid flag names string %s\n",
			  names);
	  goto justhex;
	}

	BK_FLAG_CLEAR(in, bit);
      }
      else
      {
	for ( ; *names > 32; names++)
	  ;					// skip this (unset) flag
      }
    }

    if (anybits)
    {
      OUT(FLAG_END);
      if (in != 0)				// non-symbolic bits left over
	OUT(FLAG_APPROX);
    }

    in = src;
  }
  else
  {
  justhex:					// can't do symbolic rep
    in = src;
    out = dst;
    outlen = len;
  }

  if ((size_t) snprintf(out, outlen, "0x%x", in) >= outlen)
  {						// not enough room
    if (anybits)
    {
      anybits = 0;
      goto justhex;
    }

    *dst = '\0';				// ensure reasonableness
    ret = -1;
  }
  else
    ret = 0;

  BK_RETURN(B, ret);
}



/**
 * Convert a string to flags.
 *
 * Decodes symbolic flags if present and all flags are provided in names;
 * otherwise performs hex decoding.  Reverse of @a bk_string_flagtoa.  Example
 * valid input strings are "[flagbit1,flagbit2]0x3", "[flagbit1]", "0x6",
 * "[flagbit1]~0x5", and "flagbit1,flagbit2".  In the first two cases, symbolic
 * decoding will be used if possible; in the second two cases, hex decoding
 * will be used; and in the last case, symbolic decoding will be used, with a
 * hard failure (-1 return) if it cannot be completely decoded.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state
 *	@param src Source ascii string to convert
 *	@param dst Copy-out flags
 *	@param names Flag names, encoded as "\1flagbit1\2bittwo\3bitthree" etc.
 *	@param flags Future meta-ness.
 *	@return <i>-1</i> Call failure, not a valid string
 *	@return <br><i>0</i> on success
 *	@return <br><i>positive</i> on non-null terminated flags--best effort
 *	@return <br>Copy-out <i>dst</i>
 */
int bk_string_atoflag(bk_s B, const char *src, bk_flags *dst, const char *names, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  const char *in = src;
  const char *end;
  int ret = 0;

  if (!dst || !src)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, -1);
  }

  if (!names)					// NULL is same as empty string
    names = "";

  if ((end = strrchr(in, FLAG_APPROX)))		// hex is canonical
    goto justhex;

  if ((in[0] == FLAG_BEGIN && (end = strrchr(in, FLAG_END)))
      || (!isdigit(in[0]) && (ret = -1) && (end = &in[strlen(in)])))
  {
    const char *tok;
    const char *sep;
    const char *symbol;
    bk_flags out = 0;

    for (tok = in + 1 + ret; tok < end; tok = sep + 1)
    {
      if (!(sep = strchr(tok, FLAG_SEP)))
	sep = end;

      if (tok == sep)				// empty symbol; bail
      {
	bk_error_printf(B, ret < 0 ? BK_ERR_ERR : BK_ERR_WARN,
			"Flags string \"%s\" has empty symbol\n", src);
	goto badsymbol;
      }

      // not found, or not full match (not preceded and followed by bit/NUL)
      if (!(symbol = bk_strstrn(B, names + 1, tok, sep - tok))
	  || symbol[sep - tok] > 32 || symbol[-1] > 32)
      {
	bk_error_printf(B, ret < 0 ? BK_ERR_ERR : BK_ERR_WARN,
			"Flags string \"%s\" has symbol(s) not in \"%s\"\n",
			src, names);
	goto badsymbol;
      }

      BK_FLAG_SET(out, 1 << (symbol[-1] - 1));
    }

    *dst = out;
    BK_RETURN(B, 0);

  badsymbol:
    if (ret)
      BK_RETURN(B, ret);
  }

 justhex:
  if (end)
  {
    in = end + 1;

    /*
     * If we've jumped to look for hex, and only whitespace (or EOS) remains,
     * force a hard (-1) failure by setting 'in' to point to end terminator.
     */
    if (!in[strspn(in, " \b\t\n\v\f\r")])
      in = end;
  }

  ret = bk_string_atou32(B, in, dst, 0);
  if (ret)
    bk_error_printf(B, ret > 0 ? BK_ERR_WARN : BK_ERR_ERR,
		    "Could not convert flags string \"%s\"\n", src);

  BK_RETURN(B, ret);
}



/**
 * Convert symbolic value to it corresponding textual name.  You must NOT free
 * the return value.
 *
 * Reverse of @a bk_string_atosymbol().
 *
 * THREADS: MT-SAFE (assuming convlist is safe)
 *
 *	@param B BAKA Thread/global state
 *	@param value Source value to convert
 *	@param convlist Symbolic value to name lookup table (null terminated)
 *	@param flags Future meta-ness.
 *	@return <i>NULL</i> Call failure, symbol does not appear in convlist
 *	@return <br><i>const text string</i> on success
 */
const char *bk_string_symboltoa(bk_s B, u_int value, struct bk_symbol *convlist, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!convlist)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  while (convlist && convlist->bs_string)
  {
    if (value == convlist->bs_val)
      BK_RETURN(B, convlist->bs_string);
    convlist++;
  }

  BK_RETURN(B, NULL);
}



/**
 * Convert textual name to a corresponding symbolic value.
 *
 * Reverse of @a bk_string_symboltoa().
 *
 * THREADS: MT-SAFE (assuming convlist is safe)
 *
 *	@param B BAKA Thread/global state
 *	@param name Name to conver to value
 *	@param value Value to copy-out
 *	@param convlist Symbolic value to name lookup table (null terminated)
 *	@param flags BK_STRING_ATOSYMBOL_CASEINSENSITIVE
 *	@return <i>-1</i> Call failure
 *	@return <br><i>0</i> Success!
 *	@return <br><i>1</i> Name did not appear in lookup table
 */
int bk_string_atosymbol(bk_s B, const char *name, u_int *value, struct bk_symbol *convlist, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!convlist || !name || !value)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, -1);
  }

  *value = 0;

  while (convlist && convlist->bs_string)
  {
    int iseq;

    if (BK_FLAG_ISSET(flags, BK_STRING_ATOSYMBOL_CASEINSENSITIVE))
      iseq = strcasecmp(name, convlist->bs_string);
    else
      iseq = strcmp(name, convlist->bs_string);

    if (!iseq)
    {
      *value = convlist->bs_val;
      BK_RETURN(B, 0);
    }

    convlist++;
  }

  BK_RETURN(B, 1);
}



/**
 * Compute the number of textual columns required to print an integer.
 *
 * THREADS: MT-SAFE
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
 * Convert a string to a double in the BAKA way (ie with copyout)
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param string The input string to convert.
 *	@param value The converted value (only valid if return value is 0).
 *	@param flags BK_STRING_ATOD_FLAG_ALLOW_INF to accept infinite values,
 *	BK_STRING_ATOD_FLAG_ALLOW_NAN to accept not-a-number values.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 *	@return <br><i>positive</i> on non-null terminated number--best effort
 */
int
bk_string_atod(bk_s B, const char *string, double *value, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  double tmp;
  char *end = NULL;
  int err;
  int ret = 0;

  if (!string || !value)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  // Set errno, just to be sure.
  errno = 0;
  tmp = strtod(string, &end);
  err = errno;

  // skip trailing whitespace
  while (end && isspace(*end))
    end++;
  // note warning if trailing junk
  if (*end != '\0')
    ret = 1;

  if (tmp == 0.0)
  {
    // check for inf/nan unrecognized by libc strtod()
    if (string == end && !BK_FLAG_ISCLEAR(flags, BK_STRING_ATOF_FLAG_ALLOW_INF|
					  BK_STRING_ATOF_FLAG_ALLOW_NAN))
    {
      double sign = 1.0;

      switch (string[0])
      {
      case '-':
	sign = -1.0;
	/* FALLTHROUGH */
      case '+':
	string++;
      }
      if (BK_FLAG_ISSET(flags, BK_STRING_ATOF_FLAG_ALLOW_INF)
	  && (!strcasecmp(string, "inf") || !strcasecmp(string, "infinity")))
      {
	tmp = sign * INFINITY;
	err = 0;				// prevent error check below
	end = (char *) string + 1;
      }
      else if (BK_FLAG_ISSET(flags, BK_STRING_ATOF_FLAG_ALLOW_NAN)
	       && !strncasecmp(string, "nan", (size_t)3))
      {
	tmp = NAN;
	err = 0;				// prevent error check below
	end = (char *) string + 1;
      }
    }

    // potential error has occured. See strtod man page for explanations
    if (!end || string == end || err == ERANGE)
      ret = -1;
    else
      *value = tmp;
  }
  else if ((tmp == HUGE_VAL || tmp == -HUGE_VAL) && err == ERANGE)
  {
    ret = -1;
  }
  /*
   * The glibc strod recognizes "inf"/"nan" (with any upper/lower combination)
   * but most C libraries will not, however we check for it on all platforms.
   */
  else if ((BK_FLAG_ISCLEAR(flags, BK_STRING_ATOF_FLAG_ALLOW_INF) && isinf(tmp))
	   || (BK_FLAG_ISCLEAR(flags, BK_STRING_ATOF_FLAG_ALLOW_NAN) && isnan(tmp)))
  {
    ret = -1;
  }
  else
  {
    *value = tmp;
  }

  if (ret < 0)
  {
    const char *errmsg;

    if (err == ERANGE)
      errmsg = "%s exceeds range of double precision\n";
    else
      errmsg = "%s is not a valid floating point number\n";

    bk_error_printf(B, BK_ERR_ERR, errmsg, string);
  }

  BK_RETURN(B, ret);
}



/**
 * Convert a string to a floating point in the BAKA way
 *
 * THREADS: MT-SAFE
 *
 * @param B Baka thread/global state
 * @param string The input string to conver
 * @param value The converted value (copyout--only valid if return is 0)
 * @param flags BK_STRING_ATOD_FLAG_ALLOW_INF to accept infinite values,
 *	BK_STRING_ATOD_FLAG_ALLOW_NAN to accept not-a-number values.
 * @return <i>-1</i> on failure.<br>
 * @return <i>0</i> on success.
 * @return <br><i>positive</i> on non-null terminated number--best effort
 */
int
bk_string_atof(bk_s B, const char *string, float *value, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  double tmp;
  int ret = 0;

  if (!string || !value)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if ((ret = bk_string_atod(B, string, &tmp, flags)) < 0)
  {
    // bk_string_atod generated an error already, we can shut up
    ret = -1;
  }
  else if (isinf(tmp) || isnan(tmp))
  {
    // if bk_string_atod returned these, the flags say they are OK
    *value = tmp;
  }
  else if (tmp < -FLT_MAX || tmp > FLT_MAX
	   || (tmp != 0.0 && tmp < FLT_MIN && tmp > -FLT_MIN))
  {
    bk_error_printf(B, BK_ERR_ERR, "%s exceeds range of single precision\n",
		    string);
    ret = -1;
  }
  else
  {
    *value = tmp;
  }

  BK_RETURN(B,ret);
}
