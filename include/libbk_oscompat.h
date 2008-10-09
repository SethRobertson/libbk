/*
 * 
 *
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

#ifndef _libbk_oscompat_h_
#define _libbk_oscompat_h_

#define BUG_LLU_CAST(x)	((long long unsigned)(x))
#define BUG_LLI_CAST(x)	((long long int)(x))

#ifdef __x86_64
#define BK_POINTER2INTS(x) ((long)(x))
#define BK_INTS2POINTER(x) ((void *)(long)(x))
#else
#define BK_POINTER2INTS(x) ((int)(x))
#define BK_INTS2POINTER(x) ((void *)(x))
#endif



#ifdef __INSURE__
/* #undef HAVE_CONSTRUCTOR_ATTRIBUTE */
#undef HAVE_INIT_PRAGMA
#endif

#ifndef HAVE_SOCKLEN_T
typedef unsigned int socklen_t;
#endif /* HAVE_SOCKLEN_T */


// Parentheses prevent string concatenation which __func__ may not support
#ifdef HAVE__FUNC__
# ifndef __INSURE__
#  define BK_FUNCNAME (__func__)
# else // __INSURE__ doesn't know __func__; thinks it is of type int
#  define BK_FUNCNAME (__FUNCTION__)
# endif
#elif defined(HAVE__PRETTY_FUNCTION__)
# define BK_FUNCNAME (__PRETTY_FUNCTION__)
#elif defined(HAVE__FUNCTION__)
# define BK_FUNCNAME (__FUNCTION__)
#else
# define BK_FUNCNAME (__FILE__ ":" BK_STRINGIFY(__LINE__))
# define HAVE__FILE_LINE
#endif

#ifndef HAVE_IN_ADDR_T
typedef uint32_t in_addr_t;
#endif

#if !defined(__GNUC__) || defined(__INSURE__)	// should this be autoconf'ed?
#define BK_RHS(expr) (expr)			// ISO C forbids ?: or , on lhs
#else  /* __GNUC__ && !__INSURE__ */
#define BK_RHS(expr) ({expr;})			// force GCC to forbid it also
#endif /* __GNUC__ && !__INSURE__ */

// This should always work
#define USING_LT_DLOPEN

#ifndef BK_NO_USER_SHARED_LIBS
/* USING_LT_DLOPEN is hardcoded true (see #define above)
 * You can therefore ignore all of the following cruft
 * which only remains in case we encounter a
 * ltdl-unsupported system later.
 */
#ifdef USING_LT_DLOPEN
#define BK_INIT_FUN(mod) \
 void mod ## _init (void)

#define BK_FINISH_FUN(mod) \
 void mod ## _finish (void)
#else
#ifdef HAVE_CONSTRUCTOR_ATTRIBUTE
/**
 * Define an initialization function.
 *
 * This macro takes a plugin module name as an argument, and declares a
 * function signature for an initialization function.  Simply follow this macro
 * with ; for a declaration, or { code... } for a definition.  The resulting
 * function takes no arguments, returns no value, and will be called at plugin
 * load time (possibly before the program main itself).  <em>Note:</em> a
 * definition using this macro should be preceded by the following C code:
 *
 * <code>
 * #ifdef HAVE_INIT_PRAGMA
 * #pragma init(<i>mod</i>_init)	// <i>mod</i> is arg to BK_INIT_FUN
 * #endif
 * </code>
 *
 * @param mod module name
 */
#define BK_INIT_FUN(mod) \
 void __attribute__((__constructor__)) mod ## _init (void)


/**
 * Define a finalization function.
 *
 * This macro takes a plugin module name as an argument, and declares a
 * function signature for an finalization function.  Simply follow this macro
 * with ; for a declaration, or { code... } for a definition.  The resulting
 * function takes no arguments, returns no value, and will be called at plugin
 * unload time. <em>Note:</em> a definition using this macro should be preceded
 * by the following C code:
 *
 * <code>
 * #ifdef HAVE_INIT_PRAGMA
 * #pragma fini(<i>mod</i>_finish)	// <i>mod</i> is arg to BK_FINISH_FUN
 * #endif
 * </code>
 *
 * @param mod module name
 */
#define BK_FINISH_FUN(mod) \
 void __attribute__((__destructor__)) mod ## _finish (void)
