/*
 * $Id: libbk_oscompat.h,v 1.4 2001/09/25 08:25:50 dupuy Exp $
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
#if defined(_WIN32) && !defined(__CYGWIN32__)
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
#endif /* _WIN32 && !__CYGWIN32__ */


#endif /* _libbk_oscompat_h_ */
