#if !defined(lint) && !defined(__INSIGHT__)
#include "libbk_compiler.h"
UNUSED static const char libbk__rcsid[] = "$Id: b_time.c,v 1.20 2004/07/08 04:40:18 lindauer Exp $";
UNUSED static const char libbk__copyright[] = "Copyright (c) 2003";
UNUSED static const char libbk__contact[] = "<projectbaka@baka.org>";
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
 * All of the support routines for dealing with times.
 */

#include <libbk.h>
#include "libbk_internal.h"


// must adjust if converting to struct timeval
#define RESOLUTION BK_SECSTONSEC(1)
#define LOG10_RES  9
// must adjust if converting to struct timeval
#define BK_MICROSEC   (1000)
// need no adjustment
#define BK_MILLISEC   (BK_MICROSEC*1000)

#define MONTHS	   12



/**
 * Format an ISO date and time specification.
 *
 * Generates ISO extended complete Calendar date format for the struct timespec
 * pointed to by @a timep, and places it in in the character array @a str
 * of size @max.
 *
 * Passing the BK_TIME_FORMAT_FLAG_NO_TZ flag will leave off the 'T' and 'Z'
 * in the ISO format leaving Oracle compatible "ANSI format" timestamps like
 * "2002-06-22 18:46:12.012345".  Note that although no 'Z' timezone designator
 * is present, this timestamp uses GMT/UTC.
 *
 * The output flags BK_TIME_FORMAT_OUTFLAG_TRUNCATED means that the
 * function failed to find enough space.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param str buffer to use on output
 *	@param max size of buffer (including space for NUL)
 *	@param timep struct timespec pointer to use.
 *	@param out_flagsp CO flags.
 *	@param flags Flags (see above)
 *	@return <i>0</i> on failure.<br>
 *	@return number of bytes (not including terminating NUL) on success.
 */
size_t
bk_time_iso_format(bk_s B, char *str, size_t max, const struct timespec *timep, bk_flags *out_flagsp, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  // the size of the array is that of base output "YYYY-mm-ddTHH:MM:SSZ\0" for no apparent reason
  static const char FORMAT_WITH_T[21] = "%Y-%m-%dT%H:%M:%S";
  static const char FORMAT_NO_T[21] = "%Y-%m-%d %H:%M:%S";
  const char *format;
  const char *Z;
  int precision;
  unsigned fraction = 0;
  struct timespec ts;
  struct tm *tp;
  struct tm t;
  size_t len;

  if (!timep || !str)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, 0);
  }

  ts = *timep;

  if (out_flagsp)
    *out_flagsp = 0;

  if (BK_FLAG_ISCLEAR(flags, BK_TIME_FORMAT_FLAG_NO_TZ))
  {
    format = FORMAT_WITH_T;
    Z = "Z";
  }
  else
  {
    format = FORMAT_NO_T;
    Z = "";
  }

  /*
   * <TRICKY>Don't use BK_TS_RECTIFY here; tv_nsec <em>must</em> be
   * non-negative for correct output.</TRICKY>
   */
  if (ts.tv_nsec >= RESOLUTION)
  {
    ts.tv_sec += ts.tv_nsec / RESOLUTION;
    ts.tv_nsec %= RESOLUTION;
  }
  else if (ts.tv_nsec < 0)
  {
    int add = 1 - (ts.tv_nsec / RESOLUTION);
    ts.tv_sec += add;
    ts.tv_nsec += add * RESOLUTION;
  }

  if (ts.tv_nsec == 0)			// omitted entirely
    precision = 0;
  else
  {
    // <BUG ID="862">Guessing precision is stupid since even us/ms/sec boundries do occur </BUG>

    if (ts.tv_nsec % BK_MICROSEC)
    {
      fraction = ts.tv_nsec;
      precision = 9;
    }
    else if (ts.tv_nsec % BK_MILLISEC)
    {
      fraction = ts.tv_nsec / BK_MICROSEC;
      precision = 6;
    }
    else
    {
      fraction = ts.tv_nsec / BK_MILLISEC;
      precision = 3;				// what Java wants to see
    }
  }

  if (sizeof(FORMAT_WITH_T) + 2 + precision > max)	// check space in advance
  {
    if (out_flagsp)
      BK_FLAG_SET(*out_flagsp, BK_TIME_FORMAT_OUTFLAG_TRUNCATED);
    BK_RETURN(B, 0);
  }

  tp = gmtime_r(&ts.tv_sec, &t);

  if (!(len = strftime(str, max - 1 - precision, format, tp)))
  {
    if (out_flagsp)
      BK_FLAG_SET(*out_flagsp, BK_TIME_FORMAT_OUTFLAG_TRUNCATED);
    BK_RETURN(B, 0);
  }

  if (precision)
    len += snprintf(&str[len], max - len, ".%0*u%s", precision, fraction, Z);
  else
  {
    strcat(&str[len], Z);
    len++;
  }

  BK_RETURN(B, len);
}



