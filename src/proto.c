#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: proto.c,v 1.2 2001/08/16 21:10:48 seth Exp $";
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



struct global_structure
{
} Global;



int
main(int argc, char **argv, char **envp)
{
  bk_s pg_bk;					/* Baka general structure */

  if (!(pg_bk=bk_general_init(argc, &argv, &envp, BK_ENV_GWD("BK_ENV_CONF_APP", BK_APP_CONF), 64, BK_ERR_ERR, 0)))
  {
    fprintf(stderr,"Could not initialize baka structure");
    exit(1);
  }
  exit(0);
}
