#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: test_url.c,v 1.5 2001/12/11 21:36:08 seth Exp $";
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

/**
 * @file
 *
 * Example libbk user with main() prototype.
 */
#include <libbk.h>


#define ERRORQUEUE_DEPTH 32			///< Default depth

/**
 * Information of international importance to everyone
 * which cannot be passed around.
 */
struct global_structure
{
} Global;



/**
 * Information about basic program runtime configuration
 * which must be passed around.
 */
struct program_config
{
};



int proginit(bk_s B, struct program_config *pconfig);
void progrun(bk_s B, struct program_config *pconfig);



/**
 * Program entry point
 *
 *	@param argc Number of argv elements
 *	@param argv Program name and arguments
 *	@param envp Program environment
 *	@return <i>0</i> Success
 *	@return <br><i>254</i> Initialization failed
 */
int
main(int argc, char **argv, char **envp)
{
  bk_s B = NULL;				/* Baka general structure */
  BK_ENTRY(B, __FUNCTION__, __FILE__, "SIMPLE");
  char c;
  int getopterr=0;
  extern char *optarg;
  extern int optind;
  struct program_config Pconfig, *pconfig=NULL;
  poptContext optCon=NULL;
  const struct poptOption optionsTable[] = 
  {
    {"debug", 'd', POPT_ARG_NONE, NULL, 'd', "Turn on debugging", NULL },
    POPT_AUTOHELP
    POPT_TABLEEND
  };

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD("BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, LOG_LOCAL0, 0)))
  {
    fprintf(stderr,"Could not perform basic initialization\n");
    exit(254);
  }
  bk_fun_reentry(B);

  pconfig = &Pconfig;
  memset(pconfig,0,sizeof(*pconfig));

  if (!(optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not initialize options processing\n");
    bk_exit(B,254);
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
    
  if (proginit(B, pconfig) < 0)
  {
    bk_die(B,254,stderr,"Could not perform program initialization\n",0);
  }

  progrun(B, pconfig);
  bk_exit(B,0);
  abort();
  return(255);
}



/**
 * General program initialization
 *
 *	@param B BAKA Thread/Global configuration
 *	@param pconfig Program configuration
 *	@return <i>0</i> Success
 *	@return <br><i>-1</i> Total terminal failure
 */
int proginit(bk_s B, struct program_config *pconfig)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");

  if (!pconfig)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_RETURN(B, -1);
  }

  BK_RETURN(B, 0);
}



/**
 * Normal processing of program
 *
 *	@param B BAKA Thread/Global configuration
 *	@param pconfig Program configuration
 *	@return <i>0</i> Success--program may terminate normally
 *	@return <br><i>-1</i> Total terminal failure
 */
void progrun(bk_s B, struct program_config *pconfig)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
  char outputline[1024];
  char inputline[1024];
  struct bk_url *bu;
  u_int nextstart;
  
  while(fgets(inputline, 1024, stdin))
  {
    bk_string_rip(B, inputline, NULL, 0);

    if (BK_STREQ(inputline,"quit") || BK_STREQ(inputline,"exit"))
      BK_VRETURN(B);
      

    memset(outputline,0,1024);
    nextstart = 0;
    if (!(bu=bk_url_parse(B, inputline, 0)))
    {
      fprintf(stderr,"Could not convert url: %s\n", inputline);
      continue;
    }

    if (!BK_STREQ(bu->bu_proto,""))
    {
      snprintf(outputline, 1024, "%s://", bu->bu_proto);
      nextstart = strlen(outputline);
    }

    if (!BK_STREQ(bu->bu_host,""))
    {
      snprintf(outputline + nextstart, 1024 - nextstart, "%s", bu->bu_host);
      nextstart = strlen(outputline);
    }

    if (!BK_STREQ(bu->bu_serv,""))
    {
      snprintf(outputline + nextstart, 1024 - nextstart, ":%s", bu->bu_serv);
      nextstart = strlen(outputline);
    }
    
    if (!BK_STREQ(bu->bu_path,""))
    {
      snprintf(outputline + nextstart, 1024 - nextstart, "%s", bu->bu_path);
    }

    printf("%-20s %-20s > %s ** %s ** %s ** %s\n", inputline, outputline,  bu->bu_proto, bu->bu_host, bu->bu_serv, bu->bu_path);
  }
  
  BK_VRETURN(B);
}
