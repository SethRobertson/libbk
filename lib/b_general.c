#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_general.c,v 1.2 2001/06/08 22:11:03 seth Exp $";
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



/*
 * Grand creation of libbk state structure
 */
extern bk_s bk_general_init(int argc, char ***argv, char ***envp, char *configfile, int error_queue_length, int log_facility)
{
  bk_s B;

  /* Allocate thread state structure */
  if (!(B = malloc(sizeof(*B))))
    goto error;
  memset(B,0,sizeof(*B));

  if (!(B->bt_funstack = bk_fun_init()))
    goto error;

  if (!(B->bt_threadname = strdup("*MAIN*")))
    goto error;

  if (!(B->bt_general = malloc(sizeof(*B->bt_general))))
    goto error;
  memset(B->bt_general, 0, sizeof(*B->bt_general));

  if (!(B->bt_general->bg_error = bk_error_init(B, error_queue_length, NULL, BK_LOG_NOFAC)))
    goto error;

  if (!(B->bt_general->bg_debug = bk_debug_init(B)))
    goto error;

  if (!(B->bt_general->bg_reinit = bk_funlist_init(B)))
    goto error;
  if (bk_funlist_insert(B,B->bt_general->bg_reinit,bk_reinit,B) < 0)
    goto error;

  if (!(B->bt_general->bg_config = bk_config_init(B, configfile, 0)) < 0)
    goto error;

  if (bk_setProcTitle(argc, argv, envp, &B->bt_general->bg_program) < 0)
    goto error;

 error:
  if (B)
    {
      if (B->bt_general)
	{
	  if (B->bt_general->bg_config)
	    bk_config_destroy(B, B->bt_general->bg_config);

	  if (B->bt_general->bg_error)
	    bk_error_destroy(B,B->bt_general->bg_error);

	  if (B->bt_general->bg_debug)
	    bk_debug_destroy(B,B->bt_general->bg_debug);

	  free(B->bt_general);
	}
      if (B->bt_funstack)
	bk_fun_destroy(B->bt_funstack);
      if (B->bt_threadname)
	free(B->bt_threadname);
      free(B);
    }

  /* Cannot return error messages yet */
  return(NULL);
}

