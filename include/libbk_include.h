/*
 * $Id: libbk_include.h,v 1.14 2002/01/09 06:26:38 dupuy Exp $
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

#ifndef _libbk_include_h_
#define _libbk_include_h_

/*
 * The GNU C library <sys/types.h> uses some weird GNU extension for gcc > 2.7
 * to define int#_t and u_int#t types.  Insure++ doesn't grok this at all, and
 * thinks that these are all int or unsigned int types.  So we have to prevent
 * this and force the types to use the <bits/types.h> __int#_t and __uint#_t
 * variants, which are defined more conventionally using char, short, long,
 * etcetera.
 */
#if defined(__INSURE__) && defined(__linux__)
#define int8_t weird_int8_t
#define int16_t weird_int16_t
#define int32_t weird_int32_t
#define int64_t weird_int64_t
#define u_int8_t weird_u_int8_t
#define u_int16_t weird_u_int16_t
#define u_int32_t weird_u_int32_t
#define u_int64_t weird_u_int64_t
#include <sys/types.h>
#undef int8_t
#undef int16_t
#undef int32_t
#undef int64_t
#undef u_int8_t
#undef u_int16_t
#undef u_int32_t
#undef u_int64_t
#define int8_t __int8_t
#define int16_t __int16_t
#define int32_t __int32_t
#define int64_t __int64_t
#define u_int8_t __uint8_t
#define u_int16_t __uint16_t
#define u_int32_t __uint32_t
#define u_int64_t __uint64_t
#endif /* __INSURE__ */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <memory.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <libintl.h>
#include <limits.h>
#include <popt.h>
#include <assert.h>
#include <dirent.h>
#ifdef BK_MINGW
#include <winsock.h>		/* for fd_set, etc. */
#include <process.h>		/* for getpid, etc. */
#else  /* !BK_MINGW */
#include <syslog.h>
#include <termios.h>
#include <pwd.h>
#include <grp.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#ifdef HAVE_NET_ETHERNET_H
#include <net/ethernet.h>
#else
#ifdef HAVE_NETINET_IF_ETHER_H
#include <net/if.h>
#include <netinet/if_ether.h>
#else  /* no struct ether_addr */
#warning "Must include file which defines struct ether_addr"
#endif /* HAVE_NETINET_IF_ETHER_H */
#endif /* HAVE_NET_ETHERNET_H */
#endif /* !BK_MINGW */

#if defined(USING_DMALLOC)
#include <dmalloc.h>
#endif /* USING_DMALLOC */

#ifdef REALLY_NEEDED
/*
 * all of this isn't being used (yet) and it is unclear if it will ever be
 */

#include <arpa/nameser.h>
#include <resolv.h>

/* add autoconf entries for these headers if they are really needed */
#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#else  /* !HAVE_SYS_IOCTL_H */
# ifdef HAVE_SYS_FILIO_H
#  include <sys/filio.h>
# endif/* !HAVE_SYS_FILIO_H */
#endif /* !HAVE_SYS_IOCTL_H */

#endif /* REALLY NEEDED */

#include "fsma.h"
#include "dict.h"
#include "bst.h"
#include "dll.h"
#include "ht.h"
#include "pq.h"

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


#endif /* _libbk_include_h_ */
