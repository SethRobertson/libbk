/*
 *
 *
 * ++Copyright BAKA++
 *
 * Copyright © 2001-2011 The Authors. All rights reserved.
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
 * Public APIs for libbk
 */

#ifndef _LIBBK_h_
#define _LIBBK_h_

// Good to have this before inclusions.
#define LARGEFILE_SOURCE

#include "libbk_autoconf.h"
#include "libbk_include.h"
#include "libbk_oscompat.h"
#include "libbk_inline.h"


/* Forward references */
struct bk_ioh;
struct bk_skid;
struct bk_run;
struct bk_addrgroup;
struct bk_server_info;
struct bk_netinfo;
struct bk_polling_io;
struct bk_ring;
struct bk_stat_list;
struct bk_stat_node;
struct bk_threadlist;
struct bk_threadnode;

// These will be defined by each individual program.
struct global_structure;
extern struct global_structure Global;



#ifdef __INSURE__
#undef SIG_IGN
#define SIG_IGN bk_sig_ign
#endif /* __INSURE__ */




/*
 * This formula is based on zlib manual which states that dst buffer must
 * be at least .01% + 12 bytes greater than the source to permit the
 * compress alg to work. So we add one more byte to make sure that we
 * ceiling the len * 1.001 product.
 */
#define BK_COMPRESS_SWELL(l)	((u_int)((double)(l) * 1.001) + 13)

/*
 * This is irritating. While gcc has absolutely *no* problem with pointers
 * that have "bizzare" values (such as SIG_IGN which is defined as
 * ((__sighandler_t)1)), Insure tends to think these are "WILD" values. So
 * in order to shut it up we define this ptr2uint_t type, based on the
 * native size of a pointer, in order to cast these "WILD" situations to a
 * type that will not alarm Insure.
 */
#if 0
#if SIZEOF_VOID_P == 2
// Yeah, like we're *ever* going to see 16 bit again...
typedef u_int16_t ptr2uint_t;
#elif SIZEOF_VOID_P == 4
typedef u_int32_t ptr2uint_t;
#elif SIZEOF_VOID_P == 8
typedef u_int64_t ptr2uint_t;
#else
#error UNKNOWN SIZE OF POINTER
#endif
#endif


/**
 * Expand identifier or number into string constant form.
 */
#define BK_STRINGIFY(arg) BK_STRINGIFY2(arg)
#define BK_STRINGIFY2(arg) #arg

/**
 * Swap two arbitrary values
 */
#define BK_SWAP(a,b) do { typeof(a) __bk_hold = (a); (a) = (b); (b) = __bk_hold; } while(0)

/**
 * Min/max macros
 */
#define BK_MIN(a,b) ((a) > (b) ? (b) : (a))
#define BK_MAX(a,b) ((a) < (b) ? (b) : (a))


#if defined(__GNUC__) && !defined(__INSURE__)
/**
 * Short-circuit OR for non-boolean values.  Returns first argument if nonzero,
 * else second argument.  (<em>Note:</em> may expand first arg multiple times).
 */
#define BK_OR(a,b) ((a)?:(b))
//#elif defined(__GNUC__)			// insure++ can handle this
//#define BK_OR(a,b) ({ typeof(a) x = (a); x = (x ? x : (b)); })
#else
#define BK_OR(a,b) ((a)?(a):(b))
#endif /* !__GNUC__ */



typedef u_int32_t bk_flags;			///< Normal bitfield type


/* Error levels & their syslog equivalents */
#define BK_ERR_NONE	-1			///< No error logging or level
#define BK_ERR_CRIT	LOG_CRIT		///< Critical error
#define BK_ERR_ERR	LOG_ERR			///< Error
#define BK_ERR_WARN	LOG_WARNING		///< Warning message
#define BK_ERR_NOTICE	LOG_NOTICE		///< Notice about something
#define BK_ERR_DEBUG	LOG_DEBUG		///< Debugging



#define BK_APP_CONF	"/etc/bk.conf"		///< Default configuration file name
#define BK_ENV_GWD(B, e,d)	BK_OR(bk_getenv(B, e),(d)) ///< Get an environmental variable with a default if it does not work
#define BK_GWD(B,k,d) BK_OR(bk_config_getnext(B, NULL, (k), NULL),(d)) ///< Get a value from the config file, or return a default
#define BK_SYSLOG_MAXLEN 1024			///< Length of maximum user message we will syslog
// BK_FLAG_{SET,CLEAR} are statement macros to prevent inadvertent use as tests
#define BK_FLAG_SET(var,bit) do { (var) |= (bit); } while (0) ///< Set a bit in a simple bitfield
#define BK_FLAG_CLEAR(var,bit) do { (var) &= ~(bit); } while (0) ///< Clear a bit in a simple bitfield
#define BK_FLAG_ALLSET(var,bit) (((var) & (bit)) == (bit)) ///< Test if all bits are set in a simple bitfield
#define BK_FLAG_ALLCLEAR(var,bit) (BK_FLAG_ISCLEAR(var,bit)) ///< Included for completeness (or to prove why it's not actually necessary)
#define BK_FLAG_ANYCLEAR(var,bit) (((var) & (bit)) != (bit)) ///< Test if any bit is cleared
#define BK_FLAG_ANYSET(var,bit) (BK_FLAG_ISSET(var,bit)) ///< Included for completeness (or to prove why it's not actually necessary)

#define BK_FLAG_ISSET(var,bit) ((var) & (bit))	///< Test if bit (any of the bits) is set in a simple bitfield
#define BK_FLAG_ISCLEAR(var,bit) (!((var) & (bit))) ///< Test of bit is clear in a simple bitfield
#define BK_STRCMP(a,b) (((a) && (b)) ? strcmp((a),(b)) : ((a) ? 1 : -1)) ///< Strcmp with nulls allowed
#define BK_STREQ(a,b) ((a) && (b) && !strcmp((a),(b))) ///< Are two strings equal
#define BK_STREQN(a,b,n) ((a) && (b) && ((int)n>=0) && !strncmp(a,b,n)) ///< Are two strings equal for the first n characters?
#define BK_STREQCASE(a,b) ((a) && (b) && !strcasecmp((a),(b))) ///< Are two strings equal (ignoring case)
#define BK_STREQNCASE(a,b,n) ((a) && (b) && ((int)n>=0) && !strncasecmp(a,b,n)) ///< Are two strings equal (ignoring case) for the first n characters?
#define BK_INITIAL_STREQ(str, initial_str) ((strlen(str) >= strlen(initial_str)) && (BK_STREQN(str, initial_str, strlen(initial_str))))  ///< Check for a match on the initial part of a string.

#define BK_STRDUP(a) ((a) ? strdup((a)) : NULL) ///< Dup a string if it's not NULL

#define BK_CALLOC(p) BK_CALLOC_LEN(p,sizeof(*(p))) ///< Structure allocation calloc with assignment and type cast
#define BK_CALLOC_LEN(p,l) ((p) = (typeof(p))calloc(1,(l)))	///< Calloc with assignment and type cast
#define BK_MALLOC(p) BK_MALLOC_LEN(p,sizeof(*(p))) ///< Structure allocation malloc with assignment and type cast
#define BK_MALLOC_LEN(p,l) ((p) = (typeof(p))malloc(l))	///< Malloc with assignment and type cast
#define BK_ZERO(p) memset((p),0,sizeof(*p))	///< Memset for the common case
#define BK_BITS_SIZE(b)		(((b)+7)/8)	///< Size of complex bitfield in bytes
#define BK_BITS_BYTENUM(b)	((b)/8)		///< Which byte a particular bit is in
#define BK_BITS_BITNUM(b)	((b)%8)		///< Which bit in a byte has a particular bit
#define BK_BITS_VALUE(B,b)	(((B)[BK_BITS_BYTENUM(b)] & (1 << BK_BITS_BITNUM(b))) >> BK_BITS_BITNUM(b)) ///< Discover truth (0 or 1) value of a particular bit in a complex bitmap
#define BK_BITS_SET(B,b,v)	(B)[BK_BITS_BYTENUM(b)] = (((B)[BK_BITS_BYTENUM(b)] & ~(1 << BK_BITS_BITNUM(b))) | ((v) & 1) << BK_BITS_BITNUM(b)); ///< Set a particular bit in a complex bitmap

#if defined (__INSURE__) || !defined(HAVE_ALLOCA)
#define bk_alloca(l)		(malloc(l))
#define bk_alloca_free(p)	(free(p))
#define bk_strdupa(s)		(strdup(s))
#define bk_strndupa(s)		(strndup(s))
#else
#define bk_alloca(l)		(alloca(l))
#define bk_alloca_free(p)	((void)(0))
#define bk_strdupa(s)		(strdupa(s))
#define bk_strndupa(s)		(strndupa(s))
#endif

#define BK_ALLOCA(p) BK_ALLOCA_LEN(p,sizeof(*(p))) ///< Structure allocation calloc with assignment and type cast
#define BK_ALLOCA_LEN(p,l) ((p) = (typeof(p))bk_alloca(l))	///< Malloc with assignment and type cast

#ifdef __INSURE__
#define BK_NO_MALLOC_WRAP
#ifndef BK_NO_MALLOC_WRAP

#define malloc(s)	(bk_malloc_wrapper(s))
#define calloc(n,s)	(bk_calloc_wrapper(n, s))
#define realloc(b,s)	(bk_realloc_wrapper(b, s))
#define free(b)		(bk_free_wrapper(b))
#define strdup(s)	(bk_strdup_wrapper(s))
#define strndup(s,n)	(bk_strndup_wrapper(s, n))

#else /* BK_NO_MALLOC_WRAP */

/*
 * Insure (at least on linux) doesn't get that realloc(NULL,len) is legal
 * (it complains about freeing NULL), so this macro takes care of that.
 *
 * <WARNING>
 * The pointer arg get evaluatied *twice*. Don't use side effects (which is a bad idea anyay).
 * </WARNING>
 */
#define realloc(p,l) ((p)?realloc((p),(l)):malloc(l))
#endif /* BK_NO_MALLOC_WRAP */
#endif // __INSURE__



/**
 * @name BAKA Version and support routines.
 */
// @{
/**
 * @name bk_version
 *
 * A simple structure to encode versioning numbers. Mostly exists for the
 * macros associated with it.
 */
struct bk_version
{
  bk_flags		bv_flags;		///< Everyone needs flags
  u_int			bv_vers_major;		///< Major version number
  u_int			bv_vers_minor;		///< Minor version number
};



#define BK_VERSION_MAJOR(v) ((v)->bv_vers_major)
#define BK_VERSION_MINOR(v) ((v)->bv_vers_minor)

/**
 * Compare two version strucures (pointers).
 *	@param a First version structure
 *	@param b Second version structure
 *	@return <i>positive<i> if @a is greater than @b <br>
 *	@return <i>0<i> if @a is equal to @b <br>
 *	@return <i>negative<i> if @a is less than @b
 */
#define BK_VERSION_CMP(a,b) BK_OR((a)->bv_vers_major-(b)->bv_vers_major, \
				  (a)->bv_vers_minor-(b)->bv_vers_minor)



/**
 * Compare a version structure with a major number.
 *	@param v The version structure
 *	@param m The major number
 *	@return <i>positive<i> if @v is greater than @m <br>
 *	@return <i>0<i> if @v is equal to @m <br>
 *	@return <i>negative<i> if @v is less than @m
 */
#define BK_VERSION_CMP_MAJOR(v,m) ((a)->bv_vers_major-(m))



/**
 * Check of a version structure is compatible with a major number
 * <em>NB</em> This macro assums that all major numbers are > 1.
 *	@param v The version structure
 *	@param m The major number
 *	@return <i>1<i> if @v is compatible with @m <br>
 *	@return <i>0<i> if @v is not compatible with @m
 */
#define BK_VERSION_COMPAT_MAJOR(v,m) ((v)->bv_vers_major >= 1 && BK_VERSION_CMP_MAJOR((v),(m)) <= 0)
// @}



/**
 * @name Debugging function common interface
 * Virtualize the general bk_debug routines which work on a specific debug queues
 * to use the bk_general debug queue.
 */
// @{
#define BK_DEBUG_KEY "debug"			///< The default key to look up debugging levels in the configuration file
#define BK_DEBUG_DEFAULTLEVEL "*DEFAULT*"	///< The name of the default "function" which will set the debug level of all functions
#define bk_debug_and(B,l) (BK_GENERAL_FLAG_ISDEBUGON(B) && BK_BT_CURFUN(B) && (BK_BT_CURFUN(B)->bf_debuglevel & (l))) ///< Test to see if this function has a particular per-function debugging bit set
#define bk_debug_vprintf_and(B,l,f,ap) (bk_debug_and(B,l)?bk_debug_vprintf(B,f,ap):1) ///< Perform a debugging vprintf if a particular per-function debugging bit is set
#define bk_debug_printf_and(B,l,f,args...) (bk_debug_and(B,l)?bk_debug_printf(B,f,##args):1) ///< Perform a debugging printf if a particular per-function debugging bit is set
#define bk_debug_printbuf_and(B,l,i,p,v) (bk_debug_and(B,l)?bk_debug_printbuf(B,i,p,v):1) ///< Perform a debugging printbuf if a particular per-function debugging bit is set
#define bk_debug_print(B,s) ((BK_GENERAL_FLAG_ISDEBUGON(B) && BK_BT_CURFUN(B))?bk_debug_iprint(B,BK_GENERAL_DEBUG(B),s):1) ///< Perform a debugging print if debugging is enabled
#define bk_debug_printf(B,f,args...) ((BK_GENERAL_FLAG_ISDEBUGON(B) && BK_BT_CURFUN(B))?bk_debug_iprintf(B,BK_GENERAL_DEBUG(B),f,##args):1) ///< Perform a debugging printf if debugging is enabled
#define bk_debug_vprintf(B,f,ap) ((BK_GENERAL_FLAG_ISDEBUGON(B) && BK_BT_CURFUN(B))?bk_debug_ivprintf(B,BK_GENERAL_DEBUG(B),f,ap):1) ///< Perform a debugging vprintf if debugging is enabled
#define bk_debug_printbuf(B,i,p,v) ((BK_GENERAL_FLAG_ISDEBUGON(B) && BK_BT_CURFUN(B))?bk_debug_iprintbuf(B,BK_GENERAL_DEBUG(B),i,p,v):1) ///< Perform a debugging printbuf if debugging is enabled
#define bk_debug_stream(B) ((BK_GENERAL_FLAG_ISDEBUGON(B))?(bk_debug_istream(BK_GENERAL_DEBUG(B))):NULL)
// @}



/**
 * @name Error function common interface
 * Virtualize the general bk_error routines which work on a specific error queues
 * to use the bk_general error queue.
 */
// @{
#define bk_error_print(B,l,s) bk_error_iprint(B,l,BK_GENERAL_ERROR(B),s) ///< Print an error message
#define bk_error_printf(B,l,f,args...) bk_error_iprintf(B,l,BK_GENERAL_ERROR(B),f,##args) ///< Print an error message in printf-style format
#define bk_error_vprintf(B,l,f,ap) bk_error_ivprintf(B,l,BK_GENERAL_ERROR(B),f,ap) ///< Print an error message in printf-style format
#define bk_error_printbuf(B,l,i,p,v) bk_error_iprintbuf(B,l,BK_GENERAL_ERROR(B),i,p,v) ///< Print an error message along with a binary buffer
#define bk_error_dump(B,f,m,M,s,F) bk_error_idump(B,BK_GENERAL_ERROR(B),f,m,M,s,F) ///< Dump the error queues to a certain file handle, syslog level, with various filtering options
#define bk_error_repeater_flush(B,F) bk_error_irepeater_flush(B,BK_GENERAL_ERROR(B),F) ///< Output repeated error messages we've been saving up
#define bk_error_strdump(B,m,M,F) bk_error_istrdump(B,BK_GENERAL_ERROR(B),m,M,F) ///< Dump the error queues to a string (caller must free)
#define bk_error_mark(B,m,F) bk_error_idump(B,BK_GENERAL_ERROR(B),m,F) ///< Mark a particular location in the error queue for future reference
#define bk_error_clear(B,m,F) bk_error_iclear(B,BK_GENERAL_ERROR(B),m,F) ///< Delete a previously inserted mark
#define bk_error_flush(B,m,F) bk_error_iflush(B,BK_GENERAL_ERROR(B),m,F) ///< Flush the error queue, from a mark if desired.
// @}



/**
 * @name bk_general structure, accessors, and flags
 */
// @{
/**
 * General libbk global information shared by all threads.  Do NOT access this
 * structure directly.  Instead use BK_GENERAL macros for OOishness
 */
struct bk_general
{
  struct bk_error	*bg_error;		///< Error info
  struct bk_debug	*bg_debug;		///< Debug info
  struct bk_funlist	*bg_reinit;		///< Reinitialization list
  struct bk_funlist	*bg_destroy;		///< Destruction list
  struct bk_threadlist	*bg_tlist;		///< Thread list
  struct bk_proctitle	*bg_proctitle;		///< Process title info
  struct bk_config	*bg_config;		///< Configuration info
  struct bk_child	*bg_child;		///< Tracked child info
  char			*bg_funstatfile;		///< Filename to output funperfstats
  char			*bg_program;		///< Name of program
  bk_flags		bg_flags;		///< Flags
#define BK_BGFLAGS_FUNON	0x01		///< Is function tracing on?
#define BK_BGFLAGS_DEBUGON	0x02		///< Is debugging on?
#define BK_BGFLAGS_SYSLOGON	0x04		///< Is syslog on?
#define BK_BGFLAGS_THREADON	0x08		///< Is threading on?
#define BK_BGFLAGS_THREADREADY	0x10		///< libbk is thread ready
#ifdef BK_USING_PTHREADS
  pthread_mutex_t	bg_wrmutex;		///< Lock on writing of general structure <TODO>Verify that readers either get old or new value--otherwise this needs to be a rdwr lock and all hell starts breaking loose</TODO>
#endif /* BK_USING_PTHREADS */
};
#define BK_GENERAL_ERROR(B)	(*((B) ? &((B)->bt_general->bg_error): (struct bk_error **)&bk_nullptr)) ///< Access the bk_general error queue
#define BK_GENERAL_DEBUG(B)	(*((B) ? &((B)->bt_general->bg_debug):(struct bk_debug **)&bk_nullptr)) ///< Access the bk_general debug queue
#define BK_GENERAL_CHILD(B)	(*((B) ? &((B)->bt_general->bg_child):(struct bk_child **)&bk_nullptr)) ///< Access the bk_general debug queue
#define BK_GENERAL_REINIT(B)	(*((B) ? &((B)->bt_general->bg_reinit):(struct bk_funlist **)&bk_nullptr)) ///< Access the bk_general reinit list
#define BK_GENERAL_DESTROY(B)	(*((B) ? &((B)->bt_general->bg_destroy):(struct bk_funlist **)&bk_nullptr)) ///< Access the bk_general destruction list
#define BK_GENERAL_TLIST(B)	(*((B) ? &((B)->bt_general->bg_tlist):(struct bk_threadlist **)&bk_nullptr)) ///< Access the bk_general thread list
#define BK_GENERAL_PROCTITLE(B) (*((B) ? &((B)->bt_general->bg_proctitle):(struct bk_proctitle **)&bk_nullptr)) ///< Access the bk_general process title state
#define BK_GENERAL_FUNSTATFILE(B) (*((B) ? &((B)->bt_general->bg_funstatfile):(char **)&bk_nullptr)) ///< Access the bk_general function statistics output filename
#define BK_GENERAL_CONFIG(B)	(*((B) ? &((B)->bt_general->bg_config):(struct bk_config **)&bk_nullptr)) ///< Access the bk_general config info
#define BK_GENERAL_PROGRAM(B)	(*((B) ? &((B)->bt_general->bg_program):(char **)&bk_nullptr)) ///< Access the bk_general program name
#define BK_GENERAL_FLAGS(B)	(*((B) ? &((B)->bt_general->bg_flags):(unsigned *)&bk_zerouint)) ///< Access the bk_general flags
#define BK_GENERAL_WRMUTEX(B)	((B)->bt_general->bg_wrmutex) ///< Access to wrmutex (only if threading is on)
#define BK_GENERAL_FLAG_ISFUNON(B)   BK_FLAG_ISSET(BK_GENERAL_FLAGS(B), BK_BGFLAGS_FUNON) ///< Is function tracing on?
#define BK_GENERAL_FLAG_ISDEBUGON(B)  BK_FLAG_ISSET(BK_GENERAL_FLAGS(B), BK_BGFLAGS_DEBUGON) ///< Is debugging on?
#define BK_GENERAL_FLAG_ISSYSLOGON(B) BK_FLAG_ISSET(BK_GENERAL_FLAGS(B), BK_BGFLAGS_SYSLOGON) ///< Is system logging on?
#define BK_GENERAL_FLAG_ISTHREADON(B) BK_FLAG_ISSET(BK_GENERAL_FLAGS(B), BK_BGFLAGS_THREADON) ///< Is threading on?
#define BK_GENERAL_FLAG_ISTHREADREADY(B) BK_FLAG_ISSET(BK_GENERAL_FLAGS(B), BK_BGFLAGS_THREADREADY) ///< Is threading on?
#define bk_general_thread_create(B, name, start, opaque, flags) bk_thread_create(B, BK_GENERAL_TLIST(B), name, start, opaque, flags)
// @}

