#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: runsize.c,v 1.2 2004/06/04 02:50:11 seth Exp $";
static const char libbk__copyright[] = "Copyright (c) 2003";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright LIBBK++
 *
 * Copyright (c) 2003 The Authors. All rights reserved.
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
 * Run the command line problem for every size input bytes
 */
#include <libbk.h>
#include <libbk_i18n.h>


#define STD_LOCALEDIR_KEY     "LOCALEDIR"	///< Key in bkconfig to find the locale translation files
#define STD_LOCALEDIR_ENV     "BAKA_HOME"	///< Key in Environment to find base of locale directory
#define STD_LOCALEDIR_DEF     "/usr/local/baka"	///< Default base of where locale directory might be found
#define STD_LOCALEDIR_SUB     "locale"		///< Sub-component from install base where locale might be found
#define ERRORQUEUE_DEPTH      32		///< Default error queue depth
#define MAXSIZE		      65536		///< Buffer read size



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
  u_quad_t		pc_size;		///< Size of input
  bk_flags		pc_flags;		///< Flags are fun!
#define PC_VERBOSE	0x001			///< Verbose output
#define PC_SUBST	0x002			///< Want substitution
};



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
  char buf[MAXSIZE];
  u_quad_t cur;
  pid_t childpid = 0;
  int verbose_level = 0;
  int status, counter = 0;
  int infd = -1;
  char i18n_localepath[_POSIX_PATH_MAX];
  char *i18n_locale;
  struct program_config Pconfig, *pc = NULL;
  poptContext optCon = NULL;
  const struct poptOption optionsTable[] =
  {
    {"debug", 'd', POPT_ARG_NONE, NULL, 'd', N_("Turn on debugging"), NULL },
    {"verbose", 'v', POPT_ARG_NONE, NULL, 'v', N_("Turn on verbose message"), NULL },
    {"no-seatbelts", 0, POPT_ARG_NONE, NULL, 0x1000, N_("Sealtbelts off & speed up"), NULL },
    {"seatbelts", 0, POPT_ARG_NONE, NULL, 0x1001, N_("Enable function tracing"), NULL },
    {"profiling", 0, POPT_ARG_STRING, NULL, 0x1002, N_("Enable and write profiling data"), N_("filename") },

    {"size", 's', POPT_ARG_STRING, NULL, 's', N_("Number of bytes for a particular program"), N_("bytes") },
    {"counter", 'c', POPT_ARG_NONE, NULL, 'c', N_("Want counter %d substitution"), NULL },
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
  poptSetOtherOptionHelp(optCon, _("<program> [args]..."));

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
      if (++verbose_level == 1)
      {
	bk_error_config(B, BK_GENERAL_ERROR(B), 0, NULL, BK_ERR_NONE,
			BK_ERR_WARN, BK_ERROR_CONFIG_HILO_PIVOT);
      }
      if (verbose_level == 2)
      {
	bk_error_config(B, BK_GENERAL_ERROR(B), 0, NULL, BK_ERR_NONE,
			BK_ERR_NOTICE, BK_ERROR_CONFIG_HILO_PIVOT);
      }
      if (verbose_level == 3)
      {
	bk_error_config(B, BK_GENERAL_ERROR(B), 0, NULL, BK_ERR_NONE,
			BK_ERR_NONE, BK_ERROR_CONFIG_FLAGS|BK_ERROR_FLAG_MORE_FUN);
      }
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

    case 'c':					// Counter substitution
      BK_FLAG_SET(pc->pc_flags, PC_SUBST);
      break;

    case 's':					// size
      {
	double size = bk_string_demagnify(B, poptGetOptArg(optCon), 0);
	pc->pc_size = size;
      }
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

  if (c < -1 || getopterr || pc->pc_size < 1 || argc < 1)
  {
    if (c < -1)
    {
      fprintf(stderr, "%s\n", poptStrerror(c));
    }
    poptPrintUsage(optCon, stderr, 0);
    bk_exit(B, 254);
  }

  cur = pc->pc_size;
  while (1)
  {
    int retcode;
    u_quad_t diff;
    u_int readsize;
    int deferred_open = 0;
    u_int offset = 0;

    if (cur >= pc->pc_size)
    {
      deferred_open = 1;
      cur = 0;
    }

    diff = pc->pc_size-cur;
    readsize = MAXSIZE;
    if (readsize > diff)
      readsize = diff;

    if ((retcode = read(0, buf, readsize)) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Read error: %s\n", strerror(errno));
      bk_die(B, 254, stderr, _("Read failed\n"), BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
    }

    if (retcode == 0)
    {
      bk_error_printf(B, BK_ERR_NOTICE, "Got EOF\n");
      break;
    }

    if (deferred_open)
    {
      char **dupargv = argv;

      if (infd >= 0)
	close(infd);
      infd = -1;

      if (childpid > 0)
      {
	waitpid(childpid, &status, 0);
	bk_error_printf(B, BK_ERR_NOTICE, "%d Exited with status %d\n", childpid, status);
      }

      if (BK_FLAG_ISSET(pc->pc_flags, PC_SUBST))
      {
	if (!BK_MALLOC_LEN(dupargv, sizeof(char *)*argc))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Malloc failed: %s\n", strerror(errno));
	  bk_die(B, 254, stderr, _("Memory failed\n"), BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
	}
	for(retcode = 0; argv[retcode]; retcode++)
	{
	  if (!(dupargv[retcode] = bk_string_alloc_sprintf(B, 0, 0, argv[retcode], counter)))
	  {
	    bk_error_printf(B, BK_ERR_ERR, "alloc sprintf failed: %s\n", strerror(errno));
	    bk_die(B, 254, stderr, _("Memory failed\n"), BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
	  }
	}
	dupargv[retcode] = NULL;
	counter++;
      }

      if ((childpid = bk_pipe_to_exec(B, NULL, &infd, dupargv[0], dupargv, NULL, BK_EXEC_FLAG_CLOSE_CHILD_DESC|BK_EXEC_FLAG_SEARCH_PATH)) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Execute error--could not fork a new child\n");
	bk_die(B, 254, stderr, _("Execute failed\n"), BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
      }
      bk_error_printf(B, BK_ERR_NOTICE, "Started process %s got process %d\n", argv[0], childpid);
      deferred_open = 0;
      if (BK_FLAG_ISSET(pc->pc_flags, PC_SUBST))
      {
	for(retcode = 0; dupargv[retcode]; retcode++)
	  free(dupargv[retcode]);
	free(dupargv);
      }
    }

    offset = 0;
    readsize = retcode;
    while (readsize > offset && (retcode = write(infd, buf+offset, readsize - offset)) >= 0)
    {
      bk_debug_printf_and(B, 1,"Wrote %d bytes\n", retcode);
      offset += retcode;
      cur += retcode;
    }

    if (retcode < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Write error: %s\n", strerror(errno));
      bk_die(B, 254, stderr, _("Write failed\n"), BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
    }
  }
  if (infd >= 0)
    close(infd);
  if (childpid > 0)
    waitpid(childpid, &status, 0);

  poptFreeContext(optCon);
  bk_exit(B, 0);
  return(255);					// Stupid INSIGHT stuff.
}
