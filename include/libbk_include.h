/*
 * $Id: libbk_include.h,v 1.41 2004/07/12 17:23:36 jtt Exp $
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

#ifndef _libbk_include_h_
#define _libbk_include_h_

#define _GNU_SOURCE				// turn on all GNU extensions

#if defined(__INSURE__) && defined(__linux__)
/*
 * Insure can't handle the 64-bit file operation redirections using GNUC
 * asm syntax.  So we get the __REDIRECT macro and then make it go away.
 */
#include <sys/cdefs.h>
#undef __REDIRECT

/*
 * The GNU C library <sys/types.h> uses some weird GNU extension for gcc > 2.7
 * to define int#_t and u_int#t types.  Insure++ doesn't grok this at all, and
 * thinks that these are all int or unsigned int types.  So we have to prevent
 * this and force the types to use the <bits/types.h> __int#_t and __uint#_t
 * variants, which are defined more conventionally using char, short, long,
 * etcetera.
 */
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

/*
 * The Linux header files go through some insane contortions (especially for
 * union wait) to support BSD stuff, which we don't want or need, and which
 * causes Insure to choke.  So we turn off the BSD grub that we get as a
 * side-effect of _GNU_SOURCE.  <TRICKY>To avoid dependency on implementation
 * of _GNU_SOURCE, we don't include <features.h> but rather <stdio.h> which
 * gets it indirectly.</TRICKY>
 *
 * Update: __USE_BSD no longer freaks out insight. In fact, it now fails if
 * __USE_BSD is not set (sigh). So we no longer unset __USE_BSD, but we
 * leave in <stdio.h> since it works and jtt doesn't want bother figuring
 * out if it's Right or Wrong....
 */
#include <stdio.h>

#endif /* __INSURE__ */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>
#include <signal.h>
#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif
#include <stdarg.h>
#include <string.h>
#include <libgen.h>
#include <ctype.h>
#include <fcntl.h>
#include <memory.h>
#include <sys/stat.h>
#include <limits.h>
#include <popt.h>
#include <assert.h>
#include <dirent.h>
#ifndef BK_MINGW32
#include <libintl.h>
#ifdef HAVE_PATHS_H
#include <paths.h>
#endif
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
#include <arpa/inet.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/ip_icmp.h>
#include <net/route.h>
#else  /* BK_MINGW32 */
#include <winsock.h>		/* for fd_set, etc. */
#include <process.h>		/* for getpid, etc. */
#endif /* BK_MINGW32 */
#ifdef HAVE_CRT_EXTERNS_H
#include <crt_externs.h>
#endif // HAVE_CRT_EXTERNS_H

#include <float.h>

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif /* HAVE_ALLOCA_H */


#if defined(USING_DMALLOC)
#include <dmalloc.h>
#endif /* USING_DMALLOC */


#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif /* !HAVE_SYS_IOCTL_H */
#ifdef HAVE_SYS_FILIO_H
# include <sys/filio.h>
#endif/* !HAVE_SYS_FILIO_H */


#ifdef REALLY_NEEDED
/*
 * all of this isn't being used (yet) and it is unclear if it will ever be
 */

#include <arpa/nameser.h>
#include <resolv.h>
#endif /* REALLY NEEDED */

#ifdef BK_USING_PTHREADS
#include <pthread.h>
#endif

#include "fsma.h"
#include "dict.h"
#include "bst.h"
#include "dll.h"
#include "ht.h"
#include "pq.h"

#endif /* _libbk_include_h_ */
