#if !defined(lint)
static const char libbk__copyright[] __attribute__((unused)) = "Copyright © 2001-2019";
static const char libbk__contact[] __attribute__((unused)) = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2001-2019 The Authors. All rights reserved.
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
 * Check bloom filter
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
  struct bk_bloomfilter *pc_bloom;	   	///< The actual bloom filter
  const char *		pc_bloomfile;		///< The bloom filter file
  int32_t		pc_hashen;		///< Number of hash filters
  int64_t		pc_bloomsize;		///< Size of bloom filter
  bk_flags		pc_flags;		///< Flags are fun!
#define PC_VERBOSE	0x001			///< Verbose output
#define PC_BINARY	0x002			///< Input is binary on stdin, not a string on command line
#define PC_ADD		0x004			///< Add hashen, not check
};



static int proginit(bk_s B, struct program_config *pc, int argc, char **argv);
static void progrun(bk_s B, struct program_config *pc, int argc, char **argv);



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

    {"bloomfile", 'b', POPT_ARG_STRING, NULL, 'b', N_("Filename pointing to bloom filter"), N_("file") },
    {"add", 'a', POPT_ARG_NONE, NULL, 'a', N_("Add object to bloom filter"), NULL },
    {"numhash", 'h', POPT_ARG_STRING, NULL, 'h', N_("Number of hashes used in bloom filter"), N_("number") },
    {"size", 'm', POPT_ARG_STRING, NULL, 'm', N_("Size of bloom filter, in bits"), N_("bitsize") },
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
  poptSetOtherOptionHelp(optCon, _("[string-to-check-in-bloom]"));

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

    case 'b':					// bloom file
      pc->pc_bloomfile = poptGetOptArg(optCon);
      break;
    case 'h':
      pc->pc_hashen = atoi(poptGetOptArg(optCon));
      break;
    case 'm':
      bk_string_atoi64(B, poptGetOptArg(optCon), &pc->pc_bloomsize, 0);
      break;
    case 'a':					// add
      BK_FLAG_SET(pc->pc_flags, PC_ADD);
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

  if (c < -1 || getopterr || !pc->pc_bloomfile || pc->pc_hashen < 1 || pc->pc_bloomsize < 1 || argc > 1)
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

  progrun(B, pc, argc, argv);

  bk_bloomfilter_destroy(B, pc->pc_bloom);

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

  if (!(pc->pc_bloom = bk_bloomfilter_create(B, pc->pc_hashen, pc->pc_bloomsize, pc->pc_bloomfile, BK_BLOOMFILTER_CREATABLE|(BK_FLAG_ISSET(pc->pc_flags, PC_ADD)?BK_BLOOMFILTER_WRITABLE:0))))
    bk_die(B, 254, stderr, _("Could not open bloom filter\n"), 1);

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
static void progrun(bk_s B, struct program_config *pc, int argc, char **argv)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
  struct bk_memx *memx = NULL;
  char *buf = NULL;
  u_int len = 0;

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_VRETURN(B);
  }

  if (argc)
  {
    buf = argv[0];
    len = strlen(buf);
  }
  else
  {
    char inbuf[8192];
    int readlen;

    if (!(memx = bk_memx_create(B, 1, 8192, 8192, 0)))
      bk_die(B, 254, stderr, _("Could not creat memx\n"), 1);

    while ((readlen = read(0, inbuf, sizeof(inbuf))) > 0)
    {
      buf = bk_memx_get(B, memx, readlen, NULL, BK_MEMX_GETNEW);
      memcpy(buf,inbuf,readlen);
    }
    if (!(buf = bk_memx_get(B, memx, 0, &len, 0)) || len < 1)
    {
      if (!buf)
	bk_die(B, 254, stderr, _("Could not get data just read\n"), 1);
      bk_die(B, 254, stderr, _("No input data\n"), 1);
    }
  }

  if (BK_FLAG_ISSET(pc->pc_flags, PC_ADD))
  {
    if (bk_bloomfilter_add(B, pc->pc_bloom, buf, len) < 0)
      bk_die(B, 254, stderr, _("Could not add key\n"), 1);
  }
  else
  {
    int present;

    if (BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE))
    {
      fprintf(stderr, "Bits set for key: ");
      bk_bloomfilter_printkey(B, pc->pc_bloom, buf, len, stderr);
      fprintf(stderr, "\n");
    }

    if ((present = bk_bloomfilter_is_present(B, pc->pc_bloom, buf, len)) < 0)
      bk_die(B, 254, stderr, _("Could not lookup key\n"), 1);

    if (present)
    {
      if (BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE))
	printf("Key is present\n");
      exit(0);
    }
    else
    {
      if (BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE))
	printf("Key is missing\n");
      exit(0);
    }
  }

  if (memx)
    bk_memx_destroy(B, memx, 0);

  BK_VRETURN(B);
}