#ifdef BK_USING_PTHREADS
#define BK_SIMPLE_LOCK(B, lockp)					\
do {									\
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_lock(lockp) != 0)	\
    abort();								\
} while(0)
#else /* BK_USING_PTHREADS */
#define BK_SIMPLE_LOCK(B, lockp) do {} while(0)
#endif /* BK_USING_PTHREADS */


#ifdef BK_USING_PTHREADS
#define BK_SIMPLE_UNLOCK(B, lockp)						\
do {										\
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_mutex_unlock(lockp) != 0)	\
    abort();									\
} while(0)
#else /* BK_USING_PTHREADS */
#define BK_SIMPLE_UNLOCK(B, lockp) do {} while(0)
#endif /* BK_USING_PTHREADS */


/**
 * @name File locking manifest constants of interest
 */
// @{
#define BK_FILE_LOCK_ADMIN_EXTENSION	"adm"
#define BK_FILE_LOCK_EXTENSION		"lck"
#define BK_FILE_LOCK_MODE_EXCLUSIVE	"EXCLUSIVE"
#define BK_FILE_LOCK_MODE_SHARED	"SHARED"
// @}

/**
 * @name Maximum length of an INET address
 */
// @{
#define BK_MAX_INET_ADDR_LEN            15
// }@



/**
 * Manifest constants need to convert values to/from NTP style dates to unix/timeval style
 */
// @{

#define CONVERT_SECS_NTP2UNIX(secs)	((secs) - 2208988800UL) ///< Convert secs from ntp to unix.
#define CONVERT_SECS_UNIX2NTP(secs)	((secs) + 2208988800UL) ///< Concver secs from unix to ntp.

#define CONVERT_SECSNTP2TIMEVAL(secs)	CONVERT_SECS_NTP2UNIX(secs) ///< Convenience function
#define CONVERT_SECSTIMEVAL2NTP(secs)	CONVERT_SECS_UNIX2NTP(secs) ///< Convenience function
#define CONVERT_SUBSECSNTP2TIMEVAL(subsecs)	((((((u_int64_t)(subsecs))*1000000)/(1<<31))+1)/2)
#define CONVERT_SUBSECSTIMEVAL2NTP(subsecs)	((((((u_int64_t)(subsecs))*(1LL<<32))/500000)+1)/2)

#define CONVERT_SECSNTP2TIMESPEC(secs)	CONVERT_SECS_NTP2UNIX(secs) ///< Convenience function
#define CONVERT_SECSTIMESPEC2NTP(secs)	CONVERT_SECS_UNIX2NTP(secs) ///< Convenience function
#define CONVERT_SUBSECSNTP2TIMESPEC(subsecs)	((((((u_int64_t)(subsecs))*1000000000)/(1<<31))+1)/2)
#define CONVERT_SUBSECSTIMESPEC2NTP(subsecs)	((((((u_int64_t)(subsecs))*(1LL<<32))/500000000)+1)/2)
// @}

// This number (2^39-1) is apparently the current maximum supported value for maxtime, since LONG_MAX is not understood by gmtime & friends yet.  This corresponds to the year 19391 AD.  Good enough for me.
#if SIZEOF_LONG > 4
#define BK_MAX_TIME_T	(LONG_MAX > 549755813887?549755813887:LONG_MAX)
#else
#define BK_MAX_TIME_T	LONG_MAX
#endif



/**
 * A union holding all the different types of sockaddrs we handle. Used mostly for its size.
 */
typedef union
{
  struct sockaddr	bs_sa;
  struct sockaddr_in	bs_sin;
  struct sockaddr_in6	bs_sin6;
  struct sockaddr_un	bs_sun;
} bk_sockaddr_t;



/**
 * @name bk_thread structure, accessors, and flags
 */
// @{
/**
 * Baka general per-thread state information.  Do NOT access this
 * structure directly.  Instead use BK_BT macros for OOishness.
 */
typedef struct __bk_thread
{
  dict_h		bt_funstack;		///< Function stack
  struct bk_funinfo    *bt_curfun;		///< Current function
  const char	       *bt_threadname;		///< Thread name
  struct bk_general    *bt_general;		///< Common program state
  struct bk_stat_list  *bt_funstats;		///< Function performance stats
  bk_flags		bt_flags;		///< Flags for the future
} *bk_s;
#define BK_BT_FUNSTATS(B) (*((B) ? &((B)->bt_funstats):(struct bk_stat_list **)&bk_nullptr)) ///< Access the bk_general function statistics state
#define BK_BT_ISFUNSTATSON(B) (BK_GENERAL_FUNSTATFILE(B) && BK_BT_FUNSTATS(B)) ///< Is fun stats on?
#define BK_BT_FUNSTACK(B)	((B)->bt_funstack)   ///< Access the function stack
#define BK_BT_CURFUN(B)		((B)->bt_curfun)     ///< Access the current function
#define BK_BT_THREADNAME(B)	((B)->bt_threadname) ///< Access the thread name
#define BK_BT_GENERAL(B)	((B)->bt_general)    ///< Access the global shared info
#define BK_BT_FLAGS(B)		((B)->bt_flags)      ///< Access thread-specific flags

#define BK_B_FLAG_SSL_INITIALIZED	0x1	///< Set if ssl_env_init has already been called.
// @}



#define BK_URL_FILE_STR	"file"			///< String to use use for url protocol if it's a bare path

#define BK_AF_LOCAL_STREAM_PROTO_STR	"unixs"	///< Stream AF_LOCAL url marker
#define BK_AF_LOCAL_DGRAM_PROTO_STR	"unixd"	///< Datagram AF_LOCAL url marker


/*
 * We need to establish two numbers to be used as "generic" socket protocol
 * numbers. The requirements are simple: 1) they have to be > 0; and 2)
 * they have to be guarenteed never to be chosen for socket protocol
 * numbers. Choosing two network layer protocol numbers fits the bill quite
 * nicely. So with no particularly reason for these two over any others, we
 * chose IPPROTO_IP and IPPROTO_ICMP.
 */
#define BK_GENERIC_STREAM_PROTO		IPPROTO_IP
#define BK_GENERIC_STREAM_PROTO_STR	"stream"
#define BK_GENERIC_DGRAM_PROTO		IPPROTO_ICMP
#define BK_GENERIC_DGRAM_PROTO_STR	"dgram"



/**
 * Type for on demand functions
 *
 *	@param B BAKA thread/global state
 *	@param run The @a bk_run structure to use.
 *	@param opaque User args passed back.
 *	@param demand The flag which when raised causes this function to run.
 *	@param starttime The start time of the latest invocation of @a bk_run_once.
 *	@param flags Flags for your enjoyment.
 */
typedef int (*bk_run_on_demand_f)(bk_s B, struct bk_run *run, void *opaque, volatile int *demand, const struct timeval *starttime, bk_flags flags);


/**
 * Type for poll/idle functions
 *
 *	@param B BAKA thread/global state
 *	@param run The @a bk_run structure to use.
 *	@param opaque User args passed back.
 *	@param starttime The start time of the latest invocation of @a bk_run_once.
 *	@param delta Copy out of poll interval (NULL for other function types).
 *	@param flags Flags for your enjoyment.
 */
typedef int (*bk_run_f)(bk_s B, struct bk_run *run, void *opaque, const struct timeval *starttime, struct timeval *delta, bk_flags flags);


/**
 * Possible states of gethostbyfoo callback
 */
typedef enum
{
  BkGetHostByFooStateOk=1,			///< You can trust the info
  BkGetHostByFooStateErr,			///< You cannot trust the info
  BkGetHostByFooStateNetinfoErr,		///< You cannot trust bni (but the @a hostent is OK.
} bk_gethostbyfoo_state_e;






/**
 * Information about child states
 */
typedef enum
{
  BkChildStateDead,				///< Child status dead
  BkChildStateStop,				///< Child status stopped
} bk_childstate_e;



/**
 * Information about all tracked children
 */
struct bk_child
{
  dict_h		bc_childidlist;		///< List of children by childid
  dict_h		bc_childpidlist;	///< List of children by childpid
  int			bc_nextchild;		///< Tracking of child ids
#ifdef BK_USING_PTHREADS
  pthread_mutex_t	bc_lock;		///< Lock on child management
#endif /* BK_USING_PTHREADS */
};



/**
 * Information about known/tracked children
 */
struct bk_child_comm
{
  int	cc_id;					///< Identification of child
  pid_t cc_childpid;				///< Process ID of child
  int cc_childcomm[3];				///< FDs to child (maybe)
  void (*cc_callback)(bk_s B, void *opaque, int childid, bk_childstate_e state, u_int status); ///< Callback for child state management
  void *cc_opaque;				///< Opaque data for callback
  u_int cc_statuscode;				///< Last wait status
  bk_flags cc_flags;				///< State
#define CC_WANT_NOTIFYSTOP	0x002		///< Want stop notification
#define CC_DEAD			0x004		///< Child is dead
};



/**
 * Possible status types to iohhandlers.
 */
typedef enum
{
  BkIohStatusNoStatus,				///< bk_ioh notifying user handler of nothing yet
  BkIohStatusIncompleteRead=1,			///< bk_ioh notifying user handler of incomplete read, data provided
  BkIohStatusReadComplete,			///< bk_ioh notifying user handler of a previous write of a buffer has completed, and here is the zero terminated array of buffers, which you must copy if necessary
  BkIohStatusIohReadEOF,			///< bk_ioh notifying user handler of a received EOF
  BkIohStatusWriteAborted,			///< bk_ioh notifying user handler of a previous write of a buffer was not fully completed before abort, and here is the buffer
  BkIohStatusWriteComplete,			///< bk_ioh notifying user handler of a previous write of a buffer has completed, and here is the buffer
  BkIohStatusIohClosing,			///< bk_ioh notifying user handler of IOH in process of close() -- all data flushed/drained at this point
  BkIohStatusIohReadError,			///< bk_ioh notifying user handler of a received IOH error on read
  BkIohStatusIohWriteError,			///< bk_ioh notifying user handler of a received IOH error on write--and here is the buffer
  BkIohStatusIohSeekSuccess,			///< bk_ioh notifying user handler seek has succeeded
  BkIohStatusIohSeekFailed,			///< bk_ioh notifying user handler seek has failed.
} bk_ioh_status_e;

/**
 * gethostbyfoo callback
 *
 *	@param B BAKA thread/global state.
 *	@param run The BAKA run structure.
 *	@param h The @a hostent structure
 *	@param bni @a bk_netinfo which was passed in (may be NULL).
 *	@param args User arguments
 */
typedef void (*bk_gethostbyfoo_callback_f)(bk_s B, struct bk_run *run, struct hostent *h, struct bk_netinfo *bni, void *args, bk_gethostbyfoo_state_e state);



/**
 * @name b_listnum structure and macros
 */
// @{
/**
 * Information about one numbered list
 */
struct bk_listnum_head
{
  void	       *blh_first;		///< First item on list
  void	       *blh_last;		///< Last item on list
  u_int		blh_num;		///< Number of this list (of the beast?)
};


#define BK_LISTNUM_INSERT(head,node,nextname,prevname) do { ((node)->nextname) = (head)->blh_first; ((node)->prevname) = (void *)(head); ((node)->nextname->prevname) = (node); (head)->blh_first = (node); } while (0)
#define BK_LISTNUM_APPEND(head,node,nextname,prevname) do { ((node)->nextname) = (void *)(head); ((node)->prevname) = (head)->blh_last; ((node)->prevname->nextname) = (node); (head)->blh_last = (node); } while (0)
#define BK_LISTNUM_DELETE(node,nextname,prevname) do { ((node)->nextname->prevname) = ((node)->prevname); ((node)->prevname->nextname) = ((node)->nextname); ((node)->nextname) = NULL; ((node)->prevname) = NULL; } while (0)
// @}



/**
 * @name Lock types
 * The types of locks you may acquire with bk file locking
 */
typedef enum
{
  BkFileLockTypeShared=0,			///< Shared lock.
  BkFileLockTypeExclusive,			///< Exclusive lock.
} bk_file_lock_type_e;



/**
 * Name <=> value map.
 *
 * <WARNING>Storing negative values (or UINT_MAX) in map doesn't work as it may
 * be confused with -1 failure return from @a bk_nvmap_name2value.</WARNING>
 */
struct bk_name_value_map
{
  const char *		bnvm_name;		///< The name of pair
  int			bnvm_val;		///< The value of the pair
};



/**
 * Bitfield
 */
struct bk_bitfield
{
  /*
   * <TODO> using a 32-bit int for the bits is handy, but introduces endian
   * problems when you use sfv_raw_vptr to access it; also, we can't have a
   * bitstring of > 32 bits.  This should be fixed eventually.</TODO>
   */
  u_int32_t		bf_bits;		///< Bitfield value
  u_int8_t		bf_bitlen;		///< Bitfield bit length
};



/**
 * Time
 */
struct bk_timespec
{
  time_t		bt_secs;		///< Seconds since epoch
  u_int32_t		bt_nsecs;		///< Nanoseconds since second
};



/**
 * The possible "sides" of a socket
 */
typedef enum
{
  BkSocketSideLocal=1,			///< The local side.
  BkSocketSideRemote,			///< The remote side.
} bk_socket_side_e;



/**
 * 16-bit representation of a number between 0.0 and 1.0,
 * with conversion routines.
 */
typedef u_int16_t bk_pct16_t;
#define BK_FLOAT_TO_PCT16(f) ((bk_pct16_t) (f * 0xffff))
#define BK_PCT16_TO_FLOAT(p) (p / (float) 0xffff)



/**
 * Actions permitted in modifying fd flags
 */
typedef enum
{
  BkFileutilsModifyFdFlagsActionAdd=1,
  BkFileutilsModifyFdFlagsActionDelete,
  BkFileutilsModifyFdFlagsActionSet,
} bk_fileutils_modify_fd_flags_action_e;



/**
 * Enum list of known network address types.
 */
typedef enum
{
  BkNetinfoTypeUnknown=0,			///< Special "unset" marker
  BkNetinfoTypeInet,				///< IPv4 address
  BkNetinfoTypeInet6,				///< IPv6 address
  BkNetinfoTypeLocal,				///< AF_LOCAL/AF_UNIX address
  BkNetinfoTypeEther,				///< Ethernet address
} bk_netaddr_type_e;


#define BK_NETINFO_TYPE_NONE BK_NETINFO_TYPE_UNKNOWN ///< Alias for BK_NETINFO_TYPE_UNKNOWN

/**
 * Results from an asynchronous connection (these are mostly geared towars
 * the client side.
 */
typedef enum
{
  BkAddrGroupStateSysError=1,			///< An system error
  BkAddrGroupStateRemoteError,			///< A "remote" network error
  BkAddrGroupStateLocalError,			///< An "local" network error
  BkAddrGroupStateTimeout,			///< A timeout has occured
  BkAddrGroupStateSocket,			///< New socket--allow callee to muck with sockopts
  BkAddrGroupStateReady,			///< TCP listener or unconnected UDP ready
  BkAddrGroupStateConnected,			///< TCP/UDP/AF_LOCAL connected
  BkAddrGroupStateClosing,			///< State is closing
} bk_addrgroup_state_e;



/**
 * File descriptor handler type.
 */
typedef void (*bk_fd_handler_t)(bk_s B, struct bk_run *run, int fd, u_int gottypes, void *opaque, const struct timeval *starttime);

/**
 * @name bk_addrgroup structure.
 */
// @{
/**
 * Callback when connections complete/abort and server listens
 * complete/abort. NB: Choosing to store the @a server_handle means you
 * accept the responsibility of noting if and when it dies. Should
 * something go wrong with the server to which it refers, you will receive
 * a BK_ADDRGROUP_STATE_CLOSING for that socket (ie the server socket, not
 * any connected sockets you might be currently handling). The
 * server_handle is still valid at this point, but from the moment you
 * return it is <em>no longer usuable</em> (its underlying memory will be
 * freed). Do not reuse. Indeed about the only thin you probably want to do
 * when you get this noticification is to NULL out your stored version of
 * the handle (and any local cleanup associated with this). Please be aware
 * too that if you call bk_addrgroup_server_close(), you will still get the
 * closing notification.
 *
 * Checking the @a bag is required by the API. In the BkAddrGroupClosing
 * state, it is <em>guarenteed</em> to be NULL, but the API allows for it
 * to be NULL in any state (though currently this cannot happen)
 *
 *	@param B BAKA thread/global state.
 *	@param args User args
 *	@param sock The new socket.
 *	@param bag The address group pair (if you requested it).
 *	@param server_handle The handle for referencing the server (accepting connections only).
 *	@param state State as described by @a bk_addrgroup_state_e.
 *
 */
typedef int (*bk_bag_callback_f)(bk_s B, void *args, int sock, struct bk_addrgroup *bag, void *server_handle, bk_addrgroup_state_e state);

/**
 * Structure which describes a network "association". This name is slightly
 * bogus owing to the fact that server listens (which aren't tehcnically
 * associations) and unconnected udp use this structure too.
 *
 * <HACK degree="good-lord">
 * The bag_bs_desatroy argument is a totally abusive. We need to be able to
 * free the SSL state when we destroy the bag, but we don't want to create
 * a dependency on b_ssl.c because of compilations reasons. So when we need
 * destruction we fill out this function pointer (with bk_ssl_destroy())
 * and call that instead. It allows for bk_sll_destroy() to be called
 * without alerting the compiler.
 *
 * Fortunately it's unclear that this ever gets called and if so it's only
 * under exceptional conditions. It is important, however, to make sure
 * that any future added calss to bk_bag_destroy() do the Right Thing.
 *
 * </HACK>
 */
struct bk_addrgroup
{
  bk_flags		bag_flags;		///< Everyone needs flags */
  /* XXX timeout probably belongs in local address_state structure */
  struct bk_netinfo *	bag_local;		///< Local side information */
  struct bk_netinfo *	bag_remote;		///< Remote side information */
  int			bag_proto;		///< Cached proto */
  bk_netaddr_type_e	bag_type;		///< Cached address family */
  struct bk_ssl	    *	bag_ssl;		///< SSL state for connection
  void			(*bag_ssl_destroy)(bk_s B, struct bk_ssl *ssl, bk_flags flags); // Hack to allow bk_ssl_destroy to be called without creating a compilation dependency.
  int32_t		bag_refcount;		///< Don't free until this refcount is zero.
};


#define BK_ADDRGROUP_FLAGS(bag) ((bag)->bag_flags)
#define BK_ADDRGROUP_TIMEOUT(bag) ((bag)->bag_timeout)
#define BK_ADDRGROUP_LOCAL_NETINFO(bag) ((bag)->bag_local)
#define BK_ADDRGROUP_REMOTE_NETINFO(bag) ((bag)->bag_remote)
#define BK_ADDRGROUP_CALLBACK(bag) ((bag)->bag_callback)
#define BK_ADDRGROUP_SSL(bag) ((bag)->bag_ssl)
#define BK_ADDRGROUP_ARGS(bag) ((bag)->bag_args)


#define BK_ADDRGROUP_FLAG_DIVIDE_TIMEOUT	0x1 ///< Timeout should be divided among all addresses

// @}

/**
 * @name Function tracing common interfacce
 * Virtualize the bk_fun routines which work on a specific function
 * stack to use the per-thread function stack available through (B)
 */
// @{
/**
 * @brief Re-enter the current function after the function stack has been
 * initialized
 */
#define bk_fun_reentry(B) bk_fun_reentry_i(B, __bk_funinfo)

