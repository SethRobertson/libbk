#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: test_config.c,v 1.9 2003/03/19 20:00:17 lindauer Exp $";
static const char libbk__copyright[] = "Copyright (c) 2001";
static const char libbk__contact[] = "<projectbaka@baka.org>";
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
  bk_flags	gs_flags;
} Global;
#define TESTCONFIG_FLAG_DISPLAY 	0x1


int proginit(bk_s B);
void progrun(bk_s B);



int
main(int argc, char **argv, char **envp)
{
  bk_s B = NULL;				/* Baka general structure */
  BK_ENTRY_MAIN(B, __FUNCTION__, __FILE__, "SIMPLE");
  char c;
  int getopterr=0;
  extern char *optarg;
  extern int optind;
  poptContext optCon=NULL;
  const struct poptOption optionsTable[] = 
  {
    {"debug", 'd', POPT_ARG_NONE, NULL, 'd', "Turn on debugging", NULL },
    {"show", 's', POPT_ARG_NONE, NULL, 's', "Show the current config file", NULL },
    POPT_AUTOHELP
    POPT_TABLEEND
  };

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD("BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, LOG_USER, 0)))
  {
    fprintf(stderr,"Could not perform basic initialization\n");
    exit(254);
  }
  bk_fun_reentry(B);

  memset(&Global,0, sizeof(Global));
  optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, 0);

  while ((c = poptGetNextOpt(optCon)) >= 0)
  {
    switch (c)
    {
    case 'd':
      bk_general_debug_config(B, stderr, BK_ERR_NONE, 0);
      bk_error_config(B, BK_GENERAL_ERROR(B), 0, stderr, 0, BK_ERR_NONE, BK_ERROR_CONFIG_FH|BK_ERROR_CONFIG_HILO_PIVOT);
      bk_error_repeater_flush(B, 0);
      bk_error_dump(B,stderr,NULL,BK_ERR_DEBUG,BK_ERR_NONE,0);
      bk_debug_printf(B, "Debugging on\n");
      break;
    case 's':
      BK_FLAG_SET(Global.gs_flags, TESTCONFIG_FLAG_DISPLAY);
      break;
    default:
      getopterr++;
      break;
    }
  }

  if (c < -1 || getopterr)
  {
    if (c < -1)
    {
      fprintf(stderr, "%s\n", poptStrerror(c));
    }
    poptPrintUsage(optCon, stderr, 0);
    bk_exit(B,254);
  }
    
  if (proginit(B) < 0)
  {
    bk_die(B,254,stderr,"Could not perform program initialization\n",0);
  }

  progrun(B);
  bk_exit(B,0);
  abort();
  BK_RETURN(B,255);				/* Insight is stupid */
}



/*
 * Initialization
 */
int proginit(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");

  BK_RETURN(B, 0);
}



/*
 * Normal processing
 */
void progrun(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");

  if (BK_FLAG_ISSET(Global.gs_flags, TESTCONFIG_FLAG_DISPLAY))
  {
    bk_config_print(B,NULL,stderr);
  }

  BK_VRETURN(B);
}


