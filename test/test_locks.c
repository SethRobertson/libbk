#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: test_locks.c,v 1.5 2002/08/15 04:16:27 jtt Exp $";
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

/**
 * @file
 *
 * Example libbk user with main() prototype.
 */
#include <libbk.h>
#include <libbk_i18n.h>


#define STD_LOCALEDIR_KEY     "LOCALEDIR"	///< Key in bkconfig to find the locale translation files
#define STD_LOCALEDIR_ENV     "BAKA_HOME"	///< Key in Environment to find base of locale directory
#define STD_LOCALEDIR_DEF     "/usr/local/baka"	///< Default base of where locale directory might be found
#define STD_LOCALEDIR_SUB     "locale"		///< Sub-component from install base where locale might be found
#define ERRORQUEUE_DEPTH      32		///< Default error queue depth
#define PERSON_NAME	      N_("World")	///< Default name to greet
#define PERSON_KEY	      "Greeter"		///< Name to query in config to greet



/**
 * Information of international importance to everyone
 * which cannot be passed around.
 */
struct global_structure
{
} Global;


#define DEFAULT_LOCK "/tmp/jtt-lck"

/**
 * Information about basic program runtime configuration
 * which must be passed around.
 */
struct program_config
{
  bk_flags 		pc_flags;		///< Flags are fun!
#define PC_VERBOSE	0x001			///< Verbose output
  char *		pc_resource;		///< Resource to lock
};