/**
 * @brief Normal method to let function tracing know you have entered
 * function--if function tracing is enabled
 */
#define BK_ENTRY(B, fun, pkg, grp) struct bk_funinfo *__bk_funinfo = (!B || !BK_GENERAL_FLAG_ISFUNON(B)?NULL:bk_fun_entry(B, fun, pkg, grp))
#define BK_ENTRY_MAIN(B, fun, pkg, grp) struct bk_funinfo *__bk_funinfo = bk_fun_entry(B, fun, pkg, grp)



#ifdef __GNUC__
/*
 * BK_ENTRY_VOLATILE is occasionally needed to prevent compiler warnings
 * (errors) about __bk_funinfo being clobbered by longjmp or vfork (or
 * pthread_create).  <TRICKY>A special macro is needed because the __bk_funinfo
 * pointer variable *itself* (not the struct bk_funinfo it points to) must be
 * declared volatile - use cdecl to "explain" the crazy C syntax for this.
 *
 * Seth had been using a "stupid" hack to force gcc to keep the variable on the
 * stack, but the gcc-4 optimizer is too smart and optimizes the hack away, so
 * we must use the volatile declaration correctly.</TRICKY>
 */
#define BK_ENTRY_VOLATILE(B, fun, pkg, grp) struct bk_funinfo *volatile __bk_funinfo = (!B || !BK_GENERAL_FLAG_ISFUNON(B)?NULL:bk_fun_entry(B, fun, pkg, grp))



/**
 * @brief Return a value, letting function tracing know you are exiting the
 * function preserving the order of return value function stack evaluation
 * (e.g. if return value is actually a function which must be evaluated0.
 */
#define BK_RETURN(B, retval)			\
do {						\
  typeof(retval) myretval = (retval);		\
  if ((B) && BK_GENERAL_FLAG_ISFUNON(B))	\
    bk_fun_exit((B), __bk_funinfo);		\
  return myretval;				\
  /* NOTREACHED */				\
} while (0)

#else  /* !__GNUC__ */

// no need for volatile declarations - may not even compile
#define BK_ENTRY_VOLATILE(B, fun, pkg, grp) BK_ENTRY(B, fun, pkg, grp)

// order of function stack *will* be messed up, tough baka
#define BK_RETURN(B, retval)			\
do {						\
  if ((B) && BK_GENERAL_FLAG_ISFUNON(B))	\
    bk_fun_exit((B), __bk_funinfo);		\
  return retval;				\
  /* NOTREACHED */				\
} while (0)

#endif /* !__GNUC__ */



/// @brief Obsolete version of BK_RETURN()--use that instead.
#define BK_ORETURN(B, retval) BK_RETURN(B, retval)



/**
 * @brief Let function tracing know you are returning from this function, and
 * return void
 */
#define BK_VRETURN(B)				\
do {						\
  if ((B) && BK_GENERAL_FLAG_ISFUNON(B))	\
    bk_fun_exit((B), __bk_funinfo);		\
  return;					\
  /* NOTREACHED */				\
} while (0)
// @}



/**
 * @name Basic Timeval Operations
 * Perform basic timeval operations (add, subtract, compare, etc).
 */
// @{
/** @brief Add two timevals together--answer may be an argument */
#define BK_TV_ADD(sum,a,b)				\
    do							\
    {							\
      (sum)->tv_sec = (a)->tv_sec + (b)->tv_sec;	\
      (sum)->tv_usec = (a)->tv_usec + (b)->tv_usec;	\
      BK_TV_RECTIFY(sum);				\
    } while (0)
/** @brief Subtract timeval a from b--answer may be an argument */
#define BK_TV_SUB(sum,a,b)				\
    do							\
    {							\
      (sum)->tv_sec = (a)->tv_sec - (b)->tv_sec;	\
      (sum)->tv_usec = (a)->tv_usec - (b)->tv_usec;	\
      BK_TV_RECTIFY(sum);				\
    } while (0)
/** @brief Compare two timevals, return trinary value around zero */
#define BK_TV_CMP(a,b) BK_OR((a)->tv_sec-(b)->tv_sec,(a)->tv_usec-(b)->tv_usec)
/** @brief Convert a timeval to a float. */
#define BK_TV2F(tv) ((double)(((double)((tv)->tv_sec)) + ((double)((tv)->tv_usec))/1000000.0))
/** @brief Rectify timeval so that usec value is within range of second, and that usec value has same sign as sec.  Performed automatically on all BK_TV operations. */
#define BK_TV_RECTIFY(sum)					\
    do								\
    {								\
      if ((sum)->tv_usec >= BK_SECSTOUSEC(1))			\
      {								\
	(sum)->tv_sec += (sum)->tv_usec / BK_SECSTOUSEC(1);	\
	(sum)->tv_usec %= BK_SECSTOUSEC(1);			\
      }								\
								\
      if ((sum)->tv_usec <= -BK_SECSTOUSEC(1))			\
      {								\
	(sum)->tv_sec += (sum)->tv_usec / BK_SECSTOUSEC(1);	\
	(sum)->tv_usec %= -BK_SECSTOUSEC(1);			\
      }								\
								\
      if ((sum)->tv_usec < 0 && (sum)->tv_sec > 0)		\
      {								\
	(sum)->tv_sec--;					\
	(sum)->tv_usec += BK_SECSTOUSEC(1);			\
      }								\
								\
      if ((sum)->tv_usec > 0 && (sum)->tv_sec < 0)		\
      {								\
	(sum)->tv_sec++;					\
	(sum)->tv_usec -= BK_SECSTOUSEC(1);			\
      }								\
								\
    } while (0)
#define BK_SECS_TO_EVENT(x) ((x)*1000)		///< Convert seconds to event granularity
#define BK_SECSTOUSEC(x) ((x)*1000000)		///< Convert seconds to microseconds
// @}


/**
 * @name Basic Timespec Operations
 * Perform basic timespec operations (add, subtract, compare, etc).
 */
// @{
/** @brief Add two timespecs together--answer may be an argument */
#define BK_TS_ADD(sum,a,b)				\
    do							\
    {							\
      (sum)->tv_sec = (a)->tv_sec + (b)->tv_sec;	\
      (sum)->tv_nsec = (a)->tv_nsec + (b)->tv_nsec;	\
      BK_TS_RECTIFY(sum);				\
    } while (0)
/** @brief Subtract timespec a from b--answer may be an argument */
#define BK_TS_SUB(sum,a,b)				\
    do							\
    {							\
      (sum)->tv_sec = (a)->tv_sec - (b)->tv_sec;	\
      (sum)->tv_nsec = (a)->tv_nsec - (b)->tv_nsec;	\
      BK_TS_RECTIFY(sum);				\
    } while (0)
/** @brief Compare two timespecs, return trinary value around zero */
static inline int BK_TS_CMP(struct timespec *a, struct timespec *b)
{
  if (a->tv_sec < b->tv_sec)
    return(-1);
  else if (a->tv_sec > b->tv_sec)
    return(1);
  else if (a->tv_nsec < b->tv_nsec)
    return(-1);
  else if (a->tv_nsec > b->tv_nsec)
    return(1);
  else
    return(0);
}

/** @brief Convert a timespec to a float. */
#define BK_TS2F(tv) ((double)(((double)((tv)->tv_sec)) + ((double)((tv)->tv_nsec))/1000000000.0))
/** @brief Rectify timespec so that nsec value is within range of second, and that nsec value has same sign as sec.  Performed automatically on all BK_TS operations. */
#define BK_TS_RECTIFY(sum)					\
    do								\
    {								\
      if ((sum)->tv_nsec >= BK_SECSTONSEC(1))			\
      {								\
	(sum)->tv_sec += (sum)->tv_nsec / BK_SECSTONSEC(1);	\
	(sum)->tv_nsec %= BK_SECSTONSEC(1);			\
      }								\
								\
      if ((sum)->tv_nsec <= -BK_SECSTONSEC(1))			\
      {								\
	(sum)->tv_sec += (sum)->tv_nsec / BK_SECSTONSEC(1);	\
	(sum)->tv_nsec %= -BK_SECSTONSEC(1);			\
      }								\
								\
      if ((sum)->tv_nsec < 0 && (sum)->tv_sec > 0)		\
      {								\
	(sum)->tv_sec--;					\
	(sum)->tv_nsec += BK_SECSTONSEC(1);			\
      }								\
								\
      if ((sum)->tv_nsec > 0 && (sum)->tv_sec < 0)		\
      {								\
	(sum)->tv_sec++;					\
	(sum)->tv_nsec -= BK_SECSTONSEC(1);			\
      }								\
								\
    } while (0)

#define BK_SECSTONSEC(x) ((x) * 1000000000)	///< Convert seconds to nanoseconds

#define BK_USECTONSEC(x) ((x) * 1000)		///< Convert micro to nano.

// <TRICKY>This rounds to nearest microsecond rather than truncating</TRICKY>
#define BK_NSECTOUSEC(x) ((x) + 500) / 1000)	///< Convert nano to micro
// @}

/*
 * variant of BK_TS2F for "standard types not as good as my types" baka version
 * (the extra 's' at the end of the field names is for "special")
 */
#define BK_BTS2F(bt) ((double)(((double)((bt)->bt_secs)) + ((double)((bt)->bt_nsecs))/1000000000.0))
// And similarly for comparisons
static inline int BK_BTS_CMP(struct bk_timespec *a, struct bk_timespec *b)
{
  if (a->bt_secs < b->bt_secs)
    return(-1);
  else if (a->bt_secs > b->bt_secs)
    return(1);
  else if (a->bt_nsecs < b->bt_nsecs)
    return(-1);
  else if (a->bt_nsecs > b->bt_nsecs)
    return(1);
  else
    return(0);
}



/*
 * Murmurhash "functions" -- see http://murmurhash.googlepages.com
 *
 * Copyright: None -- contributed to the public domain
 *
 * Our version of this algorithm is not test vector compatible
 * due to our treatment of length (only allret hashes the length
 * explicitly and it does it in a different place to save a mix
 * operation).  However, their example code is not vector compatible
 * with the example incremental version so I am not concerned.
 */
#define MURMURHASH2A_R 24
#define MURMURHASH2A_MULTIPLIER 0x5bd1e995
#define MURMURHASH2A_SEED 2166136261		// No seed suggested, so using FNV32_OFFSET_BASIS randomly



/**
 * Initialize u_int32_t hash variable to ``random'' value.  You can initialize to other values if desired
 *
 * h - variable which will be modified to contain new seed
 */
#define murmurhash2a_init(h) do { h = MURMURHASH2A_SEED; } while (0)



/**
 * Update the hash with a 32 bit word's worth of new hashable data
 *
 * h - variable which will be modified due to hashing
 * word - the data to update h with
 */
#define murmurhash2a_update(h,word)			\
do {							\
  u_int mmh2ak = (word) * MURMURHASH2A_MULTIPLIER;	\
  mmh2ak ^= mmh2ak >> MURMURHASH2A_R;			\
  mmh2ak *= MURMURHASH2A_MULTIPLIER;			\
  h *= MURMURHASH2A_MULTIPLIER;				\
  h ^= mmh2ak;						\
 } while (0)



/**
 * Update the hash with a buffer of data
 *
 * h - variable which will be modified due to hashing
 * ptr - pointer to data to update h with
 * len - length of data in ptr to update h with
 */
#define murmurhash2a_updatebuf(h,ptr,len)	\
do {						\
  u_int mmh2al = (len);				\
  u_char *mmh2ad = (u_char *)(ptr);		\
  u_int mmh2akk = mmh2al;			\
  for(;mmh2al >= 4; mmh2ad += 4,mmh2al -= 4)	\
  {						\
    murmurhash2a_update(h,*(u_int *)mmh2ad);	\
  }						\
  switch(mmh2al)				\
  {						\
  case 3: mmh2akk ^= mmh2ad[2] << 16;		\
  case 2: mmh2akk ^= mmh2ad[1] << 8;		\
  case 1: mmh2akk ^= mmh2ad[0];			\
  }						\
  murmurhash2a_update(h,mmh2akk);		\
 } while (0)



/**
 * Perform final avalanch and otherwise ready h for usage
 *
 * h - 32bit hash variable which will be modified and readied for use
 */
#define murmurhash2a_final(h)			\
do {						\
  h ^= h >> 13;					\
  h *= MURMURHASH2A_MULTIPLIER;			\
  h ^= h >> 15;					\
 } while (0)



/**
 * Return a u_int32_t hash of a buffer
 *
 * ptr - pointer to data to be hashed
 * len - length of data to be hash
 */
#define murmurhash2a_allret(ptr,len)		\
({						\
  u_int mmh2ah;					\
  murmurhash2a_init(mmh2ah);			\
  murmurhash2a_updatebuf(mmh2ah,ptr,len);	\
  murmurhash2a_final(mmh2ah);			\
  mmh2ah;					\
 })



/**
 * Perform final mixing operation as a rvalue
 * Can be used to better distribute some non-murmurhash algorithm
 *
 * hh - hash value to analanch and finalize
 */
#define murmurhash2a_mixret(hh) ({ u_int h = (hh); murmurhash2a_final(h); h; })



/** @brief Special symbol which means resolve to "any" address. */
#define BK_ADDR_ANY "bk_addr_any"

/**
 * General vectored pointer
 */
typedef struct bk_vptr
{
  void *ptr;					///< Data
  u_int32_t len;				///< Length
} bk_vptr;


/**
 * Pointer with current length and maximum length
 */
typedef struct bk_alloc_ptr
{
  void *ptr;					///< Data
  u_int32_t cur;				///< Current length
  u_int32_t max;				///< Maximum length
} bk_alloc_ptr;


/**
 * Vectored string for more efficient appending
 */
typedef struct bk_vstr
{
  char *ptr;					///< Data
  u_int32_t cur;				///< Current length (not including NULL)
  u_int32_t max;				///< Allocated length (including NULL)
} bk_vstr;


/**
 * A structure for communicating configuration file configuration
 * parameters to the config subsystem during baka general
 * initialization.  Typically, this will be NULL and not used.
 */
struct bk_config_user_pref
{
  char *	bcup_include_tag;		///< Include file tag
  char *	bcup_separators;		///< key/value separator chars
  char *	bcup_commentchars;		///< comment chars
  bk_flags	bcup_flags;			///< Everyone needs flags
};



/**
 * The information stored about every function on the function stack during
 * function tracing.
 */
struct bk_funinfo
{
  const char *bf_funname;			///< Function name
  const char *bf_pkgname;			///< Package name
  const char *bf_grpname;			///< Group name
  u_int32_t bf_debuglevel;			///< Per-function debug level
  struct timeval bf_starttime;			///< If function stats on...
};



/**
 * @a bk_servinfo struct.
 *
 * Pretty much the same thing as a @a servent, but wth a few @a
 * BAKAisms.
 */
struct bk_servinfo
{
  bk_flags		bsi_flags;		///< Everyone needs flags
  u_int			bsi_port;		///< Port (network order)
  char *		bsi_servstr;		///< Service string
  /*
   * XXX Protostr removed 'cause Seth&Alex said too. Jtt thinks it belongs
   * as this is not no lonter a stand-alone structure, but I suppose you
   * can always use a real servent if you need to.
   */
};



/**
 * @a servinfo struct. Pretty much the same thing as a @a servent, but wth
 * a fiew @a BAKAisms. For instance the protocol is stored as a pointer to
 * a @bk_protoent, but since proto is often implied by context, this value
 * may indeed be NULL in many cases
 */
struct bk_protoinfo
{
  bk_flags		bpi_flags;		///< Everyone needs flags
  int			bpi_proto;		///< Protocol number
  char *		bpi_protostr;		///< Protocol string
};



/**
 * Everything you ever wanted to know about a network address.
 */
struct bk_netaddr
{
  bk_flags		bna_flags;		///< Everyone needs flags
  bk_netaddr_type_e	bna_type;		///< Type of address
  u_int			bna_len;		///< Length of address
  union
  {
    struct in_addr	bnaa_inet;		///< IPv4 address
    struct in6_addr	bnaa_inet6;		///< IPv6 address
    char *		bnaa_path;		///< AF_LOCAL/AF_UNIX address
    struct ether_addr	bnaa_ether;		///< Ethernet address
  } bna_addr;
  char *		bna_pretty;		///< Printable form of addr
  dict_h		bna_netinfo_addrs;	///< Back pointer to prevent double free
};


#define bna_inet	bna_addr.bnaa_inet
#define bna_inet6	bna_addr.bnaa_inet6
#define bna_path	bna_addr.bnaa_path
#define bna_ether	bna_addr.bnaa_ether

/**
 * Everything you ever wanted to know about networks (well not routing info)
 */
struct bk_netinfo
{
  bk_flags		bni_flags;		///< Everyone needs flags
  struct bk_netaddr *	bni_addr;		///< Primary address -- pointer into addrs CLC
  struct bk_netaddr *	bni_addr2;		///< Secondary address -- pointer into addrs CLC
  dict_h		bni_addrs;		///< dll of addrs
  struct bk_servinfo *	bni_bsi;		///< Service info
  struct bk_protoinfo *	bni_bpi;		///< Protocol info
  char *		bni_pretty;		///< Printable forms
};

#define BK_NETINFO_ADDR_TYPE(bni)	(((bni)->bni_addr && (bni)->bni_addr->bna_type) || ((bni)->bni_addr2 && (bni)->bni_addr2->bna_type) || BkNetinfoTypeUnknown)



/**
 * On Solaris and BSD, the IN6_IS_ADDR_MULTICAST macro takes a pointer to
 * a struct in6_addr.  This is useful since each operating system has
 * different names for the members of a struct in6_addr.  Unfortunately,
 * Linux decided to expose its internals and have IN6_IS_ADDR_MULTICAST
 * take as input a member from the structure.  Feh.
 */
#ifdef IN6_MULTICAST_TAKES_S6_ADDR // Linux ipv6 implementation
#define BK_IN6_IS_ADDR_MULTICAST(a) IN6_IS_ADDR_MULTICAST(a.s6_addr)
#else
#ifdef IN6_MULTICAST_TAKES_IN6_ADDR // Sane ipv6 implementation
#define BK_IN6_IS_ADDR_MULTICAST(a) IN6_IS_ADDR_MULTICAST(&(a))
#endif // IN6_MULTICAST_TAKES_IN6_ADDR
#endif // IN6_MULTICAST_TAKES_S6_ADDR

#define BK_IN_MULTICAST(ag) \
    (((ag)->bna_type == BkNetinfoTypeInet && IN_MULTICAST(ntohl((ag)->bna_inet.s_addr))) || \
     ((ag)->bna_type == BkNetinfoTypeInet6 && BK_IN6_IS_ADDR_MULTICAST((ag)->bna_inet6)))



/**
 * Atomic counter -- an integer with a lock for protection.
 *
 * Access only through bk_atomic_addition for safety/sanity.
 */
struct bk_atomic_cntr
{
#ifdef BK_USING_PTHREADS
  pthread_mutex_t	bac_lock;		///< Lock to make this atomic
#endif /* BK_USING_PTHREADS */
  int			bac_cntr;		///< Counter that we protect
};




/**
 * @name Defines: netinfo_addrs_clc
 * list of addresses within a @a struct @bk_netinfo.
 * which hides CLC choice.
 */
// @{
#define netinfo_addrs_create(o,k,f)		dll_create((o),(k),(f))
#define netinfo_addrs_destroy(h)		dll_destroy(h)
#define netinfo_addrs_insert(h,o)		dll_insert((h),(o))
#define netinfo_addrs_insert_uniq(h,n,o)	dll_insert_uniq((h),(n),(o))
#define netinfo_addrs_append(h,o)		dll_append((h),(o))
#define netinfo_addrs_append_uniq(h,n,o)	dll_append_uniq((h),(n),(o))
#define netinfo_addrs_search(h,k)		dll_search((h),(k))
#define netinfo_addrs_delete(h,o)		dll_delete((h),(o))
#define netinfo_addrs_minimum(h)		dll_minimum(h)
#define netinfo_addrs_maximum(h)		dll_maximum(h)
#define netinfo_addrs_successor(h,o)		dll_successor((h),(o))
#define netinfo_addrs_predecessor(h,o)		dll_predecessor((h),(o))
#define netinfo_addrs_iterate(h,d)		dll_iterate((h),(d))
#define netinfo_addrs_nextobj(h,i)		dll_nextobj((h),(i))
#define netinfo_addrs_iterate_done(h,i)		dll_iterate_done((h),(i))
#define netinfo_addrs_error_reason(h,i)		dll_error_reason((h),(i))
// @}