#else
#ifdef HAVE_INIT_PRAGMA
// better hope the developers remembered their pragmas
#define BK_INIT_FUN(mod)  void mod ## _init (void)
#define BK_FINISH_FUN(mod) void mod ## _finish (void)
#else
// no linker support for init functions; we'll need to use libtool to get name
#define BK_INIT_FUN(mod) void mod ## _LTX_init (void)
#define BK_FINISH_FUN(mod) void mod ## _LTX_finish (void)
#endif /* !HAVE_INIT_PRAGMA */
#endif /* !HAVE_CONSTRUCTOR_ATTRIBUTE */
#endif /* !USING_LT_DLOPEN */
#else

/* We don't want shared libs at all here */
#define BK_INIT_FUN(mod) void mod ## _init (void)
#define BK_FINISH_FUN(mod) void mod ## _finish (void)

#endif /* NO_USER_SHARED_LIBS */


// FreeBSD calls it O_FSYNC, not O_SYNC, and who can say why?
#ifndef O_SYNC
#ifdef O_FSYNC
#define O_SYNC O_FSYNC
#endif
#endif

// Just in case we're dealing with some anti-POSIX OS somewhere
#ifndef O_NONBLOCK
#ifdef O_NDELAY
#define O_NONBLOCK O_NDELAY
#endif
#endif

// Fix networking constants for Darwin
#ifndef ICMP_DEST_UNREACH
#define ICMP_DEST_UNREACH ICMP_UNREACH
#endif

#ifndef ICMP_SOURCE_QUENCH
#define ICMP_SOURCE_QUENCH ICMP_SOURCEQUENCH
#endif

#ifndef ICMP_TIME_EXCEEDED
#define ICMP_TIME_EXCEEDED ICMP_TIMXCEED
#endif

#ifndef ICMP_PARAMETERPROB
#define ICMP_PARAMETERPROB ICMP_PARAMPROB
#endif

#ifndef ETH_P_IP
#define ETH_P_IP ETHERTYPE_IP
#endif


// Force NULL to be a pointer type, for proper typeof (esp. in BK_RETURN)
#ifdef NULL
#undef NULL
#endif
#define NULL ((void *)0)


#ifndef MAX
#define MAX(x,y) ((x) > (y) ? (x) : (y))
#endif /* !MAX */

#ifndef MIN
#define MIN(x,y) ((x) > (y) ? (y) : (x))
#endif /* !MIN */

/*
 * Attempt to make things work on windows without cygwin
 */
#ifdef BK_MINGW32
typedef int ssize_t;
typedef unsigned char u_int8_t;
typedef unsigned short u_int16_t;
typedef unsigned int u_int32_t;
typedef unsigned long long u_int64_t;
typedef int int32_t;
typedef long long int64_t;
typedef char *caddr_t;

struct timespec
{
  time_t tv_sec;
  long tv_nsec;
};

#define LOG_EMERG	0
#define LOG_ALERT	1
#define LOG_CRIT	2
#define LOG_ERR		3
#define LOG_WARNING	4
#define LOG_NOTICE	5
#define LOG_INFO	6
#define LOG_DEBUG	7

#define snprintf _snprintf
#define vsnprintf _vsnprintf
#define sleep(sec) _sleep(1000*(sec))

#define BYTE_ORDER 4321
#define LITTLE_ENDIAN 4321
#define BIG_ENDIAN 1234
#endif /* BK_MINGW32 */

#ifndef __SIZE_TYPE__
#define __SIZE_TYPE__ size_t
#endif /* __SIZE_TYPE__ */

#ifdef HAVE_SOCKADDR_SA_LEN
# define BK_SET_SOCKADDR_LEN(B,s,l) do { ((struct sockaddr *)(s))->sa_len=(l); } while(0)
# define BK_GET_SOCKADDR_LEN(B,s,l) do { (l)=((struct sockaddr *)(s))->sa_len; } while(0)
#else
// never anything that needs to be set
# define BK_SET_SOCKADDR_LEN(B,s,l) do{}while(0)
#ifdef HAVE_SA_LEN_MACRO
# define BK_GET_SOCKADDR_LEN(B,s,l) do { (l)=SA_LEN(((struct sockaddr *)(s))); } while(0)
#else
# define BK_GET_SOCKADDR_LEN(B,s,l) do { (l)=bk_netutils_get_sa_len((B),((struct sockaddr *)(s))); } while(0)
#endif /* !HAVE_SA_LEN_MACRO */
#endif /* !HAVE_SOCKADDR_SA_LEN */