/**
 * Is this non-fully-qualified (e.g. struct tm) year a leap year?
 *
 * THREADS: MT-SAFE
 *
 * @param y Year number
 * @return <i>1</i> if it is a leap year<br>
 * @return <i>0</i> if it is not a leap year
 */
static int
is_leap(unsigned y)
{
    y += 1900;
    return (y % 4) == 0 && ((y % 100) != 0 || (y % 400) == 0);
}



/**
 * Convert a broken-down time structure, expressed as UTC, to a time_t.
 *
 * This works much like mktime() except that it uses UTC rather than local time
 * for the conversion.  Normalization of struct tm members will only occur if
 * BK_TIMEGM_FLAG_NORMALIZE is passed in flags.  On systems with timegm(), it
 * will will be used if BK_TIMEGM_FLAG_NORMALIZE is passed in flags; if leap
 * second support is enabled in the system time functions it may give slightly
 * different time_t results than if the flag is not set.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param timep broken out struct tm.
 *	@param flags BK_TIMEGM_FLAG_NORMALIZE if normalization is required.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>time_t value</i> on success.
 */
time_t
bk_timegm(bk_s B, struct tm *timep, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  static const unsigned ndays[2][MONTHS] =
    { {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
      {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31} };
  time_t res = 0;
  int year;
  int month;
  int i;

  if (!timep)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

#ifdef HAVE_TIMEGM
  if (BK_FLAG_ISSET(flags, BK_TIMEGM_FLAG_NORMALIZE))
    BK_RETURN(B, timegm(timep));
  else
#endif /* HAVE_TIMEGM */
  {
    // normalize months to avoid long loops, array bounds overflow
    year = timep->tm_year;
    month = timep->tm_mon;
    if (month > MONTHS)
    {
      i = 1 - (month / MONTHS);
      year += i;
      month += i * MONTHS;
    }
    if (month < 0)
    {
      i = 1 - (month / MONTHS);
      year += i;
      month += i * MONTHS;
    }

    /*
     * Don't bother with time before 1970, or after 2105; this not only
     * prevents long ("infinite") loops, it allows us to check for overflow,
     * as unsigned 32 bit time_t wraps in 2106, but signed 32 bit time_t
     * will wrap in 2038.  Of course, this all goes out the window if you
     * set tm_mday to INT_MAX, but whatever.
     */
    if (timep->tm_year < 70 || timep->tm_year > 205)
      BK_RETURN(B, -1);

    for (i = 70; i < timep->tm_year; ++i)
      res += is_leap(i) ? 366 : 365;

    for (i = 0; i < timep->tm_mon; ++i)
      res += ndays[is_leap(timep->tm_year)][i];

    res += timep->tm_mday - 1;
    res *= 24;
    res += timep->tm_hour;
    res *= 60;
    res += timep->tm_min;
    res *= 60;
    res += timep->tm_sec;

    if (res < 0)				// signed time_t overflow
      BK_RETURN(B, -1);
  }

#ifndef HAVE_TIMEGM
  if (BK_FLAG_ISSET(flags, BK_TIMEGM_FLAG_NORMALIZE))
  {
    struct tm *tp;
    struct tm t;
    time_t res2;

    if (!(tp = gmtime_r(&res, &t)))		// run it backwards
      BK_RETURN(B, -1);

    res2 = bk_timegm(B, tp, 0);			// and forwards

    if (res2 != -1 && res2 != res)		// leap seconds!
    {
      res = res2;
      if (!(tp = gmtime_r(&res, &t)))		// run it backwards once more
	BK_RETURN(B, -1);
    }
    *timep = *tp;
  }
#endif /* !HAVE_TIMEGM */

  BK_RETURN(B, res);
}



#ifdef HAVE_STRPTIME
/*
 * Annoyingly, you have to define _XOPEN_SOURCE to get the definition of
 * strptime, but that disables the u_int typedef (among other things), which
 * seems to be used everywhere in libbk.  Just declare it and hope for the
 * best.
 */
extern char *strptime (const char *s, const char *fmt, struct tm *tp);
#else
#ifdef USE_STRPTIME
#undef USE_STRPTIME
#endif
#endif



/**
 * Parses an ISO date and time specification.
 *
 * Only ISO extended complete Calendar date format in UTC/localtime supported.
 *
 * e.g: "yyyy-mm-ddThh:mm:ss[.SSS][Z]" with optional fractional secs and/or Z
 *
 * Use BK_TIME_FORMAT_FLAG_NO_TZ to force GMT/UTC interpretation if no timezone
 * designator is present.
 *
 * <TODO>Add support for timezone +/- offsets from GMT. Also support any ISO
 * variants that strptime can handle, using a similar approach to the Java
 * IsoDateFormat class; essentially, to scan for tokens and build up an
 * strptime format string, then let strptime do the heavy lifting for Ordinal
 * dates, etc..</TODO>
 *
 * THREADS: MT-SAFE (as long as setlocale is not called)
 *
 *	@param B BAKA thread/global state.
 *	@param string ISO format time string to parse.
 *	@param date Copy-out struct timespec pointer for parsed time.
 *	@param flags Flags for the future.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 *	@return <br><i>positive</i> on non-NUL terminated date/time (conversion
 *	still performed).
 *	@return <br>Copy-out <i>date</i>
 */
int
bk_time_iso_parse(bk_s B, const char *string, struct timespec *date, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct tm t;
  const char *format = "%4u-%2u-%2u%c%2u:%2u:%2u%n";
  const char *fraction = NULL;
  size_t precision = 0;
  unsigned long decimal = 0;
  char sep;
  int utc = BK_FLAG_ISSET(flags, BK_TIME_FORMAT_FLAG_NO_TZ);

  if (!string || !date)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  memset(&t, 0, sizeof(t));
#ifdef USE_STRPTIME
  /*
   * This is not the default since it offers no additional functionality, and
   * actually makes it difficult to handle the 'T' vs. ' ' option.
   */
  if (!(fraction = strptime(string, "%Y-%m-%dT%H:%M:%S", &t)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Invalid return when parsing text format: %s\n", string);
      BK_RETURN(B, -1);
    }
#else
  {
    int len = 0;

    if (sscanf(string, format, (u_int *) &t.tm_year, (u_int *) &t.tm_mon,
	       (u_int *) &t.tm_mday, &sep, (u_int *) &t.tm_hour,
	       (u_int *) &t.tm_min, (u_int *) &t.tm_sec, &len) < 7
	|| (sep != 'T' && sep != ' '))
    {
      bk_error_printf(B, BK_ERR_ERR, "Invalid return when parsing text format: %s\n", string);
      BK_RETURN(B, -1);
    }

    t.tm_year -= 1900;
    t.tm_mon -= 1;
    fraction = &string[len];
  }
#endif
  do
  {
    switch (*fraction++)
    {
    case 'Z':
    case 'z':
      utc = 1;
      break;

    case '.':
    case ',':
      if (!precision				// decimal not already seen
	  && *fraction != '-' && *fraction != '+'
	  && !isspace(*fraction))		// strtoul takes these, not us
      {
	char *end;

	decimal = strtoul(fraction, &end, 10);
	precision = end - fraction;

	if (decimal == ULONG_MAX)		// numeric overflow
	  precision = 0;

	if (precision)
	{
	  fraction = end;
	  continue;				// repeat do loop for zone
	}
      }
      // fallthrough - not a number; either empty or junk at end

    default:
      fraction--;				// back up to unknown character
      break;
    }

    break;					// break do loop
  }
  while (1);

#ifndef MKTIME_HAS_BUGS
  if (!utc)
  {
    t.tm_isdst = -1;				// no idea about DST
    date->tv_sec = mktime(&t);
  }
  else						// if mktime buggy, use GMT
#endif
    date->tv_sec = bk_timegm(B, &t, 0);

  if (date->tv_sec == -1)
    BK_RETURN(B, -1);

  if (precision)
  {
    static const int factors[] =
      { 0, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000 };

    if (precision > LOG10_RES)
      while (precision > LOG10_RES)
      {
	int s = (precision - LOG10_RES) % (sizeof(factors)/sizeof(*factors));
	decimal /= factors[s];
	precision -= s;
      }
    else if (precision < LOG10_RES)
      decimal *= factors[LOG10_RES - precision];

    date->tv_nsec = decimal;
  }
  else
    date->tv_nsec = 0;

  BK_RETURN(B, *fraction ? 1 : 0);		// return 1 if last != NUL
}



/**
 * Format an "NTP-style" date and time specification.
 *
 * Generates IDMEF ntpstamp format "0x12345678.0x12345678" for the struct
 * timespec pointed to by @a timep, and places it in in the character array @a
 * str of size @max.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param str buffer to use on output
 *	@param max size of buffer (including space for NUL)
 *	@param timep struct timespec pointer to use.
 *	@param flags Flags for the future.
 *	@return <i>0</i> on failure.<br>
 *	@return number of bytes (not including terminating NUL) on success.
 */
size_t
bk_time_ntp_format(bk_s B, char *str, size_t max, struct timespec *timep, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  static const size_t NTPBUFSIZE = sizeof("0x12345678.0x12345678");

  if (!timep || !str)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, 0);
  }

  if (max < NTPBUFSIZE) // check space in advance
    BK_RETURN(B, 0);

  BK_RETURN(B, sprintf(str, "0x%08lx.0x%08lx",
		       (u_long)CONVERT_SECSTIMESPEC2NTP(timep->tv_sec),
		       (u_long)CONVERT_SUBSECSTIMESPEC2NTP(timep->tv_nsec)));
}