/**
 * @name URI support
 */
// @{

union bk_url_element_u
{
  bk_vptr	bue_vptr;		///< vptr version.
  char *	bue_str;		///< string version.
};


/**
 * Modes of URL parsing
 */
typedef enum
{
  BkUrlParseVptr=0,				///< Parse URL to vptrs pointing into original URL
  BkUrlParseVptrCopy,				///< As above but make private copy of URL.
  BkUrlParseStrNULL,				///< Make known elements strings. NULL for unknown.
  BkUrlParseStrEmpty,				///< Make known elements strings. "" for unknown.
} bk_url_parse_mode_e;


/**
 * Baka internal representation of a URL. The char *'s are all NULL
 * terminated strings for convenience.
 */
struct bk_url
{
  bk_flags			bu_flags;	///< Everyone needs flags.
#define BK_URL_FLAG_SCHEME		0x1	///< Scheme section set.
#define BK_URL_FLAG_AUTHORITY		0x2	///< Authority section set.
#define BK_URL_FLAG_PATH		0x4	///< Path section set.
#define BK_URL_FLAG_QUERY		0x8	///< Query section set.
#define BK_URL_FLAG_FRAGMENT		0x10	///< Fragment section set.
#define BK_URL_FLAG_HOST		0x20	///< Host authority section set.
#define BK_URL_FLAG_SERV		0x40	///< Service authority section set.
#define BK_URL_FLAG_ALL (BK_URL_FLAG_SCHEME | BK_URL_FLAG_AUTHORITY | BK_URL_FLAG_PATH | BK_URL_FLAG_QUERY | BK_URL_FLAG_FRAGMENT) ///< Convenience "sections" flag for bk_url_reconstruct
  bk_url_parse_mode_e		bu_mode;	///< Mode of URL
  char *			bu_url;		///< Entire URL
  union bk_url_element_u	bu_scheme;	///< Scheme specification
  union bk_url_element_u	bu_authority;	///< Authority specification
  union bk_url_element_u	bu_path;	///< Path specification
  union bk_url_element_u	bu_query;	///< Query specification
  union bk_url_element_u	bu_fragment;	///< Fragment specification
  union bk_url_element_u	bu_host;	///< Host (auth subset) specification
  union bk_url_element_u	bu_serv;	///< Service (auth. subset) specification
};

#define BK_URL_IS_VPTR(bu) ((bu)->bu_mode == BkUrlParseVptr || (bu)->bu_mode == BkUrlParseVptrCopy)
#define BK_URL_DATA(bu, element) ((BK_URL_IS_VPTR(bu))?((char *)(element).bue_vptr.ptr):((char *)(element).bue_str))
#define BK_URL_LEN(bu, element) ((BK_URL_IS_VPTR(bu))?(element).bue_vptr.len:strlen((element).bue_str))

#define BK_URL_SCHEME_DATA(bu) (BK_URL_DATA((bu),(bu)->bu_scheme))
#define BK_URL_SCHEME_LEN(bu) (BK_URL_LEN((bu),(bu)->bu_scheme))
#define BK_URL_SCHEME_EQ(bu,str) ((BK_URL_IS_VPTR(bu))?BK_STREQNCASE(BK_URL_SCHEME_DATA(bu),(str),BK_URL_SCHEME_LEN(bu)):BK_STREQCASE(BK_URL_SCHEME_DATA(bu),(str)))

#define BK_URL_AUTHORITY_DATA(bu) (BK_URL_DATA((bu),(bu)->bu_authority))
#define BK_URL_AUTHORITY_LEN(bu) (BK_URL_LEN((bu),(bu)->bu_authority))
#define BK_URL_AUTHORITY_EQ(bu,str) ((BK_URL_IS_VPTR(bu))?BK_STREQN(BK_URL_AUTHORITY_DATA(bu),(str),BK_URL_AUTHORITY_LEN(bu)):BK_STREQ(BK_URL_AUTHORITY_DATA(bu),(str)))

#define BK_URL_PATH_DATA(bu) (BK_URL_DATA((bu),(bu)->bu_path))
#define BK_URL_PATH_LEN(bu) (BK_URL_LEN((bu),(bu)->bu_path))
#define BK_URL_PATH_EQ(bu,str) ((BK_URL_IS_VPTR(bu))?BK_STREQN(BK_URL_PATH_DATA(bu),(str),BK_URL_PATH_LEN(bu)):BK_STREQ(BK_URL_PATH_DATA(bu),(str)))

#define BK_URL_QUERY_DATA(bu) (BK_URL_DATA((bu),(bu)->bu_query))
#define BK_URL_QUERY_LEN(bu) (BK_URL_LEN((bu),(bu)->bu_query))
#define BK_URL_QUERY_EQ(bu,str) ((BK_URL_IS_VPTR(bu))?BK_STREQN(BK_URL_QUERY_DATA(bu),(str),BK_URL_QUERY_LEN(bu)):BK_STREQ(BK_URL_QUERY_DATA(bu),(str)))

#define BK_URL_FRAGMENT_DATA(bu) (BK_URL_DATA((bu),(bu)->bu_fragment))
#define BK_URL_FRAGMENT_LEN(bu) (BK_URL_LEN((bu),(bu)->bu_fragment))
#define BK_URL_FRAGMENT_EQ(bu,str) ((BK_URL_IS_VPTR(bu))?BK_STREQN(BK_URL_FRAGMENT_DATA(bu),(str),BK_URL_FRAGMENT_LEN(bu)):BK_STREQ(BK_URL_FRAGMENT_DATA(bu),(str)))


/**
 * Breakdown of url authority section
 * char* members are guaranteed to be non-null.
 */
struct bk_url_authority
{
  char *	bua_user;			///< username
  char *	bua_pass;			///< password
  char *	bua_host;			///< host
  char *	bua_port;			///< port
  bk_flags	bua_flags;			///< Everyone needs flags
};
// @}



/**
 * ioh relay statistics
 */
// @{

/**
 * Stats for a single side of the connection
 */
struct bk_relay_ioh_stat
{
  u_quad_t	birs_readbytes;			///< How many octets read
  u_quad_t	birs_writebytes;		///< How many octets written
  u_quad_t	birs_ioh_ops;			///< Number of I/O operations (well, number of IOH read/writes)
  u_int		birs_stalls;			///< How many times we have stalled
};

/**
 * Stats for both sides of the connection
 */
struct bk_relay_ioh_stats
{
  struct bk_relay_ioh_stat side[2];		///< Duplicate structure for each side
};
// @}



struct bk_relay_cancel
{
  bk_flags		brc_flags;		///< Everyone needs flags.
  struct bk_ioh *	brc_ioh1;		///< First ioh.
  struct bk_ioh *	brc_ioh2;		///< Second ioh.
  void *		brc_opaque;		///< User opaque data.
};



/**
 * @name MD5 routines
 */
// @{
/*
 ***********************************************************************
 ** md5.h -- header file for implementation of MD5                    **
 ** RSA Data Security, Inc. MD5 Message-Digest Algorithm              **
 ** Created: 2/17/90 RLR                                              **
 ** Revised: 12/27/90 SRD,AJ,BSK,JT Reference C version               **
 ** Revised (for MD5): RLR 4/27/91                                    **
 ***********************************************************************
 */

/*
 ***********************************************************************
 ** Copyright (C) 1990, RSA Data Security, Inc. All rights reserved.  **
 **                                                                   **
 ** License to copy and use this software is granted provided that    **
 ** it is identified as the "RSA Data Security, Inc. MD5 Message-     **
 ** Digest Algorithm" in all material mentioning or referencing this  **
 ** software or this function.                                        **
 **                                                                   **
 ** License is also granted to make and use derivative works          **
 ** provided that such works are identified as "derived from the RSA  **
 ** Data Security, Inc. MD5 Message-Digest Algorithm" in all          **
 ** material mentioning or referencing the derived work.              **
 **                                                                   **
 ** RSA Data Security, Inc. makes no representations concerning       **
 ** either the merchantability of this software or the suitability    **
 ** of this software for any particular purpose.  It is provided "as  **
 ** is" without express or implied warranty of any kind.              **
 **                                                                   **
 ** These notices must be retained in any copies of any part of this  **
 ** documentation and/or software.                                    **
 ***********************************************************************
 */

/**
 * Data structure for MD5 (Message-Digest) computation
 */
typedef struct
{
  u_int32_t i[2];					///< number of _bits_ handled mod 2^64
  u_int32_t buf[4];					///< scratch buffer
  unsigned char in[64];				///< input buffer
  unsigned char digest[16];			///< actual digest after MD5Final call
}  bk_MD5_CTX;

/*
 ***********************************************************************
 ** End of md5.h                                                      **
 ******************************** (cut) ********************************
 */
// @}


/**
 * @name BAKA String Registry
 * Maps string to a uniq identifier for purposes of quick compare and perfect hashing.
 */
// @{
/**
 * Abstracted type of the value returned by the string registry.
 */
typedef u_int32_t bk_str_id_t;
typedef struct bk_str_registry *bk_str_registry_t;


/**
 * @name bk_str_id
 * This is a convience structure which allows callers to store the string
 * identifier with the string.
 */
struct bk_str_id
{
  char *		bsi_str;		///< The string
  bk_str_id_t		bsi_id;			///< Thd id
};

// @}



/**
 * @name BAKA vault (string indexed data storage)
 */
// @{
/**
 * @name bk_vault_node
 * This is what the user allocates and destroys for storage of data
 */
struct bk_vault_node
{
  char *	key;				///< Key index of data
  void *	value;				///< Value of data being stored
};


typedef dict_h bk_vault_t;			///< Abbreviation for some vague notion of abstraction...
// @}



/**
 * Sybolic name to value conversion structure for bk_string_*symbol*
 */
struct bk_symbol
{
  const u_int bs_val;				///< Symbolic Value
  const char *bs_string;			///< Textual name
};



/* b_general.c */
extern bk_s bk_general_init(int argc, char ***argv, char ***envp, const char *configfile, struct bk_config_user_pref *bcup, int error_queue_length, int log_facility, bk_flags flags);
#define BK_GENERAL_NOPROCTITLE 1		///< Specify that proctitle is not desired during general baka initialization
#define BK_GENERAL_THREADREADY	0x2		///< Be ready for threading
extern int bk_thread_safe_if_thread_ready;
extern bk_s bk_general_thread_init(bk_s B, const char *name);
extern void bk_general_thread_destroy(bk_s B);
extern int bk_general_thread_unready(bk_s B);
extern void bk_general_proctitle_set(bk_s B, char *);
extern void bk_general_reinit(bk_s B);
extern void bk_general_destroy(bk_s B);
extern int bk_general_reinit_insert(bk_s B, void (*bf_fun)(bk_s, void *, u_int), void *args);
extern int bk_general_reinit_delete(bk_s B, void (*bf_fun)(bk_s, void *, u_int), void *args);
extern int bk_general_destroy_insert(bk_s B, void (*bf_fun)(bk_s, void *, u_int), void *args);
extern int bk_general_destroy_delete(bk_s B, void (*bf_fun)(bk_s, void *, u_int), void *args);
extern void bk_general_syslog(bk_s B, int level, bk_flags flags, const char *format, ...) __attribute__ ((format (printf, 4, 5)));
extern void bk_general_vsyslog(bk_s B, int level, bk_flags flags, const char *format, va_list args);
#define BK_SYSLOG_FLAG_NOFUN 1			///< Don't want function name included during bk_general_*syslog
#define BK_SYSLOG_FLAG_NOLEVEL 2		///< Don't want error level included during bk_general_*syslog
extern const char *bk_general_errorstr(bk_s B, int level);
extern int bk_general_funstat_init(bk_s B, char *filename, bk_flags flags);
extern int bk_general_debug_config(bk_s B, FILE *fh, int sysloglevel, bk_flags flags);
extern void *bk_nullptr;			/* NULL pointer junk */
extern int bk_zeroint;				/* Zero integer junk */
extern unsigned bk_zerouint;			/* Zero unsigned junk */


/* b_bits.c */
extern char *bk_bits_create(bk_s B, size_t size, bk_flags flags);
extern void bk_bits_destroy(bk_s B, char *base);
extern char *bk_bits_save(bk_s B, char *base, size_t size, bk_flags flags);
extern char *bk_bits_restore(bk_s B, char *saved, size_t *size, bk_flags flags);
extern u_int bk_bitcount(bk_s B, u_int word);



/* b_cksum.c */
extern int bk_in_cksum(bk_s B, bk_vptr *m, int len);



/* b_crc.c */
extern u_int32_t bk_crc32(u_int32_t crc, void *buf, int len);



/* b_config.c */
extern struct bk_config *bk_config_init(bk_s B, const char *filename, struct bk_config_user_pref *bcup, bk_flags flags);
extern int bk_config_get(bk_s B, struct bk_config *config, const char **filename, struct bk_config_user_pref **bcup, bk_flags *getflags);
extern int bk_config_reinit(bk_s B, struct bk_config *config);
extern void bk_config_destroy(bk_s B, struct bk_config *config);
extern void bk_config_write(bk_s B, struct bk_config *config, char *outfile);
extern int bk_config_insert(bk_s B, struct bk_config *config, char *key, char *value);
extern char *bk_config_getnext(bk_s B, struct bk_config *ibc, const char *key, const char *ovalue);
extern int bk_config_delete_key(bk_s B, struct bk_config *ibc, const char *key);
extern int bk_config_delete_value(bk_s B, struct bk_config *ibc, const char *key, const char *value);
extern void bk_config_print(bk_s B, struct bk_config *ibc, FILE *fp);



/* b_debug.c */
extern struct bk_debug *bk_debug_init(bk_s B, bk_flags flags);
extern void bk_debug_destroy(bk_s B, struct bk_debug *bd);
extern void bk_debug_reinit(bk_s B, struct bk_debug *bd);
extern u_int32_t bk_debug_query(bk_s B, struct bk_debug *bdinfo, const char *funname, const char *pkgname, const char *group, bk_flags flags);
extern int bk_debug_set(bk_s B, struct bk_debug *bdinfo, const char *name, u_int32_t level);
extern int bk_debug_setconfig(bk_s B, struct bk_debug *bdinfo, struct bk_config *config, const char *program);
extern void bk_debug_config(bk_s B, struct bk_debug *bdinfo, FILE *fh, int sysloglevel, bk_flags flags);
extern void bk_debug_iprint(bk_s B, struct bk_debug *bdinfo, const char *buf);
extern void bk_debug_iprintf(bk_s B, struct bk_debug *bdinfo, const char *format, ...) __attribute__ ((format (printf, 3, 4)));
extern void bk_debug_iprintbuf(bk_s B, struct bk_debug *bdinfo, const char *intro, const char *prefix, const bk_vptr *buf);
extern void bk_debug_ivprintf(bk_s B, struct bk_debug *bdinfo, const char *format, va_list ap) __attribute__ ((format (printf, 3, 0)));
extern FILE *bk_debug_istream(struct bk_debug *bdfinfo);




/* b_error.c */
extern struct bk_error *bk_error_init(bk_s B, u_int16_t queuelen, FILE *fh, int syslogthreshold, bk_flags flags);
#define BK_ERROR_FLAG_SYSLOG_FULL		0x100 ///< Duplicate timestamp and program id in syslog errors
#define BK_ERROR_FLAG_BRIEF			0x200 ///< Omit timestamp and program id from non-syslog errors
#define BK_ERROR_FLAG_NO_FUN			0x400 ///< Only display original error message string
#define BK_ERROR_FLAG_MORE_FUN			0x800 ///< Display full stack trace on error
extern void bk_error_destroy(bk_s B, struct bk_error *beinfo);
extern void bk_error_config(bk_s B, struct bk_error *beinfo, u_int16_t queuelen, FILE *fh, int syslogthreshold, int hilo_pivot, bk_flags flags);
#define BK_ERROR_CONFIG_FH			0x1 ///< File handle is being set
#define BK_ERROR_CONFIG_HILO_PIVOT		0x2 ///< Hilo_pivot is being set
#define BK_ERROR_CONFIG_SYSLOGTHRESHOLD		0x4 ///< Syslog threshold is being set
#define BK_ERROR_CONFIG_QUEUELEN		0x8 ///< Queue length is being set
#define BK_ERROR_CONFIG_FLAGS			0x10 ///< BK_ERROR_INIT flags are being set
extern void bk_error_iclear(bk_s B, struct bk_error *beinfo, const char *mark, bk_flags flags);
extern void bk_error_iflush(bk_s B, struct bk_error *beinfo, const char *mark, bk_flags flags);
extern void bk_error_imark(bk_s B, struct bk_error *beinfo, const char *mark, bk_flags flags);
extern void bk_error_iprint(bk_s B, int sysloglevel, struct bk_error *beinfo, const char *buf);
extern void bk_error_iprintf(bk_s B, int sysloglevel, struct bk_error *beinfo, const char *format, ...) __attribute__ ((format (printf, 4, 5)));
extern void bk_error_iprintbuf(bk_s B, int sysloglevel, struct bk_error *beinfo, const char *intro, const char *prefix, const bk_vptr *buf);
extern void bk_error_ivprintf(bk_s B, int sysloglevel, struct bk_error *beinfo, const char *format, va_list ap) __attribute__ ((format (printf, 4, 0)));
extern void bk_error_idump(bk_s B, struct bk_error *beinfo, FILE *fh, const char *mark, int minimumlevel, int sysloglevel, bk_flags flags);
extern char *bk_error_istrdump(bk_s B, struct bk_error *beinfo, const char *mark, int minimumlevel, bk_flags flags);
extern void bk_error_irepeater_flush(bk_s B, struct bk_error *beinfo, bk_flags flags);



/* b_fun.c */
extern dict_h bk_fun_init(void);
extern void bk_fun_destroy(dict_h funstack);
extern struct bk_funinfo *bk_fun_entry(bk_s B, const char *func, const char *package, const char *group);
extern void bk_fun_exit(bk_s B, struct bk_funinfo *fh);
extern void bk_fun_reentry_i(bk_s B, struct bk_funinfo *fh);
extern void bk_fun_trace(bk_s B, FILE *out, int sysloglevel, bk_flags flags);
extern void bk_fun_set(bk_s B, int state, bk_flags flags);
#define BK_FUN_OFF	0			///< Turn off function tracing in @a bk_fun_set
#define BK_FUN_ON	1			///< Turn on function tracing in @a bk_fun_set
extern const char *bk_fun_funname(bk_s B, int ancestordepth, bk_flags flags);
extern int bk_fun_reset_debug(bk_s B, bk_flags flags);


/* b_funlist.c */
extern struct bk_funlist *bk_funlist_init(bk_s B, bk_flags flags);
extern void bk_funlist_destroy(bk_s B, struct bk_funlist *funlist);
extern void bk_funlist_call(bk_s B, struct bk_funlist *funlist, u_int aux, bk_flags flags);
extern int bk_funlist_insert(bk_s B, struct bk_funlist *funlist, void (*bf_fun)(bk_s, void *, u_int), void *args, bk_flags flags);
extern int bk_funlist_delete(bk_s B, struct bk_funlist *funlist, void (*bf_fun)(bk_s, void *, u_int), void *args, bk_flags flags);


/* b_memx.c */
extern const size_t bk_memx_size;
extern struct bk_memx *bk_memx_create(bk_s B, size_t objsize, u_int start_hint, u_int incr_hint, bk_flags flags);
extern void bk_memx_destroy(bk_s B, struct bk_memx *bm, bk_flags flags);
#define BK_MEMX_PRESERVE_ARRAY    1		///< Don't destroy created array in @a bk_memx_destroy
extern void *bk_memx_get(bk_s B, struct bk_memx *bm, u_int count, u_int *curused, bk_flags flags);
#define BK_MEMX_GETNEW		  1		///< Get new allocation in @a bk_memx_get
#define bk_memx_new(B, bm, count, curused, flags) bk_memx_get((B), (bm), (count), (curused), (flags)|BK_MEMX_GETNEW) ///< For alex
extern int bk_memx_addstr(bk_s B, struct bk_memx *bm, char *str, bk_flags flags);
extern int bk_memx_trunc(bk_s B, struct bk_memx *bm, u_int count, bk_flags flags);
extern int bk_memx_lop(bk_s B, struct bk_memx *bm, u_int count, bk_flags flags);
extern int bk_memx_append(bk_s B, struct bk_memx *bm, const void *data, u_int count, bk_flags flags);
extern int bk_memx_info(bk_s B, struct bk_memx *bm, void **arrayp, size_t *unitesizep, size_t *curallocp, size_t *curusedp, u_int *incrp, bk_flags *flagsp, bk_flags flags);

