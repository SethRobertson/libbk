#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: test_fun.c,v 1.1 2002/03/18 21:19:07 dupuy Exp $";
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
  bk_flags	gs_flags;
} Global;


enum command
{
  noop,
  trace,
  resetdebug,
  badreturn,
};

int proginit(bk_s B);
void progrun(bk_s B);
void recurse(bk_s B, int levels, enum command cmd);



int
main(int argc, char **argv, char **envp)
{
  bk_s B = NULL;				/* Baka general structure */
  BK_ENTRY(B, __FUNCTION__, __FILE__, "SIMPLE");
  char c;
  int getopterr=0;
  extern char *optarg;
  extern int optind;
  poptContext optCon=NULL;
  const struct poptOption optionsTable[] = 
  {
    {"debug", 'd', POPT_ARG_NONE, NULL, 'd', "Turn on debugging)", NULL },
    {"no-seatbelts", 'n', POPT_ARG_NONE, NULL, 'n', "Sealtbelts off & speed up", NULL },
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
      bk_debug_setconfig(B,BK_GENERAL_DEBUG(B), BK_GENERAL_CONFIG(B),BK_GENERAL_PROGRAM(B));
      bk_error_config(B, BK_GENERAL_ERROR(B), 0, stderr, 0, BK_ERR_DEBUG, BK_ERROR_CONFIG_FH|BK_ERROR_CONFIG_HILO_PIVOT);
      bk_error_dump(B,stderr,NULL,BK_ERR_DEBUG,BK_ERR_ERR,0);
      bk_general_debug_config(B, stderr, BK_ERR_NONE, 0);
      bk_debug_printf(B, "Debugging on\n");
      break;
    case 's':
      BK_FLAG_CLEAR(BK_GENERAL_FLAGS(B), BK_BGFLAGS_FUNON);
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

  recurse(B, 10, badreturn);
  recurse(B, 10, trace);
  bk_fun_set(B, BK_FUN_OFF, 0);
  recurse(B, 1000, badreturn);
  recurse(B, 1000, trace);
  recurse(B, 1000, resetdebug);
  bk_fun_set(B, BK_FUN_ON, 0);
  recurse(B, 1000, noop);
  recurse(B, 1000, resetdebug);

  BK_VRETURN(B);
}



void recurse(bk_s B, int levels, enum command cmd)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "SIMPLE");

  if (levels)
  {
    recurse(B, --levels, cmd);
  }
  else
    switch (cmd)
    {
    case noop:
    case badreturn:
      break;
    case trace:
      bk_fun_trace(B, stderr, BK_ERR_DEBUG, 0);
      break;
    case resetdebug:
      bk_general_debug_config(B, stderr, BK_ERR_DEBUG, 0);
      break;
    }

  if (cmd == badreturn)
    return;					// not BK_VRETURN!

  BK_VRETURN(B);
}