/**
 * Parses an "NTP-style" date and time specification.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param string NTP format time string to parse.
 *	@param date Copy-out struct timespec pointer for parsed time.
 *	@param Flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 *	@return <br><i>positive</i> on non-NUL terminated date/time (conversion
 *	still performed).
 *	@return <br>Copy-out <i>date</i>
 */
int
bk_time_ntp_parse(bk_s B, const char *string, struct timespec *date, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  const char *subsec;
  char *end;
  unsigned secs;
  unsigned frac;

  if (!string || !date)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!(subsec = strchr(string, '.')))
    goto error;

  errno = 0;					// clear for error detection
  secs = strtoul(string, &end, 16);
  if (errno == ERANGE || end != subsec || strncmp(string, "0x", 2))
    goto error;

  subsec++;					// skip past '.'
  frac = strtoul(subsec, &end, 16);
  if (errno == ERANGE || strncmp(subsec, "0x", 2))
    goto error;

  date->tv_sec = CONVERT_SECSNTP2TIMESPEC(secs);
  date->tv_nsec = CONVERT_SUBSECSNTP2TIMESPEC(frac);

  BK_RETURN(B, *end);

 error:
  bk_error_printf(B, BK_ERR_ERR, "Improperly formatted NTP time string\n");
  BK_RETURN(B,-1);
}