/* b_run.c */
extern struct bk_run *bk_run_init(bk_s B, bk_flags flags);
#define BK_RUN_WANT_SIGNALTHREAD		0x01 ///< Tell bk_run that we only want signal processing on this thread--the one which is initializing bk_run_init
extern void bk_run_destroy(bk_s B, struct bk_run *run);
extern int bk_run_signal(bk_s B, struct bk_run *run, int signum, void (*handler)(bk_s B, struct bk_run *run, int signum, void *opaque), void *opaque, bk_flags flags);
#define BK_RUN_SIGNAL_CLEARPENDING		0x01 ///< Clear pending signal count for this signum for @a bk_run_signal
#define BK_RUN_SIGNAL_INTR			0x02 ///< Interrupt system calls for @a bk_run_signal
#define BK_RUN_SIGNAL_RESTART			0x04 ///< Restart system calls for @a bk_run_signal
extern int bk_run_enqueue(bk_s B, struct bk_run *run, struct timeval when, void (*event)(bk_s B, struct bk_run *run, void *opaque, const struct timeval starttime, bk_flags flags), void *opaque, void **handle, bk_flags flags);
//#define BK_RUN_THREADREADY			0x10000 ///< Handler is prepared to run in a thread
extern int bk_run_enqueue_delta(bk_s B, struct bk_run *run, time_t msecs, void (*event)(bk_s B, struct bk_run *run, void *opaque, const struct timeval starttime, bk_flags flags), void *opaque, void **handle, bk_flags flags);
//#define BK_RUN_THREADREADY			0x10000 ///< Handler is prepared to run in a thread
extern int bk_run_enqueue_cron(bk_s B, struct bk_run *run, time_t msecs, void (*event)(bk_s B, struct bk_run *run, void *opaque, const struct timeval starttime, bk_flags flags), void *opaque, void **handle, bk_flags flags);
//#define BK_RUN_THREADREADY			0x10000 ///< Handler is prepared to run in a thread
extern int bk_run_dequeue(bk_s B, struct bk_run *run, void *handle, bk_flags flags);
#define BK_RUN_DEQUEUE_EVENT			0x01 ///< Normal event to dequeue for @a bk_run_dequeue
#define BK_RUN_DEQUEUE_CRON			0x02 ///< Cron event to dequeue for @a bk_run_dequeue
#define BK_RUN_DEQUEUE_WAIT			0x04 ///< Wait for currently executing event to complete before returning--watch for lock inheritence bug
extern int bk_run_run(bk_s B, struct bk_run *run, bk_flags flags);
extern int bk_run_once(bk_s B, struct bk_run *run, bk_flags flags);
#define BK_RUN_ONCE_FLAG_DONT_BLOCK		0x1 ///<  Execute run once without blocking in select(2).
#define BK_RUN_ONCE_FLAG_BLOCK			0x2 ///<  Execute run once normally (ie pause in select(2)). This option is mostly for callers of bk_run_once() to use themselves.
extern int bk_run_handle(bk_s B, struct bk_run *run, int fd, bk_fd_handler_t handler, void *opaque, u_int wanttypes, bk_flags flags);
// handler flags passed to bk_run_handle/poll_add/idle_add/on_demand/add
#define BK_RUN_HANDLE_TIME			0x01 ///< user handler wants current time
// flags passed to run handler
#define BK_RUN_READREADY			0x01 ///< user handler is notified by bk_run that data is ready for reading
#define BK_RUN_WRITEREADY			0x02 ///< user handler is notified by bk_run that data is ready for writing
#define BK_RUN_XCPTREADY			0x04 ///< user handler is notified by bk_run that exception data is ready
#define BK_RUN_CLOSE				0x08 ///< user handler is notified by bk_run that bk_run is in process of closing this fd
#define BK_RUN_DESTROY				0x10 ///< user handler is notified by bk_run that bk_run is in process of destroying
#define BK_RUN_BAD_FD				0x20 ///< Select(2) returned EBADFD for this fd.
#define BK_RUN_USERFLAG1			0x1000 ///< Reserved for user use
#define BK_RUN_USERFLAG2			0x2000 ///< Reserved for user use
#define BK_RUN_USERFLAG3			0x4000 ///< Reserved for user use
#define BK_RUN_USERFLAG4			0x8000 ///< Reserved for user use
#define BK_RUN_THREADREADY			0x10000 ///< Handler is prepared to run in a thread
extern int bk_run_close(bk_s B, struct bk_run *run, int fd, bk_flags flags);
#define BK_RUN_CLOSE_FLAG_NO_HANDLER		0x01 ///< We do not want the handler run while closing this FD.
extern u_int bk_run_getpref(bk_s B, struct bk_run *run, int fd, bk_flags flags);
extern int bk_run_setpref(bk_s B, struct bk_run *run, int fd, u_int wanttypes, u_int wantmask, bk_flags flags);
#define BK_RUN_WANTREAD				0x01 ///< Specify to bk_run_setpref that we want read notification for this fd
#define BK_RUN_WANTWRITE			0x02 ///< Specify to bk_run_setpref that we want write notification for this fd
#define BK_RUN_WANTXCPT				0x04 ///< Specify to bk_run_setpref that we want exceptional notification for this fd
#define BK_RUN_WANTALL				(BK_RUN_WANTREAD|BK_RUN_WANTWRITE|BK_RUN_WANTXCPT|) ///< Specify to bk_run_setpref that we want *all* notifications.
extern int bk_run_poll_add(bk_s B, struct bk_run *run, int (*fun)(bk_s B, struct bk_run *run, void *opaque, const struct timeval *starttime, struct timeval *delta, bk_flags flags), void *opaque, void **handle, int flags);
extern int bk_run_poll_remove(bk_s B, struct bk_run *run, void *handle);
extern int bk_run_idle_add(bk_s B, struct bk_run *run, int (*fun)(bk_s B, struct bk_run *run, void *opaque, const struct timeval *starttime, struct timeval *delta, bk_flags flags), void *opaque, void **handle, int flags);
extern int bk_run_idle_remove(bk_s B, struct bk_run *run, void *handle);
extern int bk_run_on_demand_add(bk_s B, struct bk_run *run, bk_run_on_demand_f fun, void *opaque, volatile int *demand, void **handle, int flags);
extern int bk_run_on_demand_remove(bk_s B, struct bk_run *run, void *handle);
extern int bk_run_set_run_over(bk_s B, struct bk_run *run);
extern void bk_run_set_dont_block_run_once(bk_s B, struct bk_run *run);
extern int bk_run_fd_cancel_register(bk_s B, struct bk_run *run, int fd, bk_flags flags);
#define BK_FD_ADMIN_FLAG_WANT_CANCEL		0x1 ///< This fd wants cancel
#define BK_FD_ADMIN_FLAG_WANT_ADMIN_CLOSE	0x2 ///< This fd want admin close
#define BK_FD_ADMIN_FLAG_WANT_ALL (BK_FD_ADMIN_FLAG_WANT_CANCEL | BK_FD_ADMIN_FLAG_WANT_ADMIN_CLOSE)
#define BK_FD_ADMIN_FLAG_RESERVED1		0x4 ///< This flag is used internally
#define BK_FD_ADMIN_FLAG_RESERVED2		0x8 ///< This file is used internally

#define BK_FD_ADMIN_FLAG_CANCEL			0x1 ///< Perform cancel operation
#define BK_FD_ADMIN_FLAG_CLOSE			0x2 ///< Perform close operation
extern int bk_run_fd_cancel_unregister(bk_s B, struct bk_run *run, int fd, bk_flags flags);
extern int bk_run_fd_is_canceled(bk_s B, struct bk_run *run, int fd);
extern int bk_run_fd_is_closed(bk_s B, struct bk_run *run, int fd);
extern int bk_run_fd_cancel(bk_s B, struct bk_run *run, int fd, bk_flags flags);
extern void bk_run_handler_discard(bk_s B, struct bk_run *run, int fd, u_int types, void *opaque, const struct timeval *starttime);
extern void bk_run_select_changed(bk_s B, struct bk_run *run, bk_flags flags);
extern int bk_run_on_iothread(bk_s B, struct bk_run *run);



/* b_ioh.c */
typedef int (*bk_iorfunc_f)(bk_s B, struct bk_ioh *ioh, void *opaque, int fd, caddr_t buf, __SIZE_TYPE__ size, bk_flags flags); ///< read style I/O function for bk_ioh (flags for specialized datagram handling, like peek)
typedef int (*bk_iowfunc_f)(bk_s B, struct bk_ioh *ioh, void *opaque, int fd, struct iovec *iov, __SIZE_TYPE__ size, bk_flags flags); ///< writev style I/O function for bk_ioh
typedef void (*bk_iocfunc_f)(bk_s B, struct bk_ioh *ioh, void *opaque, int fdin, int fdout, bk_flags flags); ///< close function for bk_ioh
typedef void (*bk_iohhandler_f)(bk_s B, bk_vptr *data, void *opaque, struct bk_ioh *ioh, bk_ioh_status_e state_flags);  ///< User callback for bk_ioh w/zero terminated array of data ptrs free'd after handler returns
extern struct bk_ioh *bk_ioh_init(bk_s B, struct bk_ssl *ssl, int fdin, int fdout, bk_iohhandler_f handler, void *opaque, u_int32_t inbufhint, u_int32_t inbufmax, u_int32_t outbufmax, struct bk_run *run, bk_flags flags);
#define BK_IOH_STREAM		0x001		///< Stream (instead of datagram) oriented protocol, for bk_ioh
#define BK_IOH_RAW		0x002		///< Any data is suitable, no special message blocking, for bk_ioh
#define BK_IOH_BLOCKED		0x004		///< Must I/O in hint blocks: block size is required, for bk_ioh
#define BK_IOH_VECTORED		0x008		///< Size of data sent before data: datagramish semantics, for bk_ioh
#define BK_IOH_LINE		0x010		///< Line oriented reads, for bk_ioh
#define BK_IOH_WRITE_ALL	0x020		///< Write all available data when doing a write, for bk_ioh
#define BK_IOH_FOLLOW		0x040		///< Put the ioh in "follow" mode (read past EOF).
#define BK_IOH_DONT_ACTIVATE	0x080		///< Don't add handler to run loop
#define BK_IOH_NO_HANDLER	0x8000		///< Suppress stupid warning

#if 0
#define BK_IOH_STATUS_INCOMPLETEREAD	1	///< bk_ioh notifying user handler of incomplete read, data provided
#define BK_IOH_STATUS_IOHCLOSING	2	///< bk_ioh notifying user handler of IOH in process of close() -- all data flushed/drained at this point
#define BK_IOH_STATUS_IOHREADERROR	3	///< bk_ioh notifying user handler of a received IOH error on read
#define BK_IOH_STATUS_IOHWRITEERROR	4	///< bk_ioh notifying user handler of a received IOH error on write--and here is the buffer
#define BK_IOH_STATUS_IOHABORT		5	///< bk_ioh notifying user handler of IOH going away due to system constraints
#define BK_IOH_STATUS_WRITECOMPLETE	6	///< bk_ioh notifying user handler of a previous write of a buffer has completed, and here is the buffer
#define BK_IOH_STATUS_WRITEABORTED	7	///< bk_ioh notifying user handler of a previous write of a buffer was not fully completed before abort, and here is the buffer
#define BK_IOH_STATUS_READCOMPLETE	8	///< bk_ioh notifying user handler of a previous write of a buffer has completed, and here is the zero terminated array of buffers, which you must copy if necessary
#define BK_IOH_STATUS_USERERROR		9	///< bk_ioh notifying user handler of some kind of user error being propagated in some unknown and unknowable fashion
#define BK_IOH_STATUS_IOHREADEOF	10	///< bk_ioh notifying user handler of a received EOF
#endif

extern int bk_ioh_update(bk_s B, struct bk_ioh *ioh, bk_iorfunc_f readfun, bk_iowfunc_f writefun, bk_iocfunc_f closefun, void *iofunopaque, bk_iohhandler_f handler, void *opaque, u_int32_t inbufhint, u_int32_t inbufmax, u_int32_t outbufmax, bk_flags flags, bk_flags updateflags);
#define BK_IOH_UPDATE_READFUN		0x001	///< Update the read function
#define BK_IOH_UPDATE_WRITEFUN		0x002	///< Update the write function
#define BK_IOH_UPDATE_HANDLER		0x004	///< Update the handler
#define BK_IOH_UPDATE_OPAQUE		0x008	///< Update the opaque
#define BK_IOH_UPDATE_INBUFHINT		0x010	///< Update the inbufhint
#define BK_IOH_UPDATE_INBUFMAX		0x020	///< Update the inbufmax
#define BK_IOH_UPDATE_OUTBUFMAX		0x040	///< Update the outbufmax
#define BK_IOH_UPDATE_FLAGS		0x080	///< Update the external flags
#define BK_IOH_UPDATE_IOFUNOPAQUE	0x100	///< Update the i/o function opaque data
#define BK_IOH_UPDATE_CLOSEFUN		0x200	///< Update the write function

extern int bk_ioh_get(bk_s B, struct bk_ioh *ioh, int *fdin, int *fdout, bk_iorfunc_f *readfun, bk_iowfunc_f *writefun, bk_iocfunc_f *closefun, void **iofunopaque, bk_iohhandler_f *handler, void **opaque, u_int32_t *inbufhint, u_int32_t *inbufmax, u_int32_t *outbufmax, struct bk_run **run, bk_flags *flags);
extern int bk_ioh_write(bk_s B, struct bk_ioh *ioh, bk_vptr *data, bk_flags flags);
#define BK_IOH_BYPASSQUEUEFULL	0x01		///< Bypass bk_ioh_write normal check for output queue full
extern void bk_ioh_shutdown(bk_s B, struct bk_ioh *ioh, int how, bk_flags flags);
extern int bk_ioh_readallowed(bk_s B, struct bk_ioh *ioh, int isallowed, bk_flags flags);
extern void bk_ioh_flush(bk_s B, struct bk_ioh *ioh, int how, bk_flags flags);
#define BK_IOH_FLUSH_NOEXECUTE	0x01		///< Flag for close processing--not for user consumption
extern void bk_ioh_close(bk_s B, struct bk_ioh *ioh, bk_flags flags);
#define BK_IOH_ABORT		0x01		///< During bk_ioh_close: Abort stream immediately -- don't wait to drain */
#define BK_IOH_DONTCLOSEFDS	0x04		///< During bk_ioh_close: Don't close the file descriptors during close */
extern int bk_ioh_stdrdfun(bk_s B, struct bk_ioh *ioh, void *opaque, int fd, caddr_t buf, __SIZE_TYPE__ size, bk_flags flags);		///< read() when implemented in ioh style
extern int bk_ioh_stdwrfun(bk_s B, struct bk_ioh *ioh, void *opaque, int fd, struct iovec *buf, __SIZE_TYPE__ size, bk_flags flags);	///< write() when implemented in ioh style
void bk_ioh_stdclosefun(bk_s B, struct bk_ioh *ioh, void *opaque, int fdin, int fdout, bk_flags flags);	///< close() implemented in ioh style
extern int bk_ioh_getqlen(bk_s B, struct bk_ioh *ioh, u_int32_t *inqueue, u_int32_t *outqueue, bk_flags flags);
extern void bk_ioh_flush_read(bk_s B, struct bk_ioh *ioh, bk_flags flags);
extern void bk_ioh_flush_write(bk_s B, struct bk_ioh *ioh, bk_flags flags);
extern int bk_ioh_seek(bk_s B, struct bk_ioh *ioh, off_t offset, int whence);
extern bk_vptr *bk_ioh_coalesce(bk_s B, bk_vptr *data, bk_vptr *curvptr, bk_flags in_flags, bk_flags *out_flagsp);
// In flags
#define		BK_IOH_COALESCE_FLAG_MUST_COPY		0x1 ///< Coalesce code *must* copy.
#define		BK_IOH_COALESCE_FLAG_TRAILING_NULL	0x2 ///< Ensure trailing null (implies must-copy)
#define		BK_IOH_COALESCE_FLAG_SEIZE_DATA		0x4 ///< The caller wants to take the data to avoid the copy
// Out flags
#define	BK_IOH_COALESCE_OUT_FLAG_NO_COPY	0x1 ///< Data not copied.
extern int bk_ioh_print(bk_s B, struct bk_ioh *ioh, const char *str);
extern int bk_ioh_printf(bk_s B, struct bk_ioh *ioh, const char *format, ...);
extern int bk_ioh_stdio_init(bk_s B, struct bk_ioh *ioh, int compression_level, int auth_alg, bk_vptr auth_key, char *auth_name , int encrypt_alg, bk_vptr encrypt_key, bk_flags flags);
extern int bk_ioh_cancel_register(bk_s B, struct bk_ioh *ioh, bk_flags flags);
extern int bk_ioh_cancel_unregister(bk_s B, struct bk_ioh *ioh, bk_flags flags);
extern int bk_ioh_is_canceled(bk_s B, struct bk_ioh *ioh, bk_flags flags);
extern int bk_ioh_cancel(bk_s B, struct bk_ioh *ioh, bk_flags flags);
extern int bk_ioh_last_error(bk_s B, struct bk_ioh *ioh, bk_flags flags);
extern int bk_ioh_data_seize_permitted(bk_s B, struct bk_ioh *ioh, bk_flags flags);

/* b_pollio.c */
extern struct bk_polling_io *bk_polling_io_create(bk_s B, struct bk_ioh *ioh, bk_flags flags);
#define BK_POLLING_LINGER		0x1	///< This is a linger close
#define BK_POLLING_SYNC		0x2	///< Syncronous writes
#define BK_POLLING_THREADED		0x4	///< Callee is in a seperate thread from the bk_run loop--do not call bk_run_once
extern void bk_polling_io_close(bk_s B, struct bk_polling_io *bpi, bk_flags flags);
//#define BK_POLLING_LINGER		0x1	///< This is a linger close
#define BK_POLLING_DONT_LINGER		0x2	///< This is a non-lingering close
extern void  bk_polling_io_data_destroy(bk_s B, bk_vptr *data);
extern int bk_polling_io_throttle(bk_s B, struct bk_polling_io *bpi, bk_flags flags);
extern int bk_polling_io_unthrottle(bk_s B, struct bk_polling_io *bpi, bk_flags flags);
extern void bk_polling_io_flush(bk_s B, struct bk_polling_io *bpi, bk_flags flags);
extern int bk_polling_io_read(bk_s B, struct bk_polling_io *bpi, bk_vptr **datap, bk_ioh_status_e *status, time_t mstimeout, bk_flags flags);
extern int bk_polling_io_write(bk_s B, struct bk_polling_io *bpi, bk_vptr *data, time_t mstimeout, bk_flags flags);
extern int bk_polling_io_do_poll(bk_s B, struct bk_polling_io *bpi, bk_vptr **datap, bk_ioh_status_e *status, bk_flags flags);
extern int bk_polling_io_cancel_register(bk_s B, struct bk_polling_io *bpi, bk_flags flags);
extern int bk_polling_io_cancel_unregister(bk_s B, struct bk_polling_io *bpi, bk_flags flags);
extern int bk_polling_io_is_canceled(bk_s B, struct bk_polling_io *bpi, bk_flags flags);
extern int bk_polling_io_cancel(bk_s B, struct bk_polling_io *bpi, bk_flags flags);
extern int bk_polling_io_seek(bk_s B, struct bk_polling_io *bpi, off_t offset, int whence, bk_flags flags);
extern int64_t bk_polling_io_tell(bk_s B, struct bk_polling_io *bpi, bk_flags flags);
extern const char *bk_polling_io_geterr(bk_s B, struct bk_polling_io *bpi);


/* b_stdfun.c */
extern void bk_die(bk_s B, u_char retcode, FILE *output, char *reason, bk_flags flags);
extern void bk_warn(bk_s B, FILE *output, char *reason, bk_flags flags);
#define BK_WARNDIE_WANTDETAILS		1	///< Verbose output of error logs during bk_die/bk_warn
extern void bk_exit(bk_s B, u_char retcode);
extern void bk_dmalloc_shutdown(bk_s B, void *opaque, u_int other);
extern int bk_ssl_supported(bk_s B);
#ifdef NO_SSL
extern void bk_ssl_destroy(bk_s B, void *ssl, bk_flags flags);
#endif /* NO_SSL */



