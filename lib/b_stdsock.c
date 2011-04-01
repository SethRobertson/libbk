#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2002-2011";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2002-2011 The Authors. All rights reserved.
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
 * Standard things to do to sockets, like broadcast and multicast options
 */

#include <libbk.h>
#include "libbk_internal.h"



/**
 * Enable multicasting for a certain FD
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state
 *	@param fd File descriptor (socket) to manipulate
 *	@param ttl Time to live to use
 *	@param maddrgroup IP address to join
 *	@param flags BK_MULTICAST_WANTLOOP if we want outgoing packets to go to self as well
 *		BK_MULTICAST_NOJOIN if we do not wish to join a multicast group
 *	@return <i>0</i> success
 *	@return <br><i>-1</i> on failure
 */
int bk_stdsock_multicast(bk_s B, int fd, u_char ttl, struct bk_netaddr *maddrgroup, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  u_char zero = 0;
  u_char one = 1;
  struct ip_mreq mreq;

  if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP,
		 (BK_FLAG_ISSET(flags, BK_MULTICAST_WANTLOOP))?&one:&zero, sizeof(one)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set multicast looping preference: %s\n",strerror(errno));
    BK_RETURN(B, -1);
  }

  if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set multicast ttl preference to %d: %s\n",ttl, strerror(errno));
    BK_RETURN(B, -1);
  }

  if (BK_FLAG_ISCLEAR(flags, BK_MULTICAST_NOJOIN) && maddrgroup)
  {
    memset(&mreq, 0, sizeof(mreq));
    memcpy(&mreq.imr_multiaddr, &maddrgroup->bna_inet, sizeof(mreq.imr_multiaddr));
    mreq.imr_interface.s_addr = INADDR_ANY;

    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not set multicast group preference to %s: %s\n", inet_ntoa(mreq.imr_multiaddr), strerror(errno));
      BK_RETURN(B, -1);
    }
  }

  BK_RETURN(B, 0);
}



/**
 * Enable broadcasting for a certain FD
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state
 *	@param fd File descriptor (socket) to manipulate
 *	@param flags fun for the future
 *	@return <i>0</i> success
 *	@return <br><i>-1</i> on failure
 */
int bk_stdsock_broadcast(bk_s B, int fd, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int one = 1;

  if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set broadcast preference: %s\n",strerror(errno));
    BK_RETURN(B, -1);
  }

  BK_RETURN(B, 0);
}
