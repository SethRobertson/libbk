#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_stdfun.c,v 1.7 2001/11/28 18:24:09 seth Exp $";
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


static void bk_printserious(bk_s B, FILE *output, char *type, char *reason, bk_flags flags);



/**
 * Die -- exit and print reasoning.
 * Intentional lack of BK_ENTRY et al
 * Does not return.
 *
 *	@param B BAKA thread/global state 
 *	@param retcode Return code
 *	@param output File handle to dump death information
 *	@param reason Reason we are dying
 *	@param flags Type of messages we should print
 */
void bk_die(bk_s B, u_char retcode, FILE *output, char *reason, bk_flags flags)
{
  if (!output || !reason)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid die arguments\n"); // Not very useful
    bk_exit(B, retcode);
  }

  bk_error_printf(B, BK_ERR_CRIT, "DIE: %s",reason);
  bk_printserious(B, output, "DIE", reason, flags);
  bk_exit(B, retcode);
}



/**
 * Warn -- print a strong warning message of major problems
 * Intentional lack of BK_ENTRY et al.
 *
 *	@param B BAKA thread/global state 
 *	@param retcode Return code
 *	@param output File handle to dump death information
 *	@param reason Reason we are dying
 *	@param flags Type of messages we should print
 */
void bk_warn(bk_s B, FILE *output, char *reason, bk_flags flags)
{
  if (!output || !reason)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid warn arguments\n");
    return;
  }

  bk_error_printf(B, BK_ERR_WARN, "WARN: %s",reason);
  bk_printserious(B, output, "WARN", reason, flags);
  return;
}



/**
 * Private warn/die message output.
 * Intentional lack of BK_ENTRY et al.
 #
 *	@param B BAKA thread/global state 
 *	@param output Output file handle to direct output
 *	@param type of error message (warn/die)
 *	@param reason that user specified
 *	@param flags To control verbosity--BK_WARNDIE_WANTDETAILS
 */
static void bk_printserious(bk_s B, FILE *output, char *type, char *reason, bk_flags flags)
{
  if (!output || !type || !reason)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid die/warn arguments\n");
    return;
  }

  fprintf(output,"%s: %s: %s\n",BK_GENERAL_PROGRAM(B),type,reason);

  if (BK_FLAG_ISSET(flags,BK_WARNDIE_WANTDETAILS))
  {
    fprintf(output,"---------- Start %s Function Trace ----------\n",BK_GENERAL_PROGRAM(B));
    bk_fun_trace(B, output, BK_ERR_NONE, 0);
    fprintf(output,"---------- Start %s Error Queue ----------\n",BK_GENERAL_PROGRAM(B));
    bk_error_dump(B, output, NULL, BK_ERR_NONE, BK_ERR_NONE, 0);
    fprintf(output,"---------- End %s ----------\n",BK_GENERAL_PROGRAM(B));
  }
}



/**
 * Normal program exit.
 * Does not return.
 *
 *	@param B BAKA thread/global state 
 *	@param retcode Return code
 */
void bk_exit(bk_s B, u_char retcode)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  bk_general_destroy(B);
  exit(retcode);
}
