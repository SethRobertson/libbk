#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: proto.c,v 1.12 2001/11/13 03:49:28 seth Exp $";
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
  const char * 		pc_person;		///< The person to greet
  bk_flags 		pc_flags;		///< Flags are fun!
#define PC_VERBOSE	1			///< Verbose output
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
    {"person", 'p', POPT_ARG_STRING, NULL, 'p', "Turn on debugging", NULL },
    {"verbose", 'v', POPT_ARG_NONE, NULL, 'v', "Turn on verbose message", NULL },
    {"long-arg-only", 0, POPT_ARG_NONE, NULL, 1, "An example of a long argument without a shortcut", NULL },
    {NULL, 's', POPT_ARG_NONE, NULL, 2, "An example of a short argument without a longcut", NULL },
    POPT_AUTOHELP
    POPT_TABLEEND
  };

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD("BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, BK_ERR_ERR, 0)))
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
      bk_debug_setconfig(B, BK_GENERAL_DEBUG(B), BK_GENERAL_CONFIG(B), BK_GENERAL_PROGRAM(B));
      bk_general_debug_config(B, stderr, BK_ERR_NONE, 0);
      bk_debug_printf(B, "Debugging on\n");
      break;
    case 'p':
      pconfig->pc_person = poptGetOptArg(optCon);
      break;
    case 'v':
      BK_FLAG_SET(pconfig->pc_flags, PC_VERBOSE);
      bk_error_config(B, BK_GENERAL_ERROR(B), ERRORQUEUE_DEPTH, stderr, BK_ERR_NONE, BK_ERR_ERR, 0);
      break;
    case 1:
      printf("You specificed the long-arg-only option\n");
      break;
    case 2:
      printf("You specificed the short only option\n");
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
    bk_die(B,254,stderr,"Could not perform program initialization\n",BK_FLAG_ISSET(pconfig->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  }

  progrun(B, pconfig);
  bk_exit(B,0);
  abort();
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
  const char *person = "World";

  if (!pconfig)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_VRETURN(B);
  }

  if (pconfig->pc_person)
    person = pconfig->pc_person;

  printf("Hello %s\n",person);

  BK_VRETURN(B);
}
