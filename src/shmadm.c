#if !defined(lint) && !defined(__INSIGHT__)
#include "libbk_compiler.h"
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
 * Administer shared memory connections
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
  const char *		pc_filename;		///< Filename of IPC
  struct bk_shmipc     *pc_bsi;			///< Shared memory info
  bk_flags		pc_flags;		///< Flags are fun!
#define PC_VERBOSE	0x001			///< Verbose output
#define PC_FORCE	0x002			///< Force attach even at potentially unsafe times
#define PC_REMOVE	0x004			///< Remove rendevouz point
};



static int progrun(bk_s B, struct program_config *pc);



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

    {"force", 'f', POPT_ARG_NONE, NULL, 'f', N_("Force snoop even if unsafe"), NULL },
    {"remove", 'R', POPT_ARG_NONE, NULL, 'R', N_("Remove shared memory IPC"), NULL },
    {"write", 'w', POPT_ARG_NONE, NULL, 'w', N_("Write side of cat (otherwise is read side)"), NULL },
    {"rendevouz", 'r', POPT_ARG_STRING, NULL, 'r', N_("Shared memory rendevouz name"), N_("name") },
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

    case 'R':					// Remove
      BK_FLAG_SET(pc->pc_flags, PC_REMOVE);
      break;
    case 'f':					// rendevouz name
      BK_FLAG_CLEAR(pc->pc_flags, PC_FORCE);
      break;
    case 'r':					// rendevouz name
      pc->pc_filename = poptGetOptArg(optCon);
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

  if (c < -1 || getopterr || !pc->pc_filename)
  {
    if (c < -1)
    {
      fprintf(stderr, "%s\n", poptStrerror(c));
    }
    poptPrintUsage(optCon, stderr, 0);
    bk_exit(B, 254);
  }

  if (BK_FLAG_ISSET(pc->pc_flags, PC_REMOVE))
  {
    if (bk_shmipc_remove(B, pc->pc_filename, 0) < 0)
    {
      bk_die(B, 254, stderr, _("Could not remove shared memory name\n"), BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
    }
    bk_exit(B, 0);
  }

  if (progrun(B, pc) < 0)
  {
    bk_die(B, 254, stderr, _("Could not run\n"), BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  }

  poptFreeContext(optCon);
  bk_exit(B, 0);
  return(255);					// Stupid INSIGHT stuff.
}



/**
 * Normal processing of program
 *
 *	@param B BAKA Thread/Global configuration
 *	@param pc Program configuration
 *	@return <i>0</i> Success--program may terminate normally
 *	@return <br><i>-1</i> Total terminal failure
 */
static int progrun(bk_s B, struct program_config *pc)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
  u_int32_t magic, generation, ringsize, offset, writehand, readhand;
  size_t bytesreadable, byteswritable, segsize;
  int numothers;
  int ret;

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_RETURN(B, -1);
  }

  switch (ret = bk_shmipc_peekbyname(B, pc->pc_filename, &magic, &generation, &ringsize, &offset, &writehand, &readhand, &bytesreadable, &byteswritable, &numothers, &segsize, BK_FLAG_ISSET(pc->pc_flags, PC_FORCE)?BK_SHMIPC_FORCE:0))
  {
  case -1:
    bk_die(B, 254, stderr, _("Could not gather IPC information\n"), BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
    break;
  case 3:
    printf("Warning: insufficient people attached, stat attach might fool writer.\n--force required to get other information\n");
    printf("%40s: %s\n", "Name", pc->pc_filename);
    printf("%40s: %d\n", "Number of attached people", (int)numothers);
    printf("%40s: %d\n", "Segment size", (int)segsize);
    break;
  case 1:
    printf("Warning: no-one attached, information might be bogus\n");
    goto printinfo;
  case 2:
    printf("Warning: no-one attached or magic numbers incorrect, information definately bogus\n");
    goto printinfo;
  case 0:
  printinfo:
    printf("%40s: %s\n", "Name", pc->pc_filename);
    printf("%40s: %d\n", "Number of attached people", (int)numothers);
    printf("%40s: %d\n", "Segment size", (int)segsize);
    printf("%40s: %x\n", "Magic number", (int)magic);
    if (BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE))
    {
      printf("%40s: %x\n", "Magic number (SYN)", 0xfeedface);
      printf("%40s: %x\n", "Magic number (SYN-ACK)", 0xfacefedd);
      printf("%40s: %x\n", "Magic number (CONNECTED)", 0xabadcafe);
      printf("%40s: %x\n", "Magic number (RST)", 0xdeadbeef);
    }
    printf("%40s: %u\n", "Generation", (u_int)generation);
    printf("%40s: %d\n", "Ring size", (int)ringsize);
    printf("%40s: %d\n", "Ring offset", (int)offset);
    printf("%40s: %d\n", "Write hand", (int)writehand);
    printf("%40s: %d\n", "Read hand", (int)readhand);
    printf("%40s: %d\n", "Bytes readable", (int)bytesreadable);
    printf("%40s: %d\n", "Bytes writable", (int)byteswritable);
  }

  BK_RETURN(B, 0);
}
