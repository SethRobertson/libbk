/*
 * $Id: libbk_include.h,v 1.10 2001/11/16 21:30:03 brian Exp $
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
#if defined(_WIN32) && !defined(__CYGWIN32__)
#include <winsock.h>		/* for fd_set, etc. */
#include <process.h>		/* for getpid, etc. */
#else  /* !_WIN32 || __CYGWIN32__ */
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
#include <net/ethernet.h>
#endif /* !_WIN32 || __CYGWIN32__ */

#if defined(USING_DMALLOC)
#include <dmalloc.h>
#endif /* USING_DMALLOC */

#ifdef REALLY_NEEDED
#include <arpa/nameser.h>
#include <resolv.h>

#if defined(__linux__) || defined(__hpux__) || defined(__osf__) || defined(_AIX)
# include <sys/ioctl.h>
#else /* filio.h instead of ioctl.h */
# include <sys/filio.h>
#endif /* filio.h instead of ioctl.h */

#endif /* REALLY NEEDED */

#include "fsma.h"
#include "dict.h"
#include "bst.h"
#include "dll.h"
#include "ht.h"
#include "pq.h"

#endif /* _libbk_include_h_ */
