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
  bk_flags	gs_flags;
} Global;



/**
 * Information about basic program runtime configuration
 * which must be passed around.
 */
struct program_config
{
  bk_url_parse_mode_e pc_parse_mode;
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
  BK_ENTRY_MAIN(B, __FUNCTION__, __FILE__, "SIMPLE");
  char c;
  int getopterr=0;
  extern char *optarg;
  extern int optind;
  struct program_config Pconfig, *pconfig=NULL;
  poptContext optCon=NULL;
  struct poptOption optionsTable[] =
  {
    {"debug", 'd', POPT_ARG_NONE, NULL, 'd', "Turn on debugging", NULL },
    {"vptr", 'v', POPT_ARG_NONE, NULL, 'v', "Use Vptr mode", NULL },
    {"vptr-copy", 'c', POPT_ARG_NONE, NULL, 'c', "Use Vptr Copy mode", NULL },
    {"str-null", 'n', POPT_ARG_NONE, NULL, 'n', "Use string NULL mode", NULL },
    {"str-empty", 'e', POPT_ARG_NONE, NULL, 'e', "Use string empty mode", NULL },
    POPT_AUTOHELP
    POPT_TABLEEND
  };

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD(B, "BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, LOG_USER, 0)))
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

  pconfig->pc_parse_mode = BkUrlParseStrNULL;	// default -n

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
      pconfig->pc_parse_mode = BkUrlParseVptr;
      break;
    case 'c':
      pconfig->pc_parse_mode = BkUrlParseVptrCopy;
      break;
    case 'n':
      pconfig->pc_parse_mode = BkUrlParseStrNULL;
      break;
    case 'e':
      pconfig->pc_parse_mode = BkUrlParseStrEmpty;
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

#define PRINT_ELEMENT(scratch, size, bu, element)			  \
do {									  \
  if (BK_URL_DATA((bu),(element)))					  \
  {									  \
    char *expanded = bk_url_unescape(B, BK_URL_DATA((bu),(element)));	  \
    int len =								  \
      snprintf((scratch),MIN((size),(BK_URL_LEN((bu),(element))+1)),"%s", \
			    BK_URL_DATA((bu),(element)));		  \
    snprintf((scratch)+len,(size)-len," |%s|", expanded);		  \
    free(expanded);							  \
  }									  \
  else									  \
  {									  \
    snprintf((scratch),(size),"Not Found");				  \
  }									  \
} while (0)





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
  char scratch[1024];
  char inputline[1024];
  struct bk_url *bu;
  u_int nextstart;
  char *params;
  char *parampath;
  char *frankenurl;
  static char *numbers[] =
    {
      "zero",
      "",
      "one",
      "two",
      "three",
      "baz",
      NULL
    };
  static char *letters[] =
    {
      "\0&",
      "a",
      "b",
      "C",
      "baz",
      NULL
    };

  while(fgets(inputline, 1024, stdin))
  {
    char *value;

    bk_string_rip(B, inputline, NULL, 0);

    if (BK_STREQ(inputline,"quit") || BK_STREQ(inputline,"exit"))
      BK_VRETURN(B);


    memset(scratch,0,sizeof(scratch));
    nextstart = 0;
    if (!(bu=bk_url_parse(B, inputline, pconfig->pc_parse_mode, BK_URL_FLAG_STRICT_PARSE)))
    {
      fprintf(stderr,"Could not convert url: %s\n", inputline);
      continue;
    }


    printf("URL: %s\n",bu->bu_url);
    PRINT_ELEMENT(scratch, sizeof(scratch), bu, bu->bu_scheme);
    printf("\tScheme: %s\n", scratch);
    PRINT_ELEMENT(scratch, sizeof(scratch), bu, bu->bu_authority);
    printf("\tAuthority: %s\n", scratch);
    PRINT_ELEMENT(scratch, sizeof(scratch), bu, bu->bu_host);
    printf("\t\tHost: %s\n", scratch);
    PRINT_ELEMENT(scratch, sizeof(scratch), bu, bu->bu_serv);
    printf("\t\tServ: %s\n", scratch);
    PRINT_ELEMENT(scratch, sizeof(scratch), bu, bu->bu_path);
    printf("\tPath: %s\n", scratch);
    parampath = BK_URL_PATH_DATA(bu);
    if (parampath && (params = strchr(parampath, ';')))
    {
      *params++ = '\0';				// break path and params
      while (*params != '\0')
      {
	int param;

	switch (param = bk_url_getparam(B, &params, numbers, &value))
	{
	case 0:
	case 1:
	case 2:
	case 3:
	  printf("\t\t%d = %s\n", param, value ? value : "");
	  break;
	case -1:
	  fprintf(stderr, "unrecognized param: %s\n", value ? value : "\"\"");
	}
      }
    }
    PRINT_ELEMENT(scratch, sizeof(scratch), bu, bu->bu_query);
    printf("\tQuery: %s\n", scratch);
    params = BK_URL_QUERY_DATA(bu);
    if (params)
    {
      while (*params != '\0')
      {
	int param;

	switch (param = bk_url_getparam(B, &params, letters, &value))
	{
	case 0:
	case 1:
	case 2:
	case 3:
	  printf("\t\t%c = %s\n", '@' + param, value ? value : "");
	  break;
	case -1:
	  fprintf(stderr, "unrecognized param: %s\n", value ? value : "\"\"");
	}
      }
    }
    PRINT_ELEMENT(scratch, sizeof(scratch), bu, bu->bu_fragment);
    printf("\tFragment: %s\n", scratch);

    frankenurl = bk_url_reconstruct(B, bu, BK_URL_FLAG_ALL, 0);
    printf("\treconstructed URL=%s\n", frankenurl);
    free(frankenurl);

    bk_url_destroy(B, bu);

  }

  BK_VRETURN(B);
}