#ifdef HAVE_NETINET_ETHER_H
#include <net/if.h>
#include <netinet/ether.h>
#else
#ifdef HAVE_NET_ETHERNET_H
#include <net/ethernet.h>
#else  /* no struct ether_addr */
#ifdef HAVE_NETINET_IF_ETHER_H
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/if_ether.h>
#else /* HAVE_NETINET_IF_ETHER_H */
struct ether_addr
{
  u_int8_t ether_addr_octet[6];
}
#ifdef __GNUC__
__attribute__ ((__packed__))
#endif /* __GNUC__ */
;
#endif /* HAVE_NETINET_IF_ETHER_H */
#endif /* HAVE_NETINET_ETHER_H */
#endif /* HAVE_NET_ETHERNET_H */

/*
 * No OS has ntohll/htonll yet, but it probably will be added
 */
#ifndef ntohll
#ifdef WORDS_BIGENDIAN
# define ntohll(x) (x)
# define htonll(x) (x)
#else
# if defined(__bswap_64) && !defined(__INSURE__)	// Linux uses this
#  define ntohll(x) __bswap_64 (x)
#  define htonll(x) __bswap_64 (x)
# else  /* !bswap_64 */
#  define ntohll(x) (((u_int64_t)ntohl((u_int64_t)(x) & 0xffffffff) << 32) | ntohl((u_int64_t)(x) >> 32))
#  define htonll(x) (((u_int64_t)ntohl((u_int64_t)(x) & 0xffffffff) << 32) | ntohl((u_int64_t)(x) >> 32))
# endif
#endif /* !bswap_64 */

#endif /* ntohll */

#if defined(AF_INET6) && defined(HAVE_INET_PTON)
#define HAVE_INET6
#endif

#if !defined(AF_LOCAL) && defined(AF_UNIX)
#define AF_LOCAL AF_UNIX
#endif

/*
 * The GNU C library <sys/select.h> uses some GNU asm stuff for FD_ISSET
 * that causes Insure to generate DEAD_CODE(noeffect), which we suppress
 * by using the non-asm definition.
 */
#if defined(__INSURE__) && defined(__linux__)
#if defined(__FD_ISSET) && defined(__FDS_BITS) && defined(__FDELT) && defined(__FDMASK)
#undef __FD_ISSET
#define __FD_ISSET(d, set)  (__FDS_BITS (set)[__FDELT (d)] & __FDMASK (d))
#endif /* __FD_ISSET */
#endif /* __INSURE__ */

/*
 * This is a Linux thing which allows you to open large files on fs which
 * might not otherwise support them. For OS's which do not support this
 * natively, defining it to 0 makes it an "identity" flag (ie has no
 * effect).
 */
#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif

#ifndef _PATH_DEVNULL
#ifndef BK_MINGW32
#define _PATH_DEVNULL "/dev/null"
#else  /* BK_MINGW32 */
#define _PATH_DEVNULL "NUL:"
#endif /* BK_MINGW32 */
#endif /* !_PATH_DEVNULL */

/*
 * Size definitions
 */
/* Minimum of signed integral types.  */
#ifndef INT8_MIN
# define INT8_MIN               (-128)
# define INT16_MIN              (-32767-1)
# define INT32_MIN              (-2147483647-1)
# ifdef __INT64_C
#  define INT64_MIN             (-__INT64_C(9223372036854775807)-1)
# else
#  define INT64_MIN             LLONG_MIN
# endif
/* Maximum of signed integral types.  */
# define INT8_MAX               (127)
# define INT16_MAX              (32767)
# define INT32_MAX              (2147483647)
# ifdef __INT64_C
#  define INT64_MAX             (__INT64_C(9223372036854775807))
# else
#  define INT64_MAX             LLONG_MAX
# endif