static int proginit(bk_s B, struct program_config *pconfig);
static void progrun(bk_s B, struct program_config *pconfig);



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
  int c;
  int getopterr = 0;
  char i18n_localepath[_POSIX_PATH_MAX], *i18n_locale = NULL;
  struct program_config Pconfig, *pconfig = NULL;
  poptContext optCon = NULL;
  const struct poptOption optionsTable[] = 
  {
    {"debug", 'd', POPT_ARG_NONE, NULL, 'd', N_("Turn on debugging"), NULL },
    {"verbose", 'v', POPT_ARG_NONE, NULL, 'v', N_("Turn on verbose message"), NULL },
    {"no-seatbelts", 0, POPT_ARG_NONE, NULL, 0x1000, N_("Sealtbelts off & speed up"), NULL },
    {"resource", 'r', POPT_ARG_NONE, NULL, 'r', N_("Set the resource"), "resource" },
    POPT_AUTOHELP
    POPT_TABLEEND
  };

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD("BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, LOG_USER, 0)))
  {
    fprintf(stderr,"Could not perform basic initialization\n");
    exit(254);
  }
  bk_fun_reentry(B);

  // i18n stuff
  setlocale(LC_ALL, "");
  if (!(i18n_locale = BK_GWD(B, STD_LOCALEDIR_KEY, NULL)) && (i18n_locale = (char *)&i18n_localepath))
    snprintf(i18n_localepath, sizeof(i18n_localepath), "%s/%s", BK_ENV_GWD(STD_LOCALEDIR_ENV,STD_LOCALEDIR_DEF), STD_LOCALEDIR_SUB);
  bindtextdomain(BK_GENERAL_PROGRAM(B), i18n_locale);
  textdomain(BK_GENERAL_PROGRAM(B));
  for (c = 0; optionsTable[c].longName || optionsTable[c].shortName; c++)
  {
    if (optionsTable[c].descrip) (*((char **)&(optionsTable[c].descrip)))=_(optionsTable[c].descrip);
    if (optionsTable[c].argDescrip) (*((char **)&(optionsTable[c].argDescrip)))=_(optionsTable[c].argDescrip);
  }

  pconfig = &Pconfig;
  memset(pconfig, 0, sizeof(*pconfig));

  pconfig->pc_resource = DEFAULT_LOCK;

  if (!(optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not initialize options processing\n");
    bk_exit(B, 254);
  }
  poptSetOtherOptionHelp(optCon, _("[NON-FLAG ARGUMENTS]"));

  while ((c = poptGetNextOpt(optCon)) >= 0)
  {
    switch (c)
    {
    case 'd':					// debug
      bk_error_config(B, BK_GENERAL_ERROR(B), 0, stderr, 0, 0, BK_ERROR_CONFIG_FH);	// Enable output of all error logs
      bk_general_debug_config(B, stderr, BK_ERR_NONE, 0); 				// Set up debugging, from config file
      bk_debug_printf(B, "Debugging on\n");
      break;
    case 'v':					// verbose
      BK_FLAG_SET(pconfig->pc_flags, PC_VERBOSE);
      bk_error_config(B, BK_GENERAL_ERROR(B), ERRORQUEUE_DEPTH, stderr, BK_ERR_NONE, BK_ERR_ERR, 0);
      break;
    default:
      getopterr++;
      break;

    }
  }

  /*
   * Reprocess so that argc and argv contain the remaining command
   * line arguments (note argv[0] is an argument, not the program
   * name).  argc remains the number of elements in the argv array.
   */
  argv = (char **)poptGetArgs(optCon);
  if (!(argc = 0) && argv)
    for (argc=0; argv[argc]; argc++)
      ; // Void

  if (c < -1 || getopterr)
  {
    if (c < -1)
    {
      fprintf(stderr, "%s\n", poptStrerror(c));
    }
    poptPrintUsage(optCon, stderr, 0);
    bk_exit(B, 254);
  }
    
  if (proginit(B, pconfig) < 0)
  {
    bk_die(B, 254, stderr, _("Could not perform program initialization\n"), BK_FLAG_ISSET(pconfig->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  }

  progrun(B, pconfig);
  bk_exit(B, 0);
  return(255);					// Stupid INSIGHT stuff.
}



/**
 * General program initialization
 *
 *	@param B BAKA Thread/Global configuration
 *	@param pconfig Program configuration
 *	@return <i>0</i> Success
 *	@return <br><i>-1</i> Total terminal failure
 */
static int proginit(bk_s B, struct program_config *pconfig)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");

  if (!pconfig)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_RETURN(B, -1);
  }

  BK_RETURN(B, 0);
}



struct lock_info
{
  char *		li_id;			///< Lock id (typed in by user).
  void *		li_fl;			///< Lock info returned by libbk.
};



/**
 * Normal processing of program
 *
 *	@param B BAKA Thread/Global configuration
 *	@param pconfig Program configuration
 *	@return <i>0</i> Success--program may terminate normally
 *	@return <br><i>-1</i> Total terminal failure
 */
static void progrun(bk_s B, struct program_config *pconfig)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
  int done = 0;
  char line[1024];
  char **args;
  char *cmd;
  bk_file_lock_type_e lock_type;
  dict_h lock_list;
  struct lock_info *li;
  void *fl = NULL;
  

  if (!pconfig)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_VRETURN(B);
  }

  if (!(lock_list = dll_create(NULL, NULL, DICT_UNORDERED)))
  {
    fprintf(stderr,"Could not create lock list\n");
    BK_VRETURN(B);    
  }

  for(; !done; bk_string_tokenize_destroy(B, args))
  {
    printf("\ncmd> ");
    fflush(stdout);
    fgets(line, sizeof(line), stdin);

    bk_string_rip(B, line, NULL, 0);
    if (!(args = bk_string_tokenize_split(B, line, 10, NULL, NULL, 0)))
    {
      fprintf(stderr,"Could not tokenize string: %s\n", line);
      continue;
    }

    cmd = args[0];

    if (BK_STREQ(cmd, "quit") || BK_STREQ(cmd, "exit"))
    {
      done = 1;
    }
    else if (BK_STREQ(cmd,"lock"))
    {
      int held;

      if (!args[1] || !args[2])
      {
	fprintf(stderr, "Badly formatted \"lock\" command\n");
	continue;
      }

      if (BK_STREQ(args[2],BK_FILE_LOCK_MODE_SHARED))
	lock_type = BkFileLockTypeShared;
      else
	lock_type = BkFileLockTypeExclusive;

      if (!(fl = bk_file_lock(B, pconfig->pc_resource, lock_type, NULL, NULL, &held, 0)))
      {
	if (held)
	{
	  fprintf(stderr, "Could not aquire lock: held by another party\n");
	}
	else
	{
	  fprintf(stderr, "Could not aquire lock: major failure\n");
	}
	continue;
      }
      else
      {
	if (!(BK_CALLOC(li)))
	{
	  perror("malloc'ing lock info");
	  goto error;
	}
	if (!(li->li_id = strdup(args[1])))
	{
	  perror("stdup'ing id");
	  goto error;
	}
	li->li_fl = fl;
	if (dll_insert(lock_list, li) != DICT_OK)
	{
	  fprintf(stderr,"Could not insert li on list: %s\n", dll_error_reason(lock_list, NULL)) ;
	  goto error;
	}
      }
      printf("Lock acquired\n");
    }
    else if (BK_STREQ(cmd,"release"))
    {
      if (!args[1])
      {
	fprintf(stderr, "Badly formatted release\n");
	continue;
      }

      for(li = dll_minimum(lock_list); li; li = dll_successor(lock_list, li))
      {
	if (BK_STREQ(li->li_id, args[1]))
	{
	  break;
	}
      }
      if (!li)
      {
	fprintf(stderr,"Could not locate lock: %s", args[1]);
	continue;;
      }
      dll_delete(lock_list, li);
      if (bk_file_unlock(B, li->li_fl, 0) < 0)
      {
	fprintf(stderr,"Could not release lock!\n");
      }
      else
      {
	printf("Lock released\n");
      }
      free(li->li_id);
      free(li);
    }
  }


 error:
  BK_VRETURN(B);
}
