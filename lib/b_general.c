#if !defined(lint) && !defined(__INSIGHT__)
static char baka__rcsid[] = "$Id: b_general.c,v 1.1 2001/05/30 01:23:08 seth Exp $";
static char baka__copyright[] = "Copyright (c) 2001";
static char baka__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright (c) 2001 The Authors.  All rights reserved.
 *
 * This source code is licensed to you under the terms of the file
 * LICENSE.TXT in this release for further details.
 *
 * Mail <projectbaka@baka.org> for further information
 *
 * --Copyright BAKA--
 */

#include <baka.h>
#include "baka_internal.h"



/*
 * Grand creation of baka state structure
 */
extern baka baka_baka_init(int argc, char ***argv, char ***envp, char *configfile, int error_queue_length, int log_facility)
{
  baka B;

  /* Allocate thread state structure */
  if (!(B = malloc(sizeof(*B))))
    goto error;
  memset(B,0,sizeof(*B));

  if (!(B->bt_funstack = baka_fun_init()))
    goto error;

  if (!(B->bt_threadname = strdup("*MAIN*")))
    goto error;

  if (!(B->bt_general = malloc(sizeof(*B->bt_general))))
    goto error;
  memset(B->bt_general, 0, sizeof(*B->bt_general));

  if (!(B->bt_general->bg_error = baka_error_init(B, error_queue_length, NULL, BAKA_LOG_NOFAC)))
    goto error;

  if (!(B->bt_general->bg_debug = baka_debug_init(B)))
    goto error;

  if (!(B->bt_general->bg_reinit = baka_funlist_init(B)))
    goto error;
  if (baka_funlist_insert(B,B->bt_general->bg_reinit,baka_reinit,B) < 0)
    goto error;

  if (!(B->bt_general->bg_config = baka_config_init(B, configfile, 0)) < 0)
    goto error;

  if (baka_setProcTitle(argc, argv, envp, &B->bt_general->bg_program) < 0)
    goto error;

 error:
  if (B)
    {
      if (B->bt_general)
	{
	  if (B->bt_general->bg_config)
	    baka_config_destroy(B, B->bt_general->bg_config);

	  if (B->bt_general->bg_error)
	    baka_error_destroy(B,B->bt_general->bg_error);

	  if (B->bt_general->bg_debug)
	    baka_debug_destroy(B,B->bt_general->bg_debug);

	  free(B->bt_general);
	}
      if (B->bt_funstack)
	baka_fun_destroy(B->bt_funstack);
      if (B->bt_threadname)
	free(B->bt_threadname);
      free(B);
    }

  /* Cannot return error messages yet */
  return(NULL);
}

