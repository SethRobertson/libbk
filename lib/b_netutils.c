#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_netutils.c,v 1.1 2001/11/15 22:52:06 jtt Exp $";
static char libbk__copyright[] = "Copyright (c) 2001";
static char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
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

#include <libbk.h>
#include "libbk_internal.h"

/**
 * @file 
 * Random network utility functions. This a sort of a catch-all file
 * (though clearly each functino should have somethig to do with network
 * code.
 */


/**
 * Get the length of a sockaddr (not every OS supports sa_len);
 *	@param B BAKA thread/global state.
 *	@param sa @a sockaddr 
 *	@return <i>-1</i> on failure.<br>
 *	@return @a sockaddr <i>length</i> on success.
 */
int
bk_netutils_get_sa_len(bk_s B, struct sockaddr *sa)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int len;

  if (!sa)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  switch (sa->sa_family)
  {
  case AF_INET:
    len=sizeof(struct sockaddr_in);
    break;
  case AF_INET6:
    len=sizeof(struct sockaddr_in6);
    break;
  case AF_LOCAL:
    len=bk_strnlen(B, ((struct sockaddr_un *)sa)->sun_path, sizeof(struct sockaddr_un));
    break;
  default:
    bk_error_printf(B, BK_ERR_ERR, "Address family %d is not suppored\n", sa->sa_family);
    goto error;
    break;
  }

  BK_RETURN(B,len);

 error:
  BK_RETURN(B,-1);


}
