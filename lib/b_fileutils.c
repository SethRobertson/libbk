#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_fileutils.c,v 1.3 2001/11/27 00:58:41 seth Exp $";
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

/**
 * @file
 * Random utilities relating to files and file descriptors.
 */

#include <libbk.h>
#include "libbk_internal.h"



/**
 * Add a flag (or set of flags) to a descriptor
 *
 *	@param B BAKA thread/global state.
 *	@param fd The descriptor to modify
 *	@param flags The flags to add.
 *	@param action What action to take (add/delete).
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_fileutils_modify_fd_flags(bk_s B, int fd, long flags, bk_fileutils_modify_fd_flags_action_t action)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  long oflags = 0;

  if (action != BK_FILEUTILS_MODIFY_FD_FLAGS_ACTION_SET &&
      (oflags=fcntl(fd, F_GETFL))<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not recover current flags: %s\n", strerror(errno));
    goto error;
  }

  switch (action)
  {
  case BK_FILEUTILS_MODIFY_FD_FLAGS_ACTION_ADD:
    BK_FLAG_SET(oflags,flags);
    break;
  case BK_FILEUTILS_MODIFY_FD_FLAGS_ACTION_DELETE:
    BK_FLAG_CLEAR(oflags,flags);
    break;
  case BK_FILEUTILS_MODIFY_FD_FLAGS_ACTION_SET:
    oflags=flags;
    break;
  default:
    bk_error_printf(B, BK_ERR_ERR, "Unknown fd flag action: %d\n", action);
    goto error;
    break;
  }

  if (fcntl(fd, F_SETFL, oflags))
  {
    bk_error_printf(B, BK_ERR_ERR, "Coudl not set new flags: %s\n", strerror(errno));
    goto error;
  }
  BK_RETURN(B,0);

 error:
  BK_RETURN(B,-1);
}