/**
 * Parses a duration string in the format [Dd][Hh][Mm][Ss] and returns the 
 * duration in seconds.  The string must not exceed 128 bytes.
 *
 * Example: 4d7h35m is 4 days, 7 hours, and 35 minutes.
 *
 * Durations can also be specified in bare seconds.
 * 1d, 86400s, and 86400 are all valid and equivalent.
 *
 * @param B BAKA Thread/global state
 * @param string string to parse
 * @param time_t duration in seconds (copy-out)
 * @param flags Reserved
 * @return <i>-1</i> on error
 * @return <i>0</i> on success
 */
int
bk_time_duration_parse(bk_s B, const char *string, time_t *duration, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  const size_t maxlen = 128;
  char str[maxlen];
  const char *daystr = NULL;
  const char *hourstr = NULL;
  const char *minstr = NULL;
  const char *secstr = NULL;
  char *end = NULL;
  char *p;
  size_t len = 0;
  uint32_t val;
  int i;

  if (!string || !duration)
  {
    bk_error_printf(B, BK_ERR_ERR, "Internal error: illegal arguments\n");
    BK_RETURN(B, -1);
  }

  *duration = 0;

  if (!(len = strnlen(string, maxlen)))
  {
    goto done;
  }

  if (len >= maxlen)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid duration: must be shorter than %zu characters\n", maxlen);
    goto error;
  }

  // make local copy
  strcpy(str, string);

  // convert to lower case
  for (i=0; str[i]; i++)
  {
    str[i] = tolower(str[i]);
  }

  p = str;

  if (end = strchr(p, 'd'))
  {
    *end = '\0';
    daystr = p;
    p = end + 1;
  }

  if (end = strchr(p, 'h'))
  {
    *end = '\0';
    hourstr = p;
    p = end + 1;
  }

  if (end = strchr(p, 'm'))
  {
    *end = '\0';
    minstr = p;
    p = end + 1;
  }

  if (end = strchr(p, 's'))
  {
    *end = '\0';
    secstr = p;
    p = end + 1;
  }

  // allow bare numbers as seconds
  if (!(daystr || hourstr || minstr || secstr) && *p)
  {
    secstr = p;
  }

  if (daystr)
  {
    if (bk_string_atou32(B, daystr, &val, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Invalid day part of duration string.\n");
      goto error;
    }

    *duration += val * (24 * 60 * 60);
  }

  if (hourstr)
  {
    if (bk_string_atou32(B, hourstr, &val, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Invalid hour part of duration string.\n");
      goto error;
    }

    *duration += val * (60 * 60);
  }

  if (minstr)
  {
    if (bk_string_atou32(B, minstr, &val, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Invalid minute part of duration string.\n");
      goto error;
    }

    *duration += val * (60);
  }

  if (secstr)
  {
    if (bk_string_atou32(B, secstr, &val, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Invalid second part of duration string.\n");
      goto error;
    }

    *duration += val;
  }

 done:
  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}