/* b_sysutils.c */
extern char *bk_gethostname(bk_s B);


/* b_strcode.c */
extern char *bk_encode_base64(bk_s B, const bk_vptr *str, const char *eolseq);
extern bk_vptr *bk_decode_base64(bk_s B, const char *str);
extern char *bk_string_str2xml(bk_s B, const char *str, bk_flags flags);
#define BK_STRING_STR2XML_FLAG_ALLOW_NON_PRINT	0x1 ///< Allow non printable chars in output xml string.
#define BK_STRING_STR2XML_FLAG_ENCODE_WHITESPACE 0x2 ///< Encode whitespace other than space (\040).
extern int bk_string_xml_escape_comment(bk_s B, const char *str, char **result, bk_flags flags);

/* b_strconv.c */
extern int bk_string_atou32(bk_s B, const char *string, u_int32_t *value, bk_flags flags);
extern int bk_string_atoi32(bk_s B, const char *string, int32_t *value, bk_flags flags);
extern int bk_string_atot(bk_s B, const char *string, time_t *value, bk_flags flags);
extern int bk_string_atou64(bk_s B, const char *string, u_int64_t *value, bk_flags flags);
extern int bk_string_atoi64(bk_s B, const char *string, int64_t *value, bk_flags flags);
#define BK_STRING_ATOI_FLAG_HEX		0x1	///< This number is hexidecimal even though it might not look like it
#define BK_STRING_ATOI_FLAG_OCT		0x2	///< This number is octal even though it might not look like it
#define BK_STRING_ATOI_FLAG_DEC		0x4	///< This number is octal even though it might not look like it
extern int bk_string_flagtoa(bk_s B, bk_flags src, char *dst, size_t len, const char *names, bk_flags flags);
extern int bk_string_atoflag(bk_s B, const char *src, bk_flags *dst, const char *names, bk_flags flags);
extern const char *bk_string_symboltoa(bk_s B, u_int value, struct bk_symbol *convlist, bk_flags flags);
extern int bk_string_atosymbol(bk_s B, const char *name, u_int *value, struct bk_symbol *convlist, bk_flags flags);
#define BK_STRING_ATOSYMBOL_CASEINSENSITIVE 0x1
extern int bk_string_intcols(bk_s B, int64_t num, u_int base);
extern int bk_string_atod(bk_s B, const char *string, double *value, bk_flags flags);
#define BK_STRING_ATOD_FLAG_ALLOW_INF 0x1
#define BK_STRING_ATOD_FLAG_ALLOW_NAN 0x2
extern int bk_string_atof(bk_s B, const char *string, float *value, bk_flags flags);
#define BK_STRING_ATOF_FLAG_ALLOW_INF BK_STRING_ATOD_FLAG_ALLOW_INF
#define BK_STRING_ATOF_FLAG_ALLOW_NAN BK_STRING_ATOD_FLAG_ALLOW_NAN

#if SIZEOF_INT == 4
#define BK_STRING_ATOI bk_string_atoi32
#define BK_STRING_ATOU bk_string_atou32
#endif

#if SIZEOF_INT == 8
#define BK_STRING_ATOI bk_string_atoi64
#define BK_STRING_ATOU bk_string_atou64
#endif

#if SIZEOF_LONG == 4
#define BK_STRING_ATOL bk_string_atoi32
#define BK_STRING_ATOUL bk_string_atou32
#endif

#if SIZEOF_LONG == 8
#define BK_STRING_ATOL bk_string_atoi64
#define BK_STRING_ATOUL bk_string_atou64
#endif


#if SIZEOF_LONG_LONG == 4
#define BK_STRING_ATOLL bk_string_atoi32
#define BK_STRING_ATOULL bk_string_atou32
#endif

#if SIZEOF_LONG_LONG == 8
#define BK_STRING_ATOLL bk_string_atoi64
#define BK_STRING_ATOULL bk_string_atou64
#endif

extern char *bk_string_magnitude(bk_s B, double number, u_int precision, char *units, char *buffer, u_int buflen, bk_flags flags);
#define BK_STRING_MAGNITUDE_POWER10		0x01		// Request power of 10 (instead of power of two) magnitude
extern double bk_string_demagnify(bk_s B, const char *number, bk_flags flags);


/* b_string.c */
extern u_int bk_strhash(const char *a, bk_flags flags);
// RESERVED FOR BK_HASH_STRING  0x01 NOT NEEDED BY THIS FUNCTION
#define BK_HASH_V2		0x02		///< Jenkinshash -- slow, better distribution, poorly understood
#define BK_HASH_MODULUS		0x04		///< Modulus by large prime
#define BK_HASH_V1		0x08		///< K&R hash -- slowish, not so great
#define BK_HASH_V3		0x10		///< murmurhash -- fast, good distribution
extern u_int bk_bufhash(const bk_vptr *b, bk_flags flags);
#define BK_HASH_STRING		0x01		///< String hash if len=1/2/4/8 (UNUSED AND DEPRECATED!!!!)

extern char **bk_string_tokenize_split(bk_s B, const char *src, u_int limit, const char *spliton, const char *braces, const dict_h kvht_vardb, const char **variabledb, bk_flags flags);
#define BK_WHITESPACE					" \t\r\n" ///< General definition of horizonal and vertical whitespace
#define BK_VWHITESPACE					"\r\n" ///< General definition of vertical whitespace
#define BK_HWHITESPACE					" \t" ///< General definition of horizontal whitespace
#define BK_STRING_TOKENIZE_MULTISPLIT			0x001	///< During bk_string_tokenize_split: Allow multiple split characters to seperate items (foo::bar are two tokens, not three)
#define BK_STRING_TOKENIZE_SINGLEQUOTE			0x002	///< During bk_string_tokenize_split: Handle single quotes
#define BK_STRING_TOKENIZE_DOUBLEQUOTE			0x004	///< During bk_string_tokenize_split: Handle double quotes
// #define BK_STRING_TOKENIZE_VARIABLE			0x008	///< Reserved for variable expansion--implicit flag
#define BK_STRING_TOKENIZE_BACKSLASH			0x010	///< During bk_string_tokenize_split: Backslash quote next char
#define BK_STRING_TOKENIZE_BACKSLASH_INTERPOLATE_CHAR	0x020	///< During bk_string_tokenize_split: Convert \n et al
#define BK_STRING_TOKENIZE_BACKSLASH_INTERPOLATE_OCT	0x040	///< During bk_string_tokenize_split: Convert \010 et al
#define BK_STRING_TOKENIZE_SKIPLEADING			0x080   ///< During bk_string_tokenize_split: Bypass leading split chars
#define BK_STRING_TOKENIZE_CONF_EXPAND			0x100   ///< Allow variable expansion from bk_conf.
#define BK_STRING_TOKENIZE_WANT_EMPTY_TOKEN		0x200   ///< Returned array contains an empty string if input is empty.
#define BK_STRING_TOKENIZE_KEEP_BRACES			0x400   ///< If doing brace processing, don't strip outter most braces.
#define BK_STRING_TOKENIZE_BRACES_LIKE_SQUOTE		0x800   ///< Do not do exapansion within braces.
#define BK_STRING_TOKENIZE_SIMPLE	(BK_STRING_TOKENIZE_MULTISPLIT)
#define BK_STRING_TOKENIZE_NORMAL	(BK_STRING_TOKENIZE_MULTISPLIT|BK_STRING_TOKENIZE_DOUBLEQUOTE)
#define BK_STRING_TOKENIZE_CONFIG	(BK_STRING_TOKENIZE_DOUBLEQUOTE)
extern void bk_string_tokenize_destroy(bk_s B, char **tokenized);
extern char *bk_string_printbuf(bk_s B, const char *intro, const char *prefix, const bk_vptr *buf, bk_flags flags);
extern char *bk_string_rip(bk_s B, char *string, const char *terminators, bk_flags flags);
extern char *bk_string_quote(bk_s B, const char *src, const char *needquote, bk_flags flags);
#define BK_STRING_QUOTE_NONPRINT	0x01	///< During bk_string_quote: Quote non-printable
#define BK_STRING_QUOTE_NULLOK		0x02	///< During bk_string_quote: Output NULL
#define BK_NULLSTR			"NULL"  ///< During bk_string_quote: String rep of NULL
extern ssize_t bk_strnlen(bk_s B, const char *s, size_t max);
extern char *bk_strndup(bk_s B, const char *s, size_t len);
extern char *bk_strnstr(bk_s B, const char *haystack, const char *needle, size_t len);
extern void *bk_memmem(bk_s B, const void *haystack, size_t haystacklen, const void *needle, size_t needlelen);
extern char *bk_strstrn(bk_s B, const char *haystack, const char *needle, size_t len);
extern int bk_strnspacecmp(bk_s B, const char *s1, const char *s2, u_int len1, u_int len2);
extern void *bk_memrchr(bk_s B, const void *buffer, int character, size_t len);
extern int bk_memdiff(bk_s B, const void *b1, const void *b2, u_int len1, u_int len2);
#define BK_STRING_ALLOC_SPRINTF_FLAG_STINGY_MEMORY	0x1 ///< Take more time to return use as little memory as possible.
extern char *bk_string_alloc_vsprintf(bk_s B, u_int chunk, bk_flags flags, const char *fmt, va_list ap) __attribute__ ((format (printf, 4, 0)));
extern int bk_vstr_cat(bk_s B,  bk_flags flags, bk_vstr *dest, const char *src_fmt, ...) __attribute__ ((format (printf, 4, 5)));
#define BK_VSTR_CAT_FLAG_STINGY_MEMORY		0x1 ///< Take more time; use less memory
extern int bk_string_unique_string(bk_s B, char *buf, u_int len, bk_flags flags);
extern void *bk_mempbrk(bk_s B, bk_vptr *s, bk_vptr *acceptset);
extern bk_str_registry_t bk_string_registry_init(bk_s B);
extern void bk_string_registry_destroy(bk_s B, bk_str_registry_t handle);
extern bk_str_id_t bk_string_registry_insert(bk_s B, bk_str_registry_t handle, const char *str, bk_str_id_t id, bk_flags flags);
#define BK_STR_REGISTRY_FLAG_COPY_STR	0x1	///< strdup(3) this string instead of just copying the pointer.
#define BK_STR_REGISTRY_FLAG_FORCE	0x2	///< force insertion even if string present or non-zero id is missing
extern const char *bk_string_registry_register_by_id(bk_s B, bk_str_registry_t handle, bk_str_id_t id, bk_flags flags);
extern int bk_string_registry_delete_str(bk_s B, bk_str_registry_t handle, const char *str, bk_flags flags);
extern int bk_string_registry_delete_id(bk_s B, bk_str_registry_t handle, bk_str_id_t id, bk_flags flags);
extern bk_str_id_t bk_string_registry_idbystr(bk_s B, bk_str_registry_t handle, const char *str, bk_flags flags);
extern char *bk_string_expand(bk_s B, char *src, const dict_h kvht_vardb, const char **envdb, bk_flags flags);
#define BK_STRING_EXPAND_FREE 1
extern int bk_string_csv_quote(bk_s B, const char *in_str, int in_len, char *out_str, int out_len, const char *quote_str, bk_flags flags);

/* b_sprintf.c */
extern char *bk_string_alloc_sprintf(bk_s B, u_int chunk, bk_flags flags, const char *fmt, ...) __attribute__ ((format (printf, 4, 5)));



/* getbyfoo.c */
extern int bk_getprotobyfoo(bk_s B, const char *protostr, struct protoent **ip, struct bk_netinfo *bni, bk_flags flags);
#define BK_GETPROTOBYFOO_FORCE_LOOKUP	0x1	///< Do lookup even if argumnet suggests otherwise.
extern void bk_protoent_destroy(bk_s B, struct protoent *p);
extern int bk_getservbyfoo(bk_s B, char *servstr, const char *iproto, struct servent **is, struct bk_netinfo *bni, bk_flags flags);
#define BK_GETSERVBYFOO_FORCE_LOOKUP	0x1	///< Do lookup even if argumnet suggests otherwise.
extern void bk_servent_destroy(bk_s B, struct servent *s);
void *bk_gethostbyfoo(bk_s B, char *name, int family, struct bk_netinfo *bni, struct bk_run *br, bk_gethostbyfoo_callback_f callback, void *args, bk_flags user_flags);
#define BK_GETHOSTBYFOO_FLAG_FQDN	0x1	///< Get the FQDN
extern void bk_destroy_hostent(bk_s B, struct hostent *h);
extern void bk_gethostbyfoo_abort(bk_s B, void *opaque);
extern int bk_ether_aton(bk_s B, const char *ether_addr_str, struct ether_addr *ether_addr, bk_flags flags);
extern int bk_ether_ntoa(bk_s B, struct ether_addr *ether_addr, char *ether_addr_str, bk_flags flags);
#define BK_ETHER_NTOA_FLAG_UPPER 0x1


/* b_netinfo.c */
extern struct bk_netinfo *bk_netinfo_create(bk_s B);
extern void bk_netinfo_destroy(bk_s B, struct bk_netinfo *bni);
extern int bk_netinfo_add_addr(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *bna, struct bk_netaddr **obna);
extern int bk_netinfo_delete_addr(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *ibna, struct bk_netaddr **obna);
extern int bk_netinfo_set_primary_address(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *bna);
extern int bk_netinfo_reset_primary_address(bk_s B, struct bk_netinfo *bni);
extern struct bk_netinfo *bk_netinfo_clone(bk_s B, struct bk_netinfo *obni);
extern int bk_netinfo_update_servent(bk_s B, struct bk_netinfo *bni, struct servent *s);
extern int bk_netinfo_update_protoent(bk_s B, struct bk_netinfo *bni, struct protoent *p);
extern int bk_netinfo_update_hostent(bk_s B, struct bk_netinfo *bni, struct hostent *h);
extern struct bk_netaddr *bk_netinfo_get_addr(bk_s B, struct bk_netinfo *bni);
extern int bk_netinfo_to_sockaddr(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *bna, bk_netaddr_type_e type, bk_sockaddr_t *bs, bk_flags flags);
#define BK_NETINFO2SOCKADDR_FLAG_FUZZY_ANY	0x1 ///< Allow bad address information to indicate ANY addresss.
extern struct bk_netinfo *bk_netinfo_from_socket(bk_s B, int s, int proto, bk_socket_side_e side);
extern const char *bk_netinfo_info(bk_s B, struct bk_netinfo *bni);
extern struct bk_netaddr * bk_netinfo_advance_primary_address(bk_s B, struct bk_netinfo *bni);
extern int bk_netinfo_addr_type(bk_s B, struct bk_netinfo *bni, bk_flags flags);


/* b_netaddr.c */
extern struct bk_netaddr *bk_netaddr_create(bk_s B);
extern void bk_netaddr_destroy(bk_s B, struct bk_netaddr *bna);
extern struct bk_netaddr *bk_netaddr_user(bk_s B, bk_netaddr_type_e type, void *addr, int len, bk_flags flags);
extern struct bk_netaddr *bk_netaddr_addrdup (bk_s B, bk_netaddr_type_e type, void *addr, bk_flags flags);
extern struct bk_netaddr *bk_netaddr_clone (bk_s B, struct bk_netaddr *obna);
extern bk_netaddr_type_e bk_netaddr_af2nat(bk_s B, int af);
extern int bk_netaddr_nat2af(bk_s B, int type);


/* b_servinfo.c */
extern struct bk_servinfo *bk_servinfo_serventdup (bk_s B, struct servent *s);
extern void bk_servinfo_destroy (bk_s B,struct bk_servinfo *bsi);
extern struct bk_servinfo *bk_servinfo_clone (bk_s B, struct bk_servinfo *obsi);

/* b_protoinfo.c */
extern struct bk_protoinfo *bk_protoinfo_create(bk_s B, int proto, const char *proto_str, bk_flags flags);
extern void bk_protoinfo_destroy(bk_s B, struct bk_protoinfo *bpi);
extern struct bk_protoinfo *bk_protoinfo_protoentdup (bk_s B, struct protoent *p);
extern struct bk_protoinfo *bk_protoinfo_clone (bk_s B, struct bk_protoinfo *obsi);



/*
 * These are flags jointly shared by all the various connection management
 * routines. Some are useful only when connecting, others are useful only
 * when receiving, still others are useful in both cases. They are all
 * grouped together because, while there are still so few of them, it's
 * fare more convientt.
 */
#define BK_NET_FLAG_ANY_ADDR		0x01	///< Bind to INADDR_ANY
#define BK_NET_FLAG_WANT_SSL		0x02	///< Use SSL (fail if not supported)
#define BK_NET_FLAG_STANDARD_UDP	0x04	///< Don't send baka preamble/syn thing (This option is no longer used, but maintained for backwards compat).
#define BK_NET_FLAG_BAKA_UDP		0x08	///< Use the deprecated BAKA UDP preamble thing.


/* b_netutils.c */
extern int bk_netutils_get_sa_len(bk_s B, struct sockaddr *sa);
extern int bk_parse_endpt_spec(bk_s B, const char *urlstr, char **hoststr, const char *defhoststr, char **servicestr,  const char *defservicestr, char **protostr, const char *defprotostr);
extern int bk_netutils_start_service(bk_s B, struct bk_run *run, const char *url, const char *defurl, bk_bag_callback_f callback, void *args, int backlog, bk_flags flags);
extern int bk_netutils_start_service_verbose(bk_s B, struct bk_run *run, const char *url, const char *defhoststr, const char *defservstr, const char *defprotostr, const char *securenets, bk_bag_callback_f callback, void *args, int backlog, const char *key_path, const char *cert_path, const char *ca_file, const char *dhparam_path, bk_flags ctx_flags, bk_flags flags);
extern int bk_netutils_make_conn(bk_s B, struct bk_run *run, const char *url, const char *defurl, u_long timeout, bk_bag_callback_f callback, void *args, bk_flags flags);
extern int bk_netutils_make_conn_verbose(bk_s B, struct bk_run *run, const char *rurl, const char *defrhost, const char *defrserv, const char *lurl, const char *deflhost, const char *deflserv, const char *defproto, u_long timeout, bk_bag_callback_f callback, void *args, const char *key_path, const char *cert_path, const char *ca_file, const char *dhparam_path, bk_flags ctx_flags, bk_flags flags );
extern int bk_netutils_commandeer_service(bk_s B, struct bk_run *run, int s, const char *securenets, bk_bag_callback_f callback, void *args, const char *key_path, const char *cert_path, const char *ca_file, const char *dhparam_path, bk_flags ctx_flags, bk_flags flags);
extern int bk_parse_endpt_no_defaults(bk_s B, const char *urlstr, char **hostname, char **servistr, char **protostr);
extern char *bk_netutils_gethostname(bk_s B);
extern char *bk_inet_ntoa(bk_s B, struct in_addr addr, char *buf);

/* b_addrgroup.c */
extern int bk_net_init(bk_s B, struct bk_run *run, struct bk_netinfo *local, struct bk_netinfo *remote, u_long timeout, bk_flags flags, bk_bag_callback_f callback, void *args, int backlog);
extern void bk_addrgroup_ref(bk_s B, struct bk_addrgroup *bag);
extern void bk_addrgroup_unref(bk_s B, struct bk_addrgroup *bag);
extern void bk_addrgroup_destroy(bk_s B,struct bk_addrgroup *bag);
extern int bk_addrgroup_get_server_socket(bk_s B, void *server_handle);
extern int bk_addrgroup_server_close(bk_s B, void *server_handle);
extern bk_addrgroup_state_e bk_net_init_sys_error(bk_s B, int lerrno);
extern int bk_addressgroup_suspend(bk_s B, struct bk_run *run, void *server_handle, bk_flags flags);
#define BK_ADDRESSGROUP_RESUME	1		///< Resume addressgroup instead of suspending it
extern int bk_addrgroup_addr_type(bk_s B, struct bk_addrgroup *bag, bk_flags flags);
extern int bk_addrgroup_network(bk_s B, struct bk_addrgroup *bag, bk_flags flags);


