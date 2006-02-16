#if !defined(lint) && !defined(__INSIGHT__)
#include "libbk_compiler.h"
UNUSED static const char libbk__rcsid[] = "$Id: allocate_actual.c,v 1.2 2006/02/16 20:09:00 seth Exp $";
UNUSED static const char libbk__copyright[] = "Copyright (c) 2003";
UNUSED static const char libbk__contact[] = "<projectbaka@baka.org>";
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
 * Figure out actual memory allocations size usage
 */
#include <libbk.h>
#include <libbk_i18n.h>


#define STD_LOCALEDIR_KEY     "LOCALEDIR"	///< Key in bkconfig to find the locale translation files
#define STD_LOCALEDIR_ENV     "BAKA_HOME"	///< Key in Environment to find base of locale directory
#define STD_LOCALEDIR_DEF     "/usr/local/baka"	///< Default base of where locale directory might be found
#define STD_LOCALEDIR_SUB     "locale"		///< Sub-component from install base where locale might be found
#define ERRORQUEUE_DEPTH      32		///< Default error queue depth
#define MAXBUMPS	      5			///< Maximum number of bumps



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
  u_int			pc_maxbumps;		///< Maximum number of pages to test
  int			pc_size;		///< Size of allocation
  bk_flags		pc_flags;		///< Flags are fun!
#define PC_VERBOSE	0x001			///< Verbose output
};



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

    {"size", 's', POPT_ARG_STRING, NULL, 's', N_("Size of allocation to test (required)"), N_("size") },
    {"tests", 't', POPT_ARG_STRING, NULL, 't', N_("Size of tests to run"), N_("tests") },
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
  pc->pc_maxbumps = MAXBUMPS;

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

    case 's':					// size
      pc->pc_size = atoi(poptGetOptArg(optCon));
      break;
    case 't':					// number of tests
      pc->pc_maxbumps = atoi(poptGetOptArg(optCon));
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

  if (c < -1 || getopterr || pc->pc_size < 1)
  {
    if (c < -1)
    {
      fprintf(stderr, "%s\n", poptStrerror(c));
    }
    fprintf(stderr,"Note you must use the -s argument (%d)\n", pc->pc_size);
    poptPrintUsage(optCon, stderr, 0);
    bk_exit(B, 254);
  }

  progrun(B, pc);

  poptFreeContext(optCon);
  bk_exit(B, 0);
  return(255);					// Stupid INSIGHT stuff.
}



#ifndef RUSAGE_WORKED
long linux_get_memusage(void);
long linux_get_memusage(void)
{
#define STATUSSIZE 16384
#define STATUS "/proc/self/status"
  static char buf[STATUSSIZE];
  int fd = open(STATUS, O_RDONLY);
  char *parse;
  int len;

  if (fd < 0)
  {
    fprintf(stderr,"Cannot open linux status %s: %s\n", STATUS, strerror(errno));
    exit(2);
  }

  if ((len = read(fd, buf, STATUSSIZE-1)) < 1)
  {
    fprintf(stderr,"Cannot read linux status %s: %s\n", STATUS, strerror(errno));
    exit(2);
  }
  close(fd);
  buf[len] = 0;

#define NEEDLE "VmSize:"
  if (!(parse = strstr(buf, NEEDLE)))
  {
    fprintf(stderr,"Bad linux status %s: %s\n", STATUS, buf);
    exit(2);
  }

  parse += strlen(NEEDLE);
  while (*parse && (*parse == ' ' || *parse == '\t'))
    parse++;
  return(atoi(parse)*1024);
}
#endif /* RUSAGE_WORKED */



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
  long lowest, curmem, newmem;
#ifdef RUSAGE_WORKED
  struct rusage usage;
#endif /* RUSAGE */
  u_int sum_allocs = 0;
  u_int allocations = 0;
  u_int tests = 0;

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_VRETURN(B);
  }

  lowest = 0;
#ifdef RUSAGE_WORKED
  getrusage(RUSAGE_SELF, &usage);
  curmem = usage.ru_idrss;
#else /* RUSAGE */
  curmem = linux_get_memusage();
#endif /* RUSAGE */

  while (1)
  {
    if (!malloc(pc->pc_size))
    {
      perror("malloc");
      BK_VRETURN(B);
    }

    allocations++;

#ifdef RUSAGE_WORKED
    getrusage(RUSAGE_SELF, &usage);
    newmem = usage.ru_idrss;
#else /* RUSAGE */
    newmem = linux_get_memusage();
#endif /* RUSAGE */
    //    printf("%d %ld %ld\n", allocations, curmem, newmem);

    if (newmem != curmem)
    {
      if (lowest == 0)
      {
	lowest = newmem;
	allocations = 0;
      }
      if (tests++ > 0)
      {
	printf("For requests of size %d, I saw %d in %ld, for average size of %.2f\n", pc->pc_size,allocations-1,newmem-curmem,(newmem-curmem)/(allocations-1.0));
	sum_allocs += allocations;
	curmem = newmem;
	if (tests > pc->pc_maxbumps)
	  break;
      }
      curmem = newmem;
      allocations = 1;
    }
  }
  printf("SUMMARY: For requests of size %d, I saw %d in %ld, for average size of %.2f\n", pc->pc_size,sum_allocs-1,newmem-lowest,(newmem-lowest)/(sum_allocs-1.0));

  BK_VRETURN(B);
}
