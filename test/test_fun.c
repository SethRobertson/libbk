#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: test_fun.c,v 1.7 2003/04/13 00:24:40 seth Exp $";
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
  int		gs_sysloglevel;
} Global = { 0, BK_ERR_NONE };


enum command
{
  noop,
  trace,
  resetdebug,
  badreturn,
  funon,
  funoff,
};


static int FUN_ON = 1;


int proginit(bk_s B);
void progrun(bk_s B);
void recurse(bk_s B, int levels, enum command cmd);
void recurse2(bk_s B, int levels, enum command cmd);



int
main(int argc, char **argv, char **envp)
{
  bk_s B = NULL;				/* Baka general structure */
  BK_ENTRY_MAIN(B, __FUNCTION__, __FILE__, "SIMPLE");
  char c;
  int getopterr=0;
  poptContext optCon=NULL;
  const char *arg;
  int debugging = 0;
  const struct poptOption optionsTable[] =
  {
    {"debug", 'd', POPT_ARG_NONE, NULL, 'd', "Turn on debugging", NULL },
    {"syslog-level", 's', POPT_ARG_INT|POPT_ARGFLAG_OPTIONAL, NULL, 's', "Syslog level (default 0=debug)", NULL },
    {"no-seatbelts", 'n', POPT_ARG_NONE, NULL, 'n', "Sealtbelts off & speed up", NULL },
    POPT_AUTOHELP
    POPT_TABLEEND
  };

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD(B, "BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, LOG_USER, 0)))
  {
    fprintf(stderr,"Could not perform basic initialization\n");
    exit(254);
  }
  bk_fun_reentry(B);

  optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, 0);

  while ((c = poptGetNextOpt(optCon)) >= 0)
  {
    switch (c)
    {
    case 'd':
      bk_error_config(B, BK_GENERAL_ERROR(B), 0, stderr, 0, 0, BK_ERROR_CONFIG_FH);	// Enable output of all error logs
      bk_general_debug_config(B, stderr, BK_ERR_NONE, 0);				// Set up debugging, from config file
      bk_debug_printf(B, "Debugging on\n");
      debugging = 1;
      break;
    case 'n':
      BK_FLAG_CLEAR(BK_GENERAL_FLAGS(B), BK_BGFLAGS_FUNON);
      FUN_ON = 0;
      break;
    case 's':
      arg = poptGetOptArg(optCon);
      Global.gs_sysloglevel = BK_ERR_DEBUG - atoi(arg ? arg : "0");
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

  if (!debugging)
  {
    bk_error_repeater_flush(B, 0);
    bk_error_dump(B,stderr,NULL,BK_ERR_DEBUG,BK_ERR_NONE,0);
  }

  bk_exit(B,0);
  abort();
  BK_RETURN(B,255);				/* Insight is stupid */
}



/*
 * Initialization
 */
int proginit(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "SIMPLE");

  BK_RETURN(B, 0);
}



/*
 * Normal processing
 */
void progrun(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
  int x;

  recurse(B, 9, badreturn);
  recurse(B, 9, trace);
  recurse(B, 9, funoff);
  if (FUN_ON)
    bk_fun_set(B, BK_FUN_OFF, 0);
  recurse(B, 999, badreturn);
  recurse(B, 999, trace);
  recurse(B, 999, resetdebug);
  recurse(B, 9, funon);
  if (FUN_ON)
    bk_fun_set(B, BK_FUN_ON, 0);
  for(x=0;x<9999;x++)
    recurse(B, 999, noop);
  recurse(B, 999, resetdebug);

  BK_VRETURN(B);
}



/*
 * Push down through the stack
 */
void recurse(bk_s B, int levels, enum command cmd)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "SIMPLE");

  if (levels)
  {
    recurse2(B, --levels, cmd);
  }

  BK_VRETURN(B);
}



/*
 * Push down further through the stack
 */
void recurse2(bk_s B, int levels, enum command cmd)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "SIMPLE");

  if (levels--)
  {
    recurse(B, levels, cmd);
  }
  else
    switch (cmd)
    {
    case noop:
    case badreturn:
      break;
    case trace:
      bk_fun_trace(B, stderr, Global.gs_sysloglevel, 0);
      break;
    case resetdebug:
      bk_general_debug_config(B, stderr, Global.gs_sysloglevel, 0);
      break;
    case funon:
      if (FUN_ON)
	bk_fun_set(B, BK_FUN_ON, 0);
      break;
    case funoff:
      if (FUN_ON)
	bk_fun_set(B, BK_FUN_OFF, 0);
      break;
    }

  if (cmd == badreturn)
    return;					// not BK_VRETURN!

  BK_VRETURN(B);
}