/* b_signal.c */
typedef void (*bk_sighandler_f)(int);
extern int bk_signal(bk_s B, int signo, bk_sighandler_f handler, bk_flags flags);
#define BK_SIGNAL_FLAG_RESTART	0x1		///< Restart syscall on signal.
extern int bk_signal_mgmt(bk_s B, int sig, bk_sighandler_f new, bk_sighandler_f *old, bk_flags flags);
extern void *bk_signal_set(bk_s B, int signo, bk_sighandler_f handler, bk_flags flags);
#define BK_SIGNAL_SET_SIGNAL_FLAG_FORCE		0x1 ///< Set this handler even if a non-default handler is in place.
extern void *bk_signal_set_alarm(bk_s B, u_int secs, bk_sighandler_f handler, bk_flags flags);
extern int bk_signal_reset(bk_s B, void *args, bk_flags flags);
extern int bk_signal_reset_alarm(bk_s B, void *args, bk_flags flags);
extern void bk_reaper(int signum);
#ifdef __INSURE__
void bk_sig_ign(int signal);
#endif /* __INSURE__ */




/**
 * Callback prototype for ioh relay. This is called once per read and once
 * one everything is shutdown. While the relay is active, the callback is
 * called when the data has been read but before it has been
 * written. During shutdown, indicated by a NULL data argument, the
 * read_ioh and write_ioh no longer have these meanings, but are supplied
 * for convenience.
 *
 *	@param B BAKA thread/global state.
 *	@param opaque Data supplied by the relay initiator (optional of course).
 *	@param read_ioh Where the data came from.
 *	@param flags Where the data is going.
 *	@param data The data to be relayed.
 *	@param flags Flags for future use.
 */
typedef void (*bk_relay_cb_f)(bk_s B, void *opaque, struct bk_ioh *read_ioh, struct bk_ioh *write_ioh, bk_vptr *data,  bk_flags flags);

/* b_relay.c */
extern int bk_relay_ioh(bk_s B, struct bk_ioh *ioh1, struct bk_ioh *ioh2, bk_relay_cb_f callback, void *opaque, struct bk_relay_ioh_stats *stats, struct bk_relay_cancel *brc, bk_flags flags);
#define BK_RELAY_IOH_DONE_AFTER_ONE_CLOSE	0x1 ///< Shut down relay after only one side has closed
#define BK_RELAY_IOH_DONTCLOSEFDS		0x2 ///< Don't actually close fds
#define BK_RELAY_IOH_NOSHUTDOWN			0x4 ///< Don't actually shutdown fds
extern int bk_relay_cancel(bk_s B, struct bk_relay_cancel *brc, bk_flags flags);

/* b_fileutils.c */
extern int bk_fileutils_modify_fd_flags(bk_s B, int fd, long flags, bk_fileutils_modify_fd_flags_action_e action);
extern void *bk_file_lock(bk_s B, const char *resource, bk_file_lock_type_e type, const char *admin_ext, const char *lock_ext, int *held, bk_flags flags);
extern int bk_file_unlock(bk_s B, void *opaque, bk_flags flags);
extern int bk_fileutils_match_extension(bk_s B, const char *path, const char * const *exts);
extern int bk_fileutils_is_true_pipe(bk_s B, int fd, bk_flags flags);
extern bk_vptr *bk_slurp(bk_s B, FILE *FH, int fd, const char *filename, int maxwastage, bk_flags flags);
#define BK_SLURP_NULL_TERMINATE			0x01 ///< Ask for returned data to be null terminated



/* b_time.c */
extern size_t bk_time_iso_format(bk_s B, char *str, size_t max, const struct timespec *timep, bk_flags *out_flagsp, bk_flags flags);
// "in" flags
#define BK_TIME_FORMAT_FLAG_NO_TZ		0x1 ///< Leave of the 'T' and 'Z' in the iso format
#define BK_TIME_FORMAT_NO_ERROR			0x2 ///< We don't hear about errors.
// "out" flags
#define BK_TIME_FORMAT_OUTFLAG_TRUNCATED	0x1 ///< Insufficient input space to save converted time.
extern int bk_time_iso_parse(bk_s B, const char *src, struct timespec *dst, bk_flags flags);
extern time_t bk_timegm(bk_s B, struct tm *timeptr, bk_flags flags);
#define BK_TIMEGM_FLAG_NORMALIZE	0x1	///< Normalize struct tm fields
extern int bk_time_duration_parse(bk_s B, const char *string, time_t *duration, bk_flags flags);

size_t bk_time_ntp_format(bk_s B, char *str, size_t max, struct timespec *timep, bk_flags flags);
extern int bk_time_ntp_parse(bk_s B, const char *src, struct timespec *dst, bk_flags flags);


/* b_url.c */
extern struct bk_url *bk_url_parse(bk_s B, const char *url_in, bk_url_parse_mode_e mode, bk_flags flags);
#define BK_URL_FLAG_STRICT_PARSE	0x1	///< Don't do BAKA fuzzy logic.
extern struct bk_url *bk_url_create(bk_s B);
extern void bk_url_destroy(bk_s B, struct bk_url *bu);
extern char *bk_url_unescape(bk_s B, const char *urlcomponent);
extern char *bk_url_unescape_len(bk_s B, const char *component, size_t len);
extern int bk_url_getparam(bk_s B, char **pathp, char * const *tokens, char **valuep);
extern struct bk_url_authority *bk_url_parse_authority(bk_s B, const char *auth_str, bk_flags flags);
extern void bk_url_authority_destroy(bk_s B, struct bk_url_authority *auth);
extern char *bk_url_reconstruct(bk_s B, struct bk_url *bu, bk_flags sections, bk_flags flags);


/* b_nvmap.c */
extern int bk_nvmap_name2value(bk_s B, struct bk_name_value_map *nvmap, const char *name);
extern const char *bk_nvmap_value2name(bk_s B, struct bk_name_value_map *nvmap, int val);
extern int bk_flags2value(bk_s B, struct bk_name_value_map *nvmap, const char *flags_str, const char *separator, bk_flags *output_flagsp, bk_flags flags);
extern char *bk_value2flags(bk_s B, struct bk_name_value_map *nvmap, bk_flags input_flags, const char *separator, bk_flags flags);


/* b_exec.c */

#define BK_EXEC_FLAG_SEARCH_PATH		0x1 ///< Search PATH for the process
#define BK_EXEC_FLAG_TOSS_STDOUT		0x2 ///< Duplicate stdout to /dev/null.
#define BK_EXEC_FLAG_TOSS_STDERR		0x4 ///< Duplicate stderr to /dev/null.
#define BK_EXEC_FLAG_USE_SUPPLIED_FDS		0x8 ///< Use the fd's supplied as copyu in args
#define BK_EXEC_FLAG_CLOSE_CHILD_DESC		0x10 ///< Close all extraneous deciptors in child
#define BK_EXEC_FLAG_STDERR_ON_STDOUT		0x20 ///< When creating pipe to child, stderr dups on stdout
#define BK_EXEC_FLAG_USE_PTY			0x40 ///< Use PTY instead of pipe
#define BK_EXEC_FLAG_CLOSE_UNNEEDED_STDINOUT	0x80 ///< Close unneeded stdin/stdout

#ifdef MISSING_PTHREAD_RWLOCK_INIT
/* lock initialization only required on platforms where rwlock_t is not a pointer (Darwin) */
extern int bk_envlock_init(bk_s B);
#endif /* MISSING_PTHREAD_RWLOCK_INIT */
extern pid_t bk_pipe_to_process(bk_s B, int *fdinp, int*fdoutp, bk_flags flags);
#define BK_PIPE_TO_PROCESS_FLAG_CLOSE_EXTRANEOUS_DESC	0x1
#define BK_PIPE_FLAG_STDERR_ON_STDOUT		0x2 ///< When creating pipe to child, stderr dups on stdout
#define BK_PIPE_FLAG_USE_PTY			0x4 ///< Use PTY instead of pipe
#define BK_PIPE_FLAG_CLOSE_UNNEEDED_STDINOUT	0x8 ///< Close stdin/stdout in child if not needed.
extern int bk_fork_exec(bk_s B, const char *proc, char *const *args, char *const *env, bk_flags flags);
extern int bk_exec(bk_s B, const char *proc, char *const *args, char *const *env, bk_flags flags);
extern char *bk_search_path(bk_s B, const char *proc, const char *path, int mode, bk_flags flags);
extern int bk_exec_cmd(bk_s B, const char *cmd, char *const *env, bk_flags flags);
extern int bk_exec_cmd_tokenize(bk_s B, const char *cmd, char *const *env, u_int limit, const char *spliton, const dict_h kvht_vardb, const char **variabledb, bk_flags tokenize_flags, bk_flags flags);
extern pid_t bk_pipe_to_exec(bk_s B, int *fdinp, int *fdoutp, const char *proc, char *const *args, char *const *env, bk_flags flags);
extern pid_t bk_pipe_to_cmd_tokenize(bk_s B, int *fdinp, int *fdoutp, const char *cmd, char *const *env, u_int limit, const char *spliton, const dict_h kvht_vardb, const char **variabledb, bk_flags tokenize_flags, bk_flags flags);
extern pid_t bk_pipe_to_cmd(bk_s B, int *fdin,int *fdout, const char *cmd, char *const *env, bk_flags flags);
extern int bk_setenv_with_putenv(bk_s B, const char *key, const char *value, int overwrite);
extern const char *bk_getenv(bk_s B, const char *key);


/* b_rand.c */
extern struct bk_truerandinfo *bk_truerand_init(bk_s B, int reinitbits, bk_flags flags);
extern void bk_truerand_destroy(bk_s B, struct bk_truerandinfo *R);
extern u_int32_t bk_truerand_getword(bk_s B, struct bk_truerandinfo *R, u_int32_t *co, bk_flags flags);
extern int bk_truerand_getbuf(bk_s B, struct bk_truerandinfo *R, u_char *buf, u_int len, bk_flags flags);


/* b_skid.c */
typedef void (*bk_skid_cb)(bk_s B, struct bk_ioh *ioh, void *opaque, u_int state, char *failurereason);
extern int bk_skid_authenticate(bk_s B, struct bk_ioh *ioh, bk_vptr *myname, bk_vptr *hisname, bk_vptr *key, struct bk_config *bs_hisnamekeylist, bk_skid_cb done, void *opaque, bk_flags flags);
extern void bk_skid_destroy(bk_s B, struct bk_skid *bs, bk_flags flags);
#define BK_SKID_NORESTORE	0x01		///< Do not restore IOH parameters during destroy


/* b_listnum.c */
extern struct bk_listnum_main *bk_listnum_create(bk_s B, bk_flags flags);
extern struct bk_listnum_head *bk_listnum_get(bk_s B, struct bk_listnum_main *main, u_int number, bk_flags flags);
extern void bk_listnum_destroy(bk_s B, struct bk_listnum_main *main);
extern struct bk_listnum_head *bk_listnum_next(bk_s B, struct bk_listnum_main *main, struct bk_listnum_head *prev, bk_flags flags);
#define BK_LISTNUM_PRUNE_EMPTY	0x01		///< Prune empty list nodes and search again instead of returning


/* b_md5.c */
extern void bk_MD5Init (bk_s B, bk_MD5_CTX *mdContext);
extern void bk_MD5Update (bk_s B, bk_MD5_CTX *mdContext, const unsigned char *inBuf, unsigned int inLen);
extern void bk_MD5Final (bk_s B, bk_MD5_CTX *mdContext);
extern int bk_MD5_extract_printable(bk_s B, char *str, bk_MD5_CTX *ctx, bk_flags flags);

/* b_stdsock.c */
extern int bk_stdsock_multicast(bk_s B, int fd, u_char ttl, struct bk_netaddr *maddrgroup, bk_flags flags);
#define BK_MULTICAST_WANTLOOP		0x01	///< Want multicasts to loop to local machine
#define BK_MULTICAST_NOJOIN		0x02	///< Do not wish to join multicast group
extern int bk_stdsock_broadcast(bk_s B, int fd, bk_flags flags);

/* b_child.c */
extern struct bk_child *bk_child_icreate(bk_s B, bk_flags flags);
extern void bk_child_iclean(bk_s B, struct bk_child *bchild, int specialchild, bk_flags flags);
extern void bk_child_idestroy(bk_s B, struct bk_child *bchild, bk_flags flags);
extern int bk_child_istart(bk_s B, struct bk_child *bchild, void (*cc_callback)(bk_s B, void *opaque, int childid, bk_childstate_e state, u_int status), void *cc_opaque, int *fds[], bk_flags flags);
#define BK_CHILD_WANTRPIPE	0x01		///< Want a read pipe
#define BK_CHILD_WANTWPIPE	0x02		///< Want a write pipe
#define BK_CHILD_WANTEPIPE	0x04		///< Want a write error pipe
#define BK_CHILD_WANTEASW	0x08		///< Want error pipe on stdout
#define BK_CHILD_NOTIFYSTOP	0x10		///< Want stop notification
extern void bk_child_isigfun(bk_s B, struct bk_run *run, int signum, void *opaque);

/* b_thread.c */
extern int bk_atomic_addition(bk_s B, struct bk_atomic_cntr *bac, int delta, int *result, bk_flags flags);
extern int bk_atomic_add_init(bk_s B, struct bk_atomic_cntr *bac, int start, bk_flags flags);
#ifdef BK_USING_PTHREADS
extern int bk_pthread_mutex_lock(bk_s B, struct bk_run *run, pthread_mutex_t *mutex, bk_flags flags);
#endif /* BK_USING_PTHREADS */


/* b_vault.c */
extern bk_vault_t bk_vault_create(bk_s B, int table_entries, int bucket_entries, bk_flags flags);
#define bk_vault_destroy(h)		ht_destroy(h)
#define bk_vault_insert(h,o)		ht_insert((h),(o))
#define bk_vault_insert_uniq(h,n,o)	ht_insert_uniq((h),(n),(o))
#define bk_vault_append(h,o)		ht_append((h),(o))
#define bk_vault_append_uniq(h,n,o)	ht_append_uniq((h),(n),(o))
#define bk_vault_search(h,k)		ht_search((h),(k))
#define bk_vault_delete(h,o)		ht_delete((h),(o))
#define bk_vault_minimum(h)		ht_minimum(h)
#define bk_vault_maximum(h)		ht_maximum(h)
#define bk_vault_successor(h,o)		ht_successor((h),(o))
#define bk_vault_predecessor(h,o)	ht_predecessor((h),(o))
#define bk_vault_iterate(h,d)		ht_iterate((h),(d))
#define bk_vault_nextobj(h,i)		ht_nextobj((h),(i))
#define bk_vault_iterate_done(h,i)	ht_iterate_done((h),(i))
#define bk_vault_error_reason(h,i)	ht_error_reason((h),(i))


/* b_stats.c */
extern struct bk_stat_list *bk_stat_create(bk_s B, bk_flags flags);
extern void bk_stat_destroy(bk_s B, struct bk_stat_list *blist);
extern struct bk_stat_node *bk_stat_nodelist_create(bk_s B, struct bk_stat_list *blist, const char *name1, const char *name2, bk_flags flags);
extern struct bk_stat_node *bk_stat_node_create(bk_s B, const char *name1, const char *name2, bk_flags flags);
#define BK_STATS_NO_LOCKS_NEEDED		0x100
extern void bk_stat_node_destroy(bk_s B, struct bk_stat_node *bnode);
extern void bk_stat_start(bk_s B, struct bk_stat_list *blist, const char *name1, const char *name2, bk_flags flags);
extern void bk_stat_end(bk_s B, struct bk_stat_list *blist, const char *name1, const char *name2, bk_flags flags);
extern void bk_stat_node_start(bk_s B, struct bk_stat_node *bnode, bk_flags flags);
//#define BK_STATS_NO_LOCKS_NEEDED		0x100
extern void bk_stat_node_end(bk_s B, struct bk_stat_node *bnode, bk_flags flags);
//#define BK_STATS_NO_LOCKS_NEEDED		0x100
extern char *bk_stat_dump(bk_s B, struct bk_stat_list *blist, bk_flags flags);
#define BK_STAT_DUMP_HTML	0x1	///< Request HTML string instead of CSV
//#define BK_STATS_NO_LOCKS_NEEDED		0x100
extern void bk_stat_info(bk_s B, struct bk_stat_list *blist, const char *name1, const char *name2, u_quad_t *minusec, u_quad_t *maxusec, u_quad_t *sumutime, u_int *count, bk_flags flags);
//#define BK_STATS_NO_LOCKS_NEEDED		0x100
extern void bk_stat_node_info(bk_s B, struct bk_stat_node *bnode, u_quad_t *minusec, u_quad_t *maxusec, u_quad_t *sumutime, u_int *count, bk_flags flags);
//#define BK_STATS_NO_LOCKS_NEEDED		0x100
extern void bk_stat_add(bk_s B, struct bk_stat_list *blist, const char *name1, const char *name2, u_quad_t usec, bk_flags flags);
//#define BK_STATS_NO_LOCKS_NEEDED		0x100
extern void bk_stat_node_add(bk_s B, struct bk_stat_node *bnode, u_quad_t usec, bk_flags flags);
//#define BK_STATS_NO_LOCKS_NEEDED		0x100

/* b_thread.c */
extern struct bk_threadlist *bk_threadlist_create(bk_s B, bk_flags flags);
extern void bk_threadlist_destroy(bk_s B, struct bk_threadlist *tlist, bk_flags flags);
extern struct bk_threadnode *bk_threadnode_create(bk_s B, const char *threadname, bk_flags flags);
extern void bk_threadnode_destroy(bk_s B, struct bk_threadnode *tnode, bk_flags flags);
#define BK_THREADNODE_DESTROY_DESTROYSELF	0x1
#ifdef BK_USING_PTHREADS
extern pthread_t *bk_thread_create(bk_s B, struct bk_threadlist *tlist, const char *threadname, void *(*start)(bk_s B, void *opaque), void *opaque, bk_flags flags);
extern void bk_thread_tnode_done(bk_s B, struct bk_threadlist *tlist, struct bk_threadnode *tnode, bk_flags flags);
extern void bk_thread_kill_others(bk_s B, bk_flags flags);
extern void *bk_monitor_memory_thread(bk_s B, void *opaque);
extern void *bk_monitor_int_thread(bk_s B, void *opaque);
#endif /* BK_USING_PTHREADS */


/* b_ringbuf.c */
extern struct bk_ring *bk_ring_create(bk_s B, u_int size, bk_flags flags);
extern void bk_ring_destroy(bk_s B, struct bk_ring *ring, bk_flags flags);
//#define BK_RING_WAIT			0x1000	///< Wait for ring to drain of data
extern int bk_ring_write(bk_s B, struct bk_ring *ring, void *opaque, bk_flags flags);
#define BK_RING_WAIT			0x1000	///< Wait for ring to have room to write
#define BK_RING_NOLOCK			0x2000	///< Multiple write locking not required
extern void volatile *bk_ring_read(bk_s B, struct bk_ring *ring, bk_flags flags);
//#define BK_RING_WAIT			0x1000	///< Wait for ring to have room to read
//#define BK_RING_NOLOCK		0x2000	///< Multiple read locking not required
extern int bk_ring_length(bk_s B, struct bk_ring *ring, bk_flags flags);

/* b_vptr.c */
extern int bk_vptr_append(bk_s B, bk_vptr *dp, const bk_vptr *sp);
extern int bk_vptr_trimleft(bk_s B, bk_vptr *vptr, const void *ptr, bk_flags flags);
#define BK_VPTR_FLAG_NO_RESIZE		0x01	///< Don't realloc memory
extern int bk_vptr_ntrimleft(bk_s B, bk_vptr *vptr, size_t n);
extern int bk_vptr_cmp(bk_vptr *v1, bk_vptr *v2);


/* b_ringdir.c */
typedef void *bk_ringdir_t;

enum bk_ringdir_chkpnt_actions
{
  BkRingDirChkpntActionChkpnt,			///< Save checkpoint
  BkRingDirChkpntActionRecover,			///< Recover checkpoint
  BkRingDirChkpntActionDelete,			///< Delete checkpoint state
};



enum bk_ringdir_callback_source
{
  BkRingDirCallbackSourceInit,			///< Callback called from init function.
  BkRingDirCallbackSourceRotate,		///< Callback called from rotate function.
  BkRingDirCallbackSourceDestroy,		///< Callback called from destroy function.
};


/**
 * This call back API for the ring directory
 */
