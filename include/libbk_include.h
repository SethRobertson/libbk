/*
 * $Id: libbk_include.h,v 1.12 2001/12/27 17:51:41 dupuy Exp $
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

#endif /* _libbk_include_h_ */
