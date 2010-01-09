#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2002-2010";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2002-2010 The Authors. All rights reserved.
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
 * Convenience functions for retrieving and manipulating system state
 */

#include <libbk.h>
#include "libbk_internal.h"



#define STARTINGHOSTNAMELEN	8		///< First guess (well, actually two greater than first guess)

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN	      1024		///< Maximum size of a host name
#endif /* MAXHOSTNAMELEN */



/**
 * Find the system's hostname.  Note this is allocated memory which
 * the caller must free.
 *
 * <TODO>I am given to understand not everyone supports gethostname</TODO>
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <br><i>hostname</i> on success.
 */
char *bk_gethostname(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int hostnamelen=STARTINGHOSTNAMELEN;
  char *hostname;

  while (1)
  {
    if (!BK_CALLOC_LEN(hostname, hostnamelen))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate %d memory for hostname: %s\n", hostnamelen, strerror(errno));
      BK_RETURN(B, NULL);
    }

    errno = 0;
    if ((gethostname(hostname, hostnamelen) < 0) && (errno != EINVAL) && (errno != ENAMETOOLONG))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not retrieve hostname: %s\n", strerror(errno));
      BK_RETURN(B, NULL);
    }

    // Ensure that hostname was not truncated
    if (!errno && !hostname[hostnamelen-2])
      break;

    free(hostname);

    if ((hostnamelen *= 2) > MAXHOSTNAMELEN)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not hostname without error--hostname too long?: %s\n", strerror(errno));
      BK_RETURN(B, NULL);
    }
  }

  BK_RETURN(B, hostname);
}