struct bk_ringdir_callbacks
{
  /**
   *
   * Allocate and initialize private data for this particular
   * implementation of a ring directory. The valued returnd from this
   * function will the @a opaque argument in all the other functions in the
   * API. This function is OPTIONAL but if it declared then it must return
   * a non NULL value for success even if there is no private state
   * created. Using (void *)1 works fine, but irriates the heck out of
   * memory checkers. We suggest returning the function name (ie function
   * pointer) instead. For your convenience, you are passed all the values
   * which the caller passed to @a bk_ringdir_init(), you may use them how
   * you will. The @a private data in this function is intended to give the
   * plugin author a mechanism for permitting the caller to pass
   * information from the calling context to the plugin init() call in case
   * it is required for completing some aspect of the init call. It is
   * <emphasis>not</emphasis> the @a opaque data of all the rest of the
   * prototypes in this API. That @a opqque is the return value from this
   * function.
   *
   *	@param B BAKA thread/global state.
   *	@param directory The "path" to the ring directory;
   *	@param rotate_size How big a file may grow before rotation.
   *	@param max_num_files How many files in the ring dir before reuse.
   *	@param file_name_pattern The sprintf-like pattern for creating names in the directory.
   *	@param private Private data for this function from the caller.
   *	@param flags Flags for future use.
   *
   * BK_RINGDIR_FLAG_DO_NOT_CREATE: If you passed this flag, do not create
   * the directory if it doesn't exist; return an error instead.
   *
   *	@return <i>NULL</i> on failure.<br>
   *	@return <i>private_data</i> on success.
   */
  void *(*brc_init)(bk_s B, const char *directory, off_t rotate_size, u_int32_t max_num_files, const char *file_name_pattern, void *private, bk_flags flags);



  /**
   * Create a @a bk_ring_directory
   *
   *	@param B BAKA thread/global state.
   *	@param opaque Your private data.
   *	@param directory The directory you may be asked to nuke.
   *	@param flags Flags for future use.
   *
   * BK_RINGDIR_FLAG_NUKE_DIR_ON_DESTROY: If you passed this flag, the
   * caller is requesting that you destroy the directory. You are not
   * obliged to honor this, but it's a good idea.
   */
  void (*brc_destroy)(bk_s B, void *opaque, const char *directory, bk_flags flags);



  /**
   * Return the "size" of the current file. You may define size in any
   * manner you chose. The only requirement is that the value returned from
   * this function be greater than or equal to that of @a rotate_size (from
   * the init function) when you desire rotation to occur. The macro
   * BK_RINGDIR_GET_SIZE_MAX is provided as a convenience value for the
   * largest legal return value from this function.
   *
   *	@param B BAKA thread/global state.
   *	@param opaque Your private data.
   *	@param filename The filename you are getting the size of
   *	@param filenum The number/index of the file you are getting the size of
   *	@param flags Flags for future use.
   *	@return <i>BK_RINGDIR_GET_SIZE_ERROR</i> on failure.<br>
   *	@return <i>non-negative</i> on success.
   */
  off_t (*brc_get_size)(bk_s B, void *opaque, const char *filename, u_int filenum, bk_flags flags);



  /**
   * Open a new file by whatever method is appropriate for your implementation.
   *
   *	@param B BAKA thread/global state.
   *	@param opaque Your private data.
   *	@param filename The filename to open
   *	@param filenum Number/index of the file to open
   *	@param source The ring direction action which triggerd this callback
   *	@param flags Flags for future use.
   *
   * BK_RINGDIR_FLAG_OPEN_APPEND: If you are passed this flag you should
   * open the file in append mode (ie we are starting up from a
   * checkpointed location).
   *
   *	@return <i>-1</i> on failure.<br>
   *	@return <i>0</i> on success.
   */
  int (*brc_open)(bk_s B, void *opaque, const char *filename, u_int filenum, enum bk_ringdir_callback_source source, bk_flags flags);



  /**
   * Close a file in the manner consistent with your ringdir implementation.
   *
   *	@param B BAKA thread/global state.
   *	@param opaque Your private data.
   *	@param filename The filename to close
   *	@param filenum Number/index of the file to close
   *	@param source The ring direction action which triggerd this callback
   *	@param flags Flags for future use.
   *	@return <i>-1</i> on failure.<br>
   *	@return <i>0</i> on success.
   */
  int (*brc_close)(bk_s B, void *opaque, const char *filename, u_int filenum, enum bk_ringdir_callback_source source, bk_flags flags);



  /**
   * Remove a file in the manner consistent with your ringdir implementation.
   *
   *	@param B BAKA thread/global state.
   *	@param opaque Your private data.
   *	@param filename The filename to close
   *	@param filenum Number/index of the file to close
   *	@param source The ring direction action which triggerd this callback
   *	@param flags Flags for future use.
   *	@return <i>-1</i> on failure.<br>
   *	@return <i>0</i> on success.
   */
  int (*brc_unlink)(bk_s B, void *opaque, const char *filename, u_int filenum, enum bk_ringdir_callback_source source, bk_flags flags);



  /**
   * Do checkpointing managment. You will be called with one of the actions:
   *	BkRingDirChkpntActionChkpnt:	Save value to checkpoint state "file"
   *	BkRingDirChkpntActionRecover:	Recover checkpoint value from state "file"
   *	BkRingDirChkpntActionDelete:	Delete the checkpoint state "file"
   *
   * The ring directory name and the file name pattern are supplied to you
   * for your convience as your checkpoint key will almost certainly need
   * to be some function of these to values.
   *
   * The value of @a valuep depends on the action. If saving then *valuep
   * is the copy in value to save. If recoving then *valuep is the copy out
   * value you need to update. If deleting, then @a valuep is NULL.
   *
   *
   *	@param B BAKA thread/global state.
   *	@param opaque Your private data.
   *	@param action The action to take (described above).
   *	@param directory The directory you may be asked to nuke.
   *	@param pattern The file pattern.
   *	@param valuep The file pattern.
   *	@param source The ring direction action which triggerd this callback
   *	@param flags Flags for future use.
   *	@return <i>-1</i> on failure.<br>
   *	@return <i>0</i> on success.
   */
  int (*brc_chkpnt)(bk_s B, void *opaque, enum bk_ringdir_chkpnt_actions action, const char *directory, const char *pattern, u_int32_t *valuep, enum bk_ringdir_callback_source source, bk_flags flags);


  /**
   *  Check for the existence of a "file" by whatever means are appropriate for your implementation.
   *
   *	@param B BAKA thread/global state.
   *	@param opaque Your private data.
   *	@param filename The filename to open
   *	@param filenum Number/index of the file to open
   *	@param flags Flags for future use.
   *	@return <i>-1</i> on failure.<br>
   *	@return <i>0</i> if filename does not exist.
   *	@return <i>1</i> if filename exists.
   */
  int (*brc_exists)(bk_s B, void *opaque, const char *filename, int filenum, bk_flags flags);
} bk_ringdir_standard_callbacks;


/* There is only one set of flags for all of the ring directory functions */
#define BK_RINGDIR_FLAG_DO_NOT_CREATE		0x1 ///< Do not create directory if it doesn't exist
#define BK_RINGDIR_FLAG_NUKE_DIR_ON_DESTROY	0x2 ///< Empty directory during destroy. Ask callback to nuke it.
#define BK_RINGDIR_FLAG_NO_NUKE			0x4 ///< Override the above flags
#define BK_RINGDIR_FLAG_NO_CONTINUE		0x8 ///< Do not continue from old check point
#define BK_RINGDIR_FLAG_NO_CHECKPOINT		0x10 ///< Do not bother check pointing.
#define BK_RINGDIR_FLAG_FORCE_ROTATE		0x20 ///< Rotate regardless of "size"
#define BK_RINGDIR_FLAG_OPEN_APPEND		0x20 ///< Rotate regardless of "size"
#define BK_RINGDIR_FLAG_CANT_APPEND		0x40 ///< The "file" type does not support appending
#define BK_RINGDIR_FLAG_ROTATION_OCCURED	0x80 ///< A "file" rotation occured on the last call to rotate().
#define BK_RINGDIR_FLAG_EWARN			0x100 ///< Errors as warnings
#define BK_RINGDIR_FLAG_CREATE_NOWRITE		0x200 ///< The init function should not attempt to update on-disk information
#define BK_RINGDIR_FLAG_INTERNAL		0x400 ///< Not for public consumption

#define BK_RINGDIR_GET_SIZE_ERROR	UINT32_MAX
#define BK_RINGDIR_GET_SIZE_MAX		(BK_RINGDIR_GET_SIZE_ERROR - 1)

extern bk_ringdir_t bk_ringdir_init(bk_s B, const char *directory, off_t rotate_size, u_int32_t max_num_files, const char *file_name_pattern, void *private, struct bk_ringdir_callbacks *callbacks, bk_flags flags);
extern bk_ringdir_t bk_ringdir_create(bk_s B, const char *directory, off_t rotate_size, u_int32_t max_num_files, const char *pattern, struct bk_ringdir_callbacks *callbacks, bk_flags flags); /* Typically not needed */
extern void bk_ringdir_idestroy(bk_s B, bk_ringdir_t brd); /* Only needed if you create */
extern void bk_ringdir_destroy(bk_s B, bk_ringdir_t brdh, bk_flags flags);
extern int bk_ringdir_rotate(bk_s B, bk_ringdir_t brdh, u_int estimate_size_increment, bk_flags flags);
extern void *bk_ringdir_get_private_data(bk_s B, bk_ringdir_t brdh, bk_flags flags);
extern int bk_ringdir_did_rotate_occur(bk_s B, bk_ringdir_t brdh, bk_flags flags);
extern char *bk_ringdir_filename_oldest(bk_s B, bk_ringdir_t brdh, u_int *filenum, bk_flags flags);
extern char *bk_ringdir_filename_successor(bk_s B, bk_ringdir_t brdh, const char *filename, u_int *nextnum, bk_flags flags);
extern char *bk_ringdir_filename_current(bk_s B, bk_ringdir_t brdh, u_int *filenum, bk_flags flags);
extern char *bk_ringdir_filename_predecessor(bk_s B, bk_ringdir_t brdh, const char *filename, u_int *prevnum, bk_flags flags);
extern char *bk_ringdir_create_file_name(bk_s B, bk_ringdir_t brdh, u_int32_t cnt, bk_flags flags);
extern const char *bk_ringdir_getpattern(bk_s B, bk_ringdir_t brdh);
#define BK_RINGDIR_FILENAME_ITERATE_FLAG_FREE	0x1 ///< For successor/predecessor free the input filename.

extern void *bk_ringdir_standard_init(bk_s B, const char *directory, off_t rotate_size, u_int32_t max_num_files, const char *file_name_pattern, void *private, bk_flags flags);
extern void bk_ringdir_standard_destroy(bk_s B, void *opaque, const char *directory, bk_flags flags);
extern off_t bk_ringdir_standard_get_size(bk_s B, void *opaque, const char *filename, u_int filenum, bk_flags flags);
extern int bk_ringdir_standard_open(bk_s B, void *opaque, const char *filename, u_int filenum, enum bk_ringdir_callback_source source, bk_flags flags);
extern int bk_ringdir_standard_close(bk_s B, void *opaque, const char *filename, u_int filenum, enum bk_ringdir_callback_source source, bk_flags flags);
extern int bk_ringdir_standard_unlink(bk_s B, void *opaque, const char *filename, u_int filenum, enum bk_ringdir_callback_source source, bk_flags flags);
extern int bk_ringdir_standard_chkpnt(bk_s B, void *opaque, enum bk_ringdir_chkpnt_actions action, const char *directory, const char *pattern, u_int32_t *valuep, enum bk_ringdir_callback_source source, bk_flags flags);
extern int bk_ringdir_standard_exists(bk_s B, void *opaque, const char *filename, int filenum, bk_flags flags);
extern int bk_ringdir_standard_update_private_data(bk_s B, bk_ringdir_t brdh, void *opaque, bk_flags flags);
extern int bk_ringdir_standard_set_fd(bk_s B, bk_ringdir_t brdh, int fd, bk_flags flags);
extern int bk_ringdir_standard_get_fd(bk_s B, bk_ringdir_t brdh, bk_flags flags);
extern void *bk_ringdir_standard_get_fh(bk_s B, bk_ringdir_t brdh, bk_flags flags);
extern int bk_ringdir_standard_set_fh(bk_s B, bk_ringdir_t brdh, void *fh, off_t (*ftell)(void *), bk_flags flags);
extern void *bk_ringdir_standard_get_private_data(bk_s B, bk_ringdir_t brdh, bk_flags flags);
extern int bk_ringdir_standard_update_private_data_by_standard(bk_s B, void *brsh, void *opaque, bk_flags flags);
extern void *bk_ringdir_standard_get_private_data_by_standard(bk_s B, void *brsh, bk_flags flags);
extern int bk_ringdir_split_pattern(bk_s B, const char *path, char **dir_namep, char **patternp, bk_flags flags);
extern int bk_ringdir_get_status(bk_s B, bk_ringdir_t brdh, u_int32_t *current, u_int32_t *oldest, u_int32_t *num_filesp, u_int32_t *max, bk_flags flags);



/* b_realloc.c */
#if defined(__INSURE__) && !defined(BK_NO_MALLOC_WRAP)
extern int bk_malloc_wrap_init(bk_flags flags);
extern void bk_malloc_wrap_destroy(bk_flags flags);
extern void bk_malloc_wrap_destroy(bk_flags flags);
extern void *bk_malloc_wrapper(size_t size);
extern void *bk_calloc_wrapper(size_t nmemb, size_t size);
extern void *bk_realloc_wrapper(void *buf, size_t size);
extern void bk_free_wrapper(void *buf);
extern char *bk_strdup_wrapper(const char *str);
extern char *bk_strndup_wrapper(const char *str, size_t n);

#endif /* defined(__INSURE__) && !defined(BK_NO_MALLOC_WRAP) */

/* b_bigint.c */

/**
 * @name bk_bigint
 *
 * Structure for handling u_ints with overflow protection
 */
struct bk_bigint
{
  u_int		bb_cur_uint;	///< The value of the last u_int
  u_int64_t	bb_curval;	///< The actual value being accumulated
};
extern int bk_bigint_accumulate(bk_s B, struct bk_bigint *bb, u_int val, bk_flags flags);
extern u_int64_t bk_bigint_value(bk_s B, struct bk_bigint *bb, bk_flags flags);
extern int bk_bigint_init(bk_s B, struct bk_bigint *bb, bk_flags flags);

/* mt19937 */
extern struct mt_state *mt19937_truerand_init(void);
extern struct mt_state *mt19937_init_genrand64(unsigned long long myseed);
extern struct mt_state *mt19937_init_by_array64(unsigned long long init_key[], unsigned long long key_length);
extern unsigned long long mt19937_genrand64_int64(struct mt_state *mts);
extern long long mt19937_genrand64_int63(struct mt_state *mts);
extern double mt19937_genrand64_real1(struct mt_state *mts);
extern double mt19937_genrand64_real2(struct mt_state *mts);
extern double mt19937_genrand64_real3(struct mt_state *mts);
extern void mt19937_destroy(struct mt_state *mts);

/* b_shmipc.c */
typedef enum
{
  BkShmIpcCreateSuccess=0,			///< No particular error
  BkShmIpcCreateFatal=1,			///< Probably will not succeed if attempted again
  BkShmIpcCreateStale=2,			///< Could have failed due to stale shared memory
  BkShmIpcCreateTimeout=3,			///< Failed due to timeout
} bk_shmipc_failure_e;
extern struct bk_shmipc *bk_shmipc_create(bk_s B, const char *name, u_int timeoutus, u_int initus, u_int spinus, u_int size, u_int mode, bk_shmipc_failure_e *failure_reason, bk_flags flags);
#define BK_SHMIPC_RDONLY	0x01		///< Read only activity
#define BK_SHMIPC_WRONLY	0x02		///< Write only activity
extern void bk_shmipc_destroy(bk_s B, struct bk_shmipc *bsi, bk_flags flags);
extern ssize_t bk_shmipc_write(bk_s B, struct bk_shmipc *bsi, void *data, size_t len, u_int timeoutus, bk_flags flags);
#define BK_SHMIPC_NOBLOCK	0x01		///< Do not block
#define BK_SHMIPC_WRITEALL	0x02		///< Do not succeed without writing everything
#define BK_SHMIPC_DROP2BLOCK	0x04		///< Drop entire message if it will not fit in current free space
extern ssize_t bk_shmipc_read(bk_s B, struct bk_shmipc *bsi, void *data, size_t len, u_int timeout, bk_flags flags);
//#define BK_SHMIPC_NOBLOCK	0x01		///< Do not block
#define BK_SHMIPC_READALL	0x02		///< Do not succeed without reading everything
extern bk_vptr *bk_shmipc_readall(bk_s B, struct bk_shmipc *bsi, size_t maxbytes, u_int timeoutus, bk_flags flags);
//#define BK_SHMIPC_NOBLOCK	0x01		///< Do not block
extern int bk_shmipc_peek(bk_s B, struct bk_shmipc *bsi, size_t *bytesreadable, size_t *byteswritable, u_int *buffersize, int *numothers, bk_flags flags);
extern int bk_shmipc_errno(bk_s B, struct bk_shmipc *bsi, bk_flags flags);
extern int bk_shmipc_cancel(bk_s B, struct bk_shmipc *bsi, bk_flags flags);
extern int bk_shmipc_remove(bk_s B, const char *name, bk_flags flags);
extern int bk_shmipc_peekbyname(bk_s B, const char *name, u_int32_t *magic, u_int32_t *generation, u_int32_t *ringsize, u_int32_t *offset, u_int32_t *writehand, u_int32_t *readhand, size_t *bytesreadable, size_t *byteswritable, int *numothers, size_t *segsize, bk_flags flags);
#define BK_SHMIPC_FORCE		0x01		///< Force checks even if insufficient attaches

/* b_procinfo */
struct bk_procinfo
{
  pid_t		bpi_pid;			// Process ID
  char *	bpi_exec_path;			// Full path to executable (may be NULL)
  char *	bpi_comm;			// File name of the executabe
  char *	bpi_cmdline;			// Space separated command line
  char **	bpi_env;			// Environment list
  char		bpi_state;			// State of the process
  pid_t		bpi_ppid;			// Parent PID
  int		bpi_pgid;			// Process group ID
  int		bpi_sid;			// Senssion ID
  int		bpi_tty_major;			// TTY major number
  int		bpi_tty_minor;			// TTY minor number
  u_long	bpi_proc_flags;			// Kernel flags for process (may have to translate from text)
  u_long	bpi_utime;			// User mode time (units are arch specific)
  u_long	bpi_stime;			// Kernel mode time (units are arch specific)
  long		bpi_nice;			// Standard nice value ([-19, 19])
  u_long	bpi_starttime;			// Startime (units are arch specific)
  u_long	bpi_vsize;			// Virtual memory size
  long		bpi_rss;			// Resident set size
  u_long	bpi_wchan;			// Wait channel
  void *	bpi_arch_specific;		// Pointer for arch specific status info (not implmented)
  bk_flags	bpi_flags;			// Everyone needs flags

};

extern dict_h bk_procinfo_create(bk_s B, bk_flags flags);
extern void bk_procinfo_destroy(bk_s B, dict_h bpi_list);

/**
 * @name Defines: procinfo_list clc
 * helper defines for procinfo structures
 */
// @{
#define procinfo_list_create(o,k,f)		dll_create((o),(k),(f))
#define procinfo_list_destroy(h)		dll_destroy(h)
#define procinfo_list_insert(h,o)		dll_insert((h),(o))
#define procinfo_list_insert_uniq(h,n,o)	dll_insert_uniq((h),(n),(o))
#define procinfo_list_append(h,o)		dll_append((h),(o))
#define procinfo_list_append_uniq(h,n,o)	dll_append_uniq((h),(n),(o))
#define procinfo_list_search(h,k)		dll_search((h),(k))
#define procinfo_list_delete(h,o)		dll_delete((h),(o))
#define procinfo_list_minimum(h)		dll_minimum(h)
#define procinfo_list_maximum(h)		dll_maximum(h)
#define procinfo_list_successor(h,o)		dll_successor((h),(o))
#define procinfo_list_predecessor(h,o)		dll_predecessor((h),(o))
#define procinfo_list_iterate(h,d)		dll_iterate((h),(d))
#define procinfo_list_nextobj(h,i)		dll_nextobj((h),(i))
#define procinfo_list_iterate_done(h,i)		dll_iterate_done((h),(i))
#define procinfo_list_error_reason(h,i)		dll_error_reason((h),(i))

/* Gaussian Error Function Estimates */
#define BK_ERF(x) erf(x)
// Inverse of erf().
extern double bk_erfinv(double x);
#define BK_ERFINV(x) bk_erfinv(x)


#endif /* _BK_h_ */
