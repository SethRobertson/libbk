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
 * XXX - customize here
 * Example libbk user with main() prototype.
 */
#include <libbk.h>
#include <libbk_i18n.h>


#define STD_LOCALEDIR_KEY     "LOCALEDIR"	///< Key in bkconfig to find the locale translation files
#define STD_LOCALEDIR_ENV     "BAKA_HOME"	///< Key in Environment to find base of locale directory
#define STD_LOCALEDIR_DEF     "/usr/local/baka"	///< Default base of where locale directory might be found
#define STD_LOCALEDIR_SUB     "locale"		///< Sub-component from install base where locale might be found
#define ERRORQUEUE_DEPTH      32		///< Default error queue depth



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
  struct bk_pnode      *pc_pn;			///< Patricia Trie
  bk_flags		pc_flags;		///< Flags are fun!
#define PC_VERBOSE	0x001			///< Verbose output
};



static int proginit(bk_s B, struct program_config *pc, int argc, char **argv);
static void progrun(bk_s B, struct program_config *pc);



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
  int debug_level = 0;
  char i18n_localepath[_POSIX_PATH_MAX];
  char *i18n_locale;
  struct program_config Pconfig, *pc = NULL;
  poptContext optCon = NULL;
  struct poptOption optionsTable[] =
  {
    {"debug", 'd', POPT_ARG_NONE, NULL, 'd', N_("Turn on debugging"), NULL },
    {"verbose", 'v', POPT_ARG_NONE, NULL, 'v', N_("Turn on verbose message"), NULL },
    {"no-seatbelts", 0, POPT_ARG_NONE, NULL, 0x1000, N_("Sealtbelts off & speed up"), NULL },
    {"seatbelts", 0, POPT_ARG_NONE, NULL, 0x1001, N_("Enable function tracing"), NULL },
    {"profiling", 0, POPT_ARG_STRING, NULL, 0x1002, N_("Enable and write profiling data"), N_("filename") },

    POPT_AUTOHELP
    POPT_TABLEEND
  };

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD(NULL, "BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, LOG_LOCAL0, 0)))
  {
    fprintf(stderr,"Could not perform basic initialization\n");
    exit(254);
  }
  bk_fun_reentry(B);

  // Enable error output
  bk_error_config(B, BK_GENERAL_ERROR(B), ERRORQUEUE_DEPTH, stderr, BK_ERR_ERR,
		  BK_ERR_ERR, BK_ERROR_CONFIG_FH |
		  BK_ERROR_CONFIG_SYSLOGTHRESHOLD | BK_ERROR_CONFIG_HILO_PIVOT);

  // i18n stuff
  setlocale(LC_ALL, "");
  if (!(i18n_locale = BK_GWD(B, STD_LOCALEDIR_KEY, NULL)))
  {
    i18n_locale = i18n_localepath;
    snprintf(i18n_localepath, sizeof(i18n_localepath), "%s/%s", BK_ENV_GWD(B, STD_LOCALEDIR_ENV,STD_LOCALEDIR_DEF), STD_LOCALEDIR_SUB);
  }
  bindtextdomain(BK_GENERAL_PROGRAM(B), i18n_locale);
  textdomain(BK_GENERAL_PROGRAM(B));
  for (c = 0; optionsTable[c].longName || optionsTable[c].shortName; c++)
  {
    if (optionsTable[c].descrip) (*((char **)&(optionsTable[c].descrip)))=_(optionsTable[c].descrip);
    if (optionsTable[c].argDescrip) (*((char **)&(optionsTable[c].argDescrip)))=_(optionsTable[c].argDescrip);
  }

  pc = &Pconfig;
  memset(pc, 0, sizeof(*pc));

  if (!(optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not initialize options processing\n");
    bk_exit(B, 254);
  }

  while ((c = poptGetNextOpt(optCon)) >= 0)
  {
    switch (c)
    {
    case 'd':					// debug
      if (!debug_level)
      {
	// Set up debugging, from config file
	bk_general_debug_config(B, stderr, BK_ERR_NONE, 0);
	bk_debug_printf(B, "Debugging on\n");
	debug_level++;
      }
      else if (debug_level == 1)
      {
	/*
	 * Enable output of error and higher error logs (this can be
	 * annoying so require -dd)
	 */
	bk_error_config(B, BK_GENERAL_ERROR(B), 0, stderr, BK_ERR_NONE, BK_ERR_ERR, BK_ERROR_CONFIG_FH | BK_ERROR_CONFIG_HILO_PIVOT | BK_ERROR_CONFIG_SYSLOGTHRESHOLD);
	bk_debug_printf(B, "Extra debugging on\n");
	debug_level++;
      }
      else if (debug_level == 2)
      {
	/*
	 * Enable output of all levels of bk_error logs (this can be
	 * very annoying so require -ddd)
	 */
	bk_error_config(B, BK_GENERAL_ERROR(B), 0, stderr, BK_ERR_NONE, BK_ERR_DEBUG, BK_ERROR_CONFIG_FH | BK_ERROR_CONFIG_HILO_PIVOT | BK_ERROR_CONFIG_SYSLOGTHRESHOLD);
	bk_debug_printf(B, "Super-extra debugging on\n");
	debug_level++;
      }
      break;
    case 'v':					// verbose
      BK_FLAG_SET(pc->pc_flags, PC_VERBOSE);
      bk_error_config(B, BK_GENERAL_ERROR(B), ERRORQUEUE_DEPTH, stderr, BK_ERR_NONE, BK_ERR_ERR, 0);
      break;
    case 0x1000:				// no-seatbelts
      BK_FLAG_CLEAR(BK_GENERAL_FLAGS(B), BK_BGFLAGS_FUNON);
      break;
    case 0x1001:				// seatbelts
      BK_FLAG_SET(BK_GENERAL_FLAGS(B), BK_BGFLAGS_FUNON);
      break;
    case 0x1002:				// profiling
      bk_general_funstat_init(B, (char *)poptGetOptArg(optCon), 0);
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
  argc = 0;
  if (argv)
    for (; argv[argc]; argc++)
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

  if (proginit(B, pc, argc, argv) < 0)
  {
    bk_die(B, 254, stderr, _("Could not perform program initialization\n"), BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  }

  progrun(B, pc);

  poptFreeContext(optCon);
  bk_exit(B, 0);
  return(255);					// Stupid INSIGHT stuff.
}



/**
 * General program initialization
 *
 *	@param B BAKA Thread/Global configuration
 *	@param pc Program configuration
 *	@return <i>0</i> Success
 *	@return <br><i>-1</i> Total terminal failure
 */
static int proginit(bk_s B, struct program_config *pc, int argc, char **argv)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_RETURN(B, -1);
  }

  pc->pc_pn = bk_patricia_create(B);

  BK_RETURN(B, 0);
}



/**
 * Normal processing of program
 *
 *	@param B BAKA Thread/Global configuration
 *	@param pc Program configuration
 *	@return <i>0</i> Success--program may terminate normally
 *	@return <br><i>-1</i> Total terminal failure
 */
static void progrun(bk_s B, struct program_config *pc)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
  void *conflict;
  long counter = 0;

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_VRETURN(B);
  }

#define TINSERT(k) { printf("Insert %s\n",k); conflict=NULL; if (bk_patricia_insert(B, pc->pc_pn, (k), (sizeof((k))-1)*8, (void *)++counter, &conflict) < 0) bk_die(B, 1, stderr, _("Could not insert\n"), BK_WARNDIE_WANTDETAILS); if (conflict) printf("Insert conflict: evicted %ld\n",(long)conflict);   /*bk_patricia_print(B,pc->pc_pn,stdout,0);*/ }
#define SEARCH(k,e) { void *data = bk_patricia_search(B, pc->pc_pn, (k), (sizeof((k))-1)*8); if ((long)data != (long)e) { fprintf(stderr,"Unexpected search result on %s %ld!=%ld\n", k,(long)data,(long)e); exit(1); } }

  TINSERT("");

  SEARCH("",1);
  SEARCH("aaaa",0);

  bk_patricia_print(B,pc->pc_pn,stdout,0);

  TINSERT("aaaa");
  SEARCH("",1);
  SEARCH("aaaa",2);
  SEARCH("cm002",0);
  TINSERT("a");
  SEARCH("",1);
  SEARCH("aaaa",2);
  SEARCH("a",3);
  SEARCH("cm002",0);
  TINSERT("a");
  SEARCH("",1);
  SEARCH("aaaa",2);
  SEARCH("a",4);
  SEARCH("cm002",0);
  TINSERT("b");
  SEARCH("",1);
  SEARCH("aaaa",2);
  SEARCH("a",4);
  SEARCH("b",5);
  SEARCH("c",0);
  SEARCH("cm002",0);
  TINSERT("c");
  SEARCH("",1);
  SEARCH("aaaa",2);
  SEARCH("a",4);
  SEARCH("b",5);
  SEARCH("c",6);
  SEARCH("aaa",0);
  SEARCH("aab",0);
  SEARCH("aac",0);
  SEARCH("bbbb",0);
  SEARCH("bbbbbbbb",0);
  SEARCH("bbbbbbbbb",0);
  SEARCH("bbbbbbb",0);
  SEARCH("cm002",0);
  TINSERT("aaa");
  SEARCH("",1);
  SEARCH("aaaa",2);
  SEARCH("a",4);
  SEARCH("b",5);
  SEARCH("c",6);
  SEARCH("aaa",7);
  SEARCH("aab",0);
  SEARCH("aac",0);
  SEARCH("bbbb",0);
  SEARCH("bbbbbbbb",0);
  SEARCH("bbbbbbbbb",0);
  SEARCH("bbbbbbb",0);
  SEARCH("cm002",0);
  TINSERT("aab");
  SEARCH("",1);
  SEARCH("aaaa",2);
  SEARCH("a",4);
  SEARCH("b",5);
  SEARCH("c",6);
  SEARCH("aaa",7);
  SEARCH("aab",8);
  SEARCH("aac",0);
  SEARCH("bbbb",0);
  SEARCH("bbbbbbbb",0);
  SEARCH("bbbbbbbbb",0);
  SEARCH("bbbbbbb",0);
  SEARCH("cm002",0);
  TINSERT("aac");
  SEARCH("",1);
  SEARCH("aaaa",2);
  SEARCH("a",4);
  SEARCH("b",5);
  SEARCH("c",6);
  SEARCH("aaa",7);
  SEARCH("aab",8);
  SEARCH("aac",9);
  SEARCH("bbbb",0);
  SEARCH("bbbbbbbb",0);
  SEARCH("bbbbbbbbb",0);
  SEARCH("bbbbbbb",0);
  SEARCH("cm002",0);
  TINSERT("bbbb");
  SEARCH("",1);
  SEARCH("aaaa",2);
  SEARCH("a",4);
  SEARCH("b",5);
  SEARCH("c",6);
  SEARCH("aaa",7);
  SEARCH("aab",8);
  SEARCH("aac",9);
  SEARCH("bbbb",10);
  SEARCH("bbbbbbbb",0);
  SEARCH("bbbbbbbbb",0);
  SEARCH("bbbbbbb",0);
  SEARCH("cm002",0);
  TINSERT("bbbbbbbb");
  SEARCH("",1);
  SEARCH("aaaa",2);
  SEARCH("a",4);
  SEARCH("b",5);
  SEARCH("c",6);
  SEARCH("aaa",7);
  SEARCH("aab",8);
  SEARCH("aac",9);
  SEARCH("bbbb",10);
  SEARCH("bbbbbbbb",11);
  SEARCH("bbbbbbbbb",0);
  SEARCH("bbbbbbb",0);
  SEARCH("cm002",0);
  TINSERT("bbbbbbbbb");
  SEARCH("",1);
  SEARCH("aaaa",2);
  SEARCH("a",4);
  SEARCH("b",5);
  SEARCH("c",6);
  SEARCH("aaa",7);
  SEARCH("aab",8);
  SEARCH("aac",9);
  SEARCH("bbbb",10);
  SEARCH("bbbbbbbb",11);
  SEARCH("bbbbbbbbb",12);
  SEARCH("bbbbbbb",0);
  SEARCH("cm002",0);

  bk_patricia_print(B,pc->pc_pn,stdout,0);

  TINSERT("\0");

  bk_patricia_print(B,pc->pc_pn,stdout,0);

  printf("Minimum/successor traversal\n");
  for(conflict=bk_patricia_minimum(B, pc->pc_pn);conflict;conflict=bk_patricia_successor(B, pc->pc_pn, conflict))
  {
    printf("%3ld\n", (long)conflict);
  }

#define PDELETE(k) { printf("Delete %s\n",(k)); bk_patricia_delete(B, pc->pc_pn, (k), (sizeof((k))-1)*8); bk_patricia_print(B, pc->pc_pn, stdout, 0); }
  PDELETE("cm002");
  // Pullup
  PDELETE("bbbbbbbb");
  // Leaf w/collapse
  PDELETE("c");
  // Pullup
  PDELETE("b");
  // Leaf
  PDELETE("bbbbbbbbb");
  // Leaf with collapse
  PDELETE("bbbb");
  // Root
  PDELETE("");
  // Leaf with collapse
  PDELETE("\0");
  // pullup
  PDELETE("a");
  // pullup
  PDELETE("aaa");
  // leaf with collapse
  PDELETE("aaaa");
  SEARCH("a",0);
  SEARCH("aab",8);
  // leaf with collapse
  PDELETE("aab");
  // root
  PDELETE("aac");
  TINSERT("bbbbbbbbb");
  SEARCH("aab",0);
  SEARCH("bbbbbbbbb",14);
  bk_patricia_print(B,pc->pc_pn,stdout,0);

  TINSERT("bbbbbbbb");
  TINSERT("aaa");
  TINSERT("b");
  TINSERT("aac");
  TINSERT("");
  TINSERT("bbbbbbbbb");
  TINSERT("a");
  TINSERT("\0");
  TINSERT("c");
  TINSERT("bbbb");
  TINSERT("aaaa");
  TINSERT("a");
  TINSERT("aab");
  bk_patricia_print(B,pc->pc_pn,stdout,0);

  while(counter = (long)bk_patricia_minimum(B, pc->pc_pn))
  {
    bk_patricia_vdelete(B, pc->pc_pn, (void *)counter);
  }
  bk_patricia_print(B,pc->pc_pn,stdout,0);

  TINSERT("bbbbbbbbb");
  bk_patricia_destroy(pc->pc_pn, NULL);

  BK_VRETURN(B);
}
