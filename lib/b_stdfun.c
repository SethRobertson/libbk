#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2001-2011";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2001-2011 The Authors. All rights reserved.
 *
 * This source code is licensed to you under the terms of the file
 * LICENSE.TXT in this release for further details.
 *
 * Send e-mail to <projectbaka@baka.org> for further information.
 *
 * - -Copyright BAKA- -
 */

#include <libbk.h>
#include <libbkssl.h>
#include "libbk_internal.h"


/**
 * @file
 * Standard program execution/termination utility functions
 */

static void bk_printserious(bk_s B, FILE *output, char *type, char *reason, bk_flags flags);



/**
 * Die -- exit and print reasoning.
 * Intentional lack of BK_ENTRY et al
 * Does not return.
 *
 * THREADS: EVIL (it exits, you see, otherwise MT-SAFE)
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
 * THREADS: MT-SAFE
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
 *
 * THREADS: MT-SAFE
 *
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
    // force error aggregation first
    bk_error_repeater_flush(B, 0);
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
 * THREADS: EVIL (it exits, you see)
 *
 *	@param B BAKA thread/global state
 *	@param retcode Return code
 */
void bk_exit(bk_s B, u_char retcode)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  bk_general_destroy(B);
  exit(retcode);
  /* NOTREACHED */
  BK_VRETURN(B);				/* Stupid insight */
}



/**
 * Dmalloc shutdown the way that the destroy funlist likes
 *
 * THREADS: MT-SAFE (assuming dmalloc is safe)
 *
 * @param B Baka thread/global environment
 * @param opaque Opaque data
 * @param other Other int argument
 */
void bk_dmalloc_shutdown(bk_s B, void *opaque, u_int other)
{
#ifdef USING_DMALLOC
  dmalloc_shutdown();
#endif /* USING_DMALLOC */
}


/**
 * Return whether SSL is supported at all or not. This needs to be included
 * *not* in libbssl so it is accessble even if libbkssl is not linked in.
 *
 *	@param B BAKA thread/global state.
 *	@return <i>1</i> if SSL is supported.<br>
 *	@return <i>0</i> if not.
 */
int
bk_ssl_supported(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkssl");

#ifndef NO_SSL
  /* runtime check for successful link of libbkssl */
  if (bk_ssl_start_service_verbose)
    BK_RETURN(B, 1);
#endif /* NO_SSL */
  BK_RETURN(B, 0);
}



#if defined(NO_SSL) || defined(__ELF__)
/**
 * This is merely a compatibility version of this function for use when
 * NO_SSL is set. By supporting this version, users do not have to worry
 * about remembering to compile this function conditionally on NO_SSL.
 *
 * <TRICKY>Unless NO_SSL is defined, this function has the WEAK attribute, which
 * means that the non-WEAK definition in libbkssl:b_ssl.c will be used instead.
 * For Insure builds it may be preferable to omit even the weak definition here
 * (as is done for non-ELF systems), but James can fix that if it is really a
 * problem.</TRICKY>
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
void
#ifndef NO_SSL
__attribute__((weak))
#endif /* NO_SSL */
bk_ssl_destroy(bk_s B, struct bk_ssl *ssl, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkssl");
  BK_VRETURN(B);
}
#endif /* NO_SSL || __ELF__ */
