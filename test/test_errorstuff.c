#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: test_errorstuff.c,v 1.9 2002/10/18 18:22:58 lindauer Exp $";
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
  BK_ENTRY_MAIN(B, __FUNCTION__, __FILE__, "SIMPLE");
  poptContext optCon = NULL;
  int c;
  int getopterr = 0;
  extern char *optarg;
  extern int optind;
  struct program_config Pconfig, *pconfig;
  const struct poptOption optionsTable[] = 
  {
    {"debug", 'd', POPT_ARG_NONE, NULL, 'd', "Turn on debugging", NULL },
    {"verbose", 'v', POPT_ARG_NONE, NULL, 'v', "Turn on verbose message", NULL },
    {"stderr", 'e', POPT_ARG_NONE, NULL, 'e', "Errors to stderr", NULL },
    {"syslog", 's', POPT_ARG_NONE, NULL, 's', "Errors to syslog", NULL },

    POPT_AUTOHELP
    POPT_TABLEEND
  };


  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD("BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, LOG_USER, 0)))
  {
    fprintf(stderr,"Could not perform basic initialization\n");
    exit(254);
  }

  bk_fun_reentry(B);

  for (c = 0; optionsTable[c].longName || optionsTable[c].shortName; c++)
  {
    if (optionsTable[c].descrip) (*((char **)&(optionsTable[c].descrip)))=(char*)optionsTable[c].descrip;
    if (optionsTable[c].argDescrip) (*((char **)&(optionsTable[c].argDescrip)))=(char*)optionsTable[c].argDescrip;
  }

  pconfig = &Pconfig;
  memset(pconfig,0,sizeof(*pconfig));

  if (!(optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not initialize options processing\n");
    bk_exit(B, 254);
  }

  while ((c = poptGetNextOpt(optCon)) >= 0)
  {
    switch (c)
    {
    case 'd':
      bk_error_config(B, BK_GENERAL_ERROR(B), 0, stderr, 0, BK_ERR_DEBUG, BK_ERROR_CONFIG_FH);	// Enable output of all error logs
      bk_general_debug_config(B, stderr, BK_ERR_NONE, 0);					// Set up debugging, from config file
      bk_debug_printf(B, "Debugging on\n");
      break;

    case 'v':
      bk_error_config(B, BK_GENERAL_ERROR(B), ERRORQUEUE_DEPTH, stderr, BK_ERR_NONE, BK_ERR_ERR, 0);
      break;

    case 'e':
      BK_FLAG_SET(pconfig->pc_flags, PC_STDERR);
      break;

    case 's':
      BK_FLAG_SET(pconfig->pc_flags, PC_SYSLOG);
      break;

    default:
      usage(B);
      break;
    }
  }

  /*
   * Reprocess so that argc and argv contain the remaining command
   * line arguments (note argv[0] is an argument, not the program
   * name).  argc remains the number of elements in the argv array.
   */
  argv = (char **)poptGetArgs(optCon);
  argc = 0;
  if (argv)
    for (; argv[argc]; argc++)
      ; // Void

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

  bk_error_config(B, BK_GENERAL_ERROR(B), ERRORQUEUE_DEPTH, fh, sysloglevel, BK_ERR_ERR, 
		  BK_ERROR_CONFIG_FH | BK_ERROR_CONFIG_SYSLOGTHRESHOLD | BK_ERROR_CONFIG_HILO_PIVOT);

  BK_RETURN(B, 0);
}



/*
 * Normal processing
 */
void progrun(bk_s B, struct program_config *pconfig)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
  int cnt;

  // test error aggregation
  for(cnt=0;cnt<1;cnt++)
  {
    bk_error_printf(B, BK_ERR_ERR, "Error aggregation test 1\n");
  }
  sleep(1);

  for(cnt=0;cnt<2;cnt++)
  {
    bk_error_printf(B, BK_ERR_ERR, "Error aggregation test 2\n");
  }
  sleep(1);

  for(cnt=0;cnt<ERRORQUEUE_DEPTH;cnt++)
  {
    bk_error_printf(B, BK_ERR_ERR, "Error aggregation test 3\n");
  }
  sleep(1);

#if 0
  // test regular error output
  for(cnt=0;cnt<ERRORQUEUE_DEPTH/2;cnt++)
  {
    bk_error_printf(B, BK_ERR_ERR, "Error test 2 %d\n",cnt);
  }
  sleep(1);
  for(cnt=0;cnt<ERRORQUEUE_DEPTH/3;cnt++)
  {
    bk_error_printf(B, BK_ERR_WARN, "Warning test 2 %d\n",cnt);
  }
  sleep(1);
  for(cnt=0;cnt<ERRORQUEUE_DEPTH/3;cnt++)
  {
    bk_error_printf(B, BK_ERR_ERR, "Error test 3 %d\n",cnt);
  }
  sleep(1);
  for(cnt=0;cnt<ERRORQUEUE_DEPTH/2;cnt++)
  {
    bk_error_printf(B, BK_ERR_WARN, "Warning test 3 %d\n",cnt);
  }
  sleep(1);
  for(cnt=0;cnt<ERRORQUEUE_DEPTH/4;cnt++)
  {
    bk_error_printf(B, BK_ERR_ERR, "Error test 4 %d\n",cnt);
  }
  sleep(1);
  for(cnt=0;cnt<ERRORQUEUE_DEPTH/4;cnt++)
  {
    bk_error_printf(B, BK_ERR_WARN, "Warning test 4 %d\n",cnt);
  }

  bk_warn(B,stderr,"This is a warning\n",BK_FLAG_ISSET(pconfig->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  bk_die(B,254,stderr,"This is a fatal error\n",BK_FLAG_ISSET(pconfig->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
#endif

  BK_VRETURN(B);
}
