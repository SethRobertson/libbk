/*
 * $Id: libbk_oscompat.h,v 1.10 2002/01/20 04:12:02 jtt Exp $
 *
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

#ifndef _libbk_oscompat_h_
#define _libbk_oscompat_h_

#ifdef SOMETHING_FOR_CPLUSPLUS
#ifdef NULL
#undef NULL
#endif /* NULL */
#define NULL ((void *)0)
#endif /* SOMETHING_FOR_CPLUSPLUS */

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
typedef unsigned char u_int8_t;
typedef unsigned short u_int16_t;
typedef unsigned int u_int32_t;
typedef unsigned long long u_int64_t;
typedef int int32_t;
typedef long long int64_t;
typedef char *caddr_t;

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

/* XXX AUTOCONF THIS!!!!!!! */
#if defined(__linux__) || !defined(HAVE_SOCKADDR_LEN)
#	define BK_SET_SOCKADDR_LEN(B,s,l) do{}while(0)
#	define BK_GET_SOCKADDR_LEN(B,s,l) do { (l)=bk_netutils_get_sa_len((B),((struct sockaddr *)(s))); }while(0)
#else
#	define BK_SET_SOCKADDR_LEN(B,s,l) do { ((struct sockaddr *)(s))->sa_len=(l); }while(0)
#	define BK_GET_SOCKADDR_LEN(B,s,l) do { (l)=((struct sockaddr *)(s))->sa_len; }while(0)
#endif 

#if defined(AF_INET6) && defined(HAVE_INET_PTON)
#define HAVE_INET6
#endif

/*
 * While this makes us compat. w/ some really broken realloc()'s, this is
 * really more for keeping Insight happy.
 */
#define realloc(ptr,len)	((!(ptr))?malloc(len):realloc((ptr),(len)))

#endif /* _libbk_oscompat_h_ */