/* Maximum of unsigned integral types.  */
# define UINT8_MAX              (255)
# define UINT16_MAX             (65535)
# define UINT32_MAX             (4294967295U)
# ifdef __INT64_C
#  define UINT64_MAX             (__UINT64_C(18446744073709551615))
# else
#  define UINT64_MAX             ULLONG_MAX
# endif
#endif /* INT8_MIN */

#ifndef ULLONG_MAX				// ISO C99, but not ANSI C87
#define ULLONG_MAX ((unsigned long long) -1)
#endif

// GZIP Needs to have this OS_CODE thingy for some reason.
#ifdef BK_MINGW32
#define BK_GZIP_OS_CODE		0x0b
#else /* Default assumes unix */
#define BK_GZIP_OS_CODE		0x03
#endif

#ifndef HAVE_ISINF
# ifdef HAVE_IEEEFP_H
#  include <ieeefp.h>
# endif /* HAVE_IEEEFP_H */
#define isinf(d) ((fpclass(d) == FP_NINF)?-1:((fpclass(d) == FP_PINF)?1:0))
#endif /* HAVE_ISINF */

/*
 * gmtime() and localtime() are not reentrant, but if you don't have *time_r(),
 * you probably don't have threads or care one whit about that.
 */
#ifndef HAVE_GMTIME_R
// <WARNING>Having to use this is unsafe under certain cirumstances</WARNING>
#define gmtime_r(c,t) (((*t) = *(gmtime(c))),(t))
#else
// Just in case there's no prototype, we declare the standard one
extern struct tm *gmtime_r(const time_t *timenow, struct tm *buffer);
#endif

#ifndef HAVE_LOCALTIME_R
// <WARNING>Having to use this is unsafe under certain cirumstances</WARNING>
#define localtime_r(c,t) (((*t) = *(localtime(c))),(t))
#else
// Just in case there's no prototype, we declare the standard one
extern struct tm *localtime_r(const time_t *timenow, struct tm *buffer);
#endif

#ifndef HAVE_GETHOSTBYNAME2
# define BK_GETHOSTBYNAME2(n,f)	(gethostbyname(n))
#else
# define BK_GETHOSTBYNAME2(n,f)	(gethostbyname2(n,f))
#endif /* HAVE_GETHOSTBYNAME2 */

/*
 * Some platforms provide a definition for this in unistd.h, but even those
 * platforms say that you have to declare environ in your program.  On some
 * platforms, we may need to declare this as common (i.e. uninitialized data):
 *
 *   char **environ;
 *
 * (this is done in bk_general.c so that we don't have to suppress warnings
 * about unresolved references for *BSD).
 *
 * Theoretically, on some platforms, this might need to be declared as a
 * array rather than a pointer.  It might be const-ified in various ways as
 * well.
 */

#ifdef HAVE_NSGETENVIRON
// Darwin nonsense
#define environ (*_NSGetEnviron())
#else
extern char **environ;
#endif

#ifdef HAVE_SETENV
# define BK_SETENV(B, variable, value, overwrite)	(setenv(variable, value, overwrite))
# define BK_UNSETENV(B, variable)			(unsetenv(variable))
#else /* HAVE_SETENV */
# define BK_SETENV(B, variable, value, overwrite)	(bk_setenv_with_putenv(B, variable, value, overwrite))
# define BK_UNSETENV(B, variable)			(putenv(variable))
#endif /* HAVE_SETENV */

#ifndef HAVE_NEARBYINT
#define nearbyint rint
#endif	/* !HAVE_NEARBYINT */

#ifndef HAVE_TRUNC
/*
 * Simple replacement for C99 trunc using C89 fmod
 */
static inline double trunc(double x)
{
  return x - fmod(x, 1.0);
}
#endif /* !HAVE_TRUNC */

#ifndef HAVE_STRNLEN
/*
 * Simple replacement for GNU extension using only standard ANSI API.
 */
static inline size_t strnlen(const char *s, size_t maxlen)
{
  char *end = memchr(s, '\0', maxlen);
  if (!end)
    return maxlen;
  return end - s;
}
#endif	/* !HAVE_STRNLEN */

// Old versions of popt.  Sigh.
#ifndef POPT_TABLEEND
#define POPT_TABLEEND { NULL, '\0', 0, 0, 0, NULL, NULL }
#endif /* POPT_TABLEEND */

#endif /* _libbk_oscompat_h_ */
