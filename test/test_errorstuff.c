#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: test_errorstuff.c,v 1.5 2001/12/06 16:53:23 jtt Exp $";
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


#define ERRORQUEUE_DEPTH 64			/* Default depth */



/*
 * Information of international importance to everyone
 * which cannot be passed around.
 */
struct global_structure
{
} Global;



/*
 * Information about basic program runtime configuration
 */
struct program_config
{
  bk_flags pc_flags;
#define PC_VERBOSE	1
#define PC_STDERR	2
#define PC_SYSLOG	4
};



void usage(bk_s B);
int proginit(bk_s B, struct program_config *pconfig);
void progrun(bk_s B, struct program_config *pconfig);



int
main(int argc, char **argv, char **envp)
{
  bk_s B = NULL;				/* Baka general structure */
  BK_ENTRY(B, __FUNCTION__, __FILE__, "SIMPLE");
  int getopterr = 0;
  extern char *optarg;
  extern int optind;
  struct program_config Pconfig, *pconfig;

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD("BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, BK_ERR_ERR, 0)))
  {
    fprintf(stderr,"Could not perform basic initialization\n");
    exit(254);
  }

  bk_fun_reentry(B);

  pconfig = &Pconfig;
  memset(pconfig,0,sizeof(*pconfig));

  if (getopterr)
  {
    usage(B);
    bk_exit(B,254);
  }
    
  if (proginit(B, pconfig) < 0)
  {
    bk_die(B,254,stderr,"Could not perform program initialization\n",BK_FLAG_ISSET(pconfig->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  }

  progrun(B, pconfig);
  bk_exit(B,0);
  abort();
  BK_RETURN(B,255);				/* Insight is stupid */
}



/*
 * Usage
 */
void usage(bk_s B)
{
  fprintf(stderr,"Usage: %s: [-desv]\n",BK_GENERAL_PROGRAM(B));
}



/*
 * Initialization
 */
int proginit(bk_s B, struct program_config *pconfig)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
  FILE *fh = NULL;
  int sysloglevel = BK_ERR_NONE;

  if (BK_FLAG_ISSET(pconfig->pc_flags, PC_SYSLOG))
    sysloglevel = BK_ERR_ERR;

  if (BK_FLAG_ISSET(pconfig->pc_flags, PC_STDERR))
    fh = stderr;

  bk_error_config(B, BK_GENERAL_ERROR(B), ERRORQUEUE_DEPTH, fh, sysloglevel, BK_ERR_ERR, 0);

  BK_RETURN(B, 0);
}



/*
 * Normal processing
 */
void progrun(bk_s B, struct program_config *pconfig)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
  int cnt;

  for(cnt=0;cnt<ERRORQUEUE_DEPTH/2;cnt++)
  {
    bk_error_printf(B, BK_ERR_ERR, "Error test 1 %d\n",cnt);
  }
  sleep(1);
  for(cnt=0;cnt<ERRORQUEUE_DEPTH/3;cnt++)
  {
    bk_error_printf(B, BK_ERR_WARN, "Warning test 1 %d\n",cnt);
  }
  sleep(1);
  for(cnt=0;cnt<ERRORQUEUE_DEPTH/3;cnt++)
  {
    bk_error_printf(B, BK_ERR_ERR, "Error test 2 %d\n",cnt);
  }
  sleep(1);
  for(cnt=0;cnt<ERRORQUEUE_DEPTH/2;cnt++)
  {
    bk_error_printf(B, BK_ERR_WARN, "Warning test 2 %d\n",cnt);
  }
  sleep(1);
  for(cnt=0;cnt<ERRORQUEUE_DEPTH/4;cnt++)
  {
    bk_error_printf(B, BK_ERR_ERR, "Error test 3 %d\n",cnt);
  }
  sleep(1);
  for(cnt=0;cnt<ERRORQUEUE_DEPTH/4;cnt++)
  {
    bk_error_printf(B, BK_ERR_WARN, "Warning test 3 %d\n",cnt);
  }

  bk_warn(B,stderr,"This is a warning\n",BK_FLAG_ISSET(pconfig->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  bk_die(B,254,stderr,"This is a fatal error\n",BK_FLAG_ISSET(pconfig->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  

  BK_VRETURN(B);
}
