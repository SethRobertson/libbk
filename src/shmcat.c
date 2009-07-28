#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2006-2008";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2006-2008 The Authors. All rights reserved.
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
  const char *		pc_filename;		///< Filename of IPC
  struct bk_shmipc     *pc_bsi;			///< Shared memory info
  u_quad_t		pc_cntr;		///< Bytes transferred
  u_quad_t		pc_ops;			///< Number of transfer operations
  u_int			pc_timeout;		///< Timeout in us
  u_int			pc_poll;		///< How often to check for progress
  u_int			pc_length;		///< Length of shared memory
  u_int			pc_size;		///< Size of I/O buffers
  u_int			pc_sourcebufs;		///< Number of source buffers to generate
  bk_flags		pc_flags;		///< Flags are fun!
#define PC_VERBOSE	0x001			///< Verbose output
#define PC_RDONLY	0x002			///< Read activity
#define PC_SINK		0x004			///< Discard data from write side (reader only)
};



static int proginit(bk_s B, struct program_config *pc);
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

    {"source", 0, POPT_ARG_INT, NULL, 0x100, N_("Source fake data to remote side"), N_("number of i/o buffers") },
    {"sink", 0, POPT_ARG_NONE, NULL, 0x101, N_("Sink/discard data from remote side"), NULL },
    {"write", 'w', POPT_ARG_NONE, NULL, 'w', N_("Write side of cat (otherwise is read side)"), NULL },
    {"rendevouz", 'r', POPT_ARG_STRING, NULL, 'r', N_("Shared memory rendevouz name"), N_("name") },
    {"timeout", 't', POPT_ARG_INT, NULL, 't', N_("Timeout"), N_("microseconds") },
    {"poll", 'p', POPT_ARG_INT, NULL, 'p', N_("How often to check for activity"), N_("microseconds") },
    {"length", 'l', POPT_ARG_INT, NULL, 'l', N_("Size of shared memory buffer"), N_("bytes") },
    {"size", 's', POPT_ARG_INT, NULL, 's', N_("Size of i/o memory buffer"), N_("bytes") },
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
  pc->pc_size = 4096;
  BK_FLAG_SET(pc->pc_flags, PC_RDONLY);

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

    case 0x100:					// source
      pc->pc_sourcebufs = bk_string_demagnify(B, poptGetOptArg(optCon), 0);
      break;
    case 0x101:					// sink
      BK_FLAG_SET(pc->pc_flags, PC_SINK);
      break;
    case 'w':					// rendevouz name
      BK_FLAG_CLEAR(pc->pc_flags, PC_RDONLY);
      break;
    case 'r':					// rendevouz name
      pc->pc_filename = poptGetOptArg(optCon);
      break;
    case 't':					// timeout usec
      pc->pc_timeout = bk_string_demagnify(B, poptGetOptArg(optCon), 0);
      break;
    case 'p':					// poll usec
      pc->pc_poll = bk_string_demagnify(B, poptGetOptArg(optCon), 0);
      break;
    case 'l':					// shmem length
      pc->pc_length = bk_string_demagnify(B, poptGetOptArg(optCon), 0);
      break;
    case 's':					// i/o size
      pc->pc_size = bk_string_demagnify(B, poptGetOptArg(optCon), 0);
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

  if (proginit(B, pc) < 0)
  {
    bk_die(B, 254, stderr, _("Could not perform program initialization\n"), BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
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
 * General program initialization
 *
 *	@param B BAKA Thread/Global configuration
 *	@param pc Program configuration
 *	@return <i>0</i> Success
 *	@return <br><i>-1</i> Total terminal failure
 */
static int proginit(bk_s B, struct program_config *pc)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_RETURN(B, -1);
  }

  if (!(pc->pc_bsi = bk_shmipc_create(B, pc->pc_filename, pc->pc_timeout, pc->pc_timeout, pc->pc_poll, pc->pc_length, 0700, NULL, BK_FLAG_ISSET(pc->pc_flags, PC_RDONLY)?BK_SHMIPC_RDONLY:BK_SHMIPC_WRONLY)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not attach to shared memory IPC\n");
    BK_RETURN(B, -1);
  }
  fprintf(stderr, "Connected\n");

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
static int progrun(bk_s B, struct program_config *pc)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
  char *buf = NULL;
  int len;
  struct timeval start, end, delta;
  char speed[128];
  u_int buffersize;
  int highwater = 0;

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_RETURN(B, -1);
  }

  if (!BK_MALLOC_LEN(buf, pc->pc_size))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate %d memory: %s\n", pc->pc_size, strerror(errno));
    BK_RETURN(B, -1);
  }

  gettimeofday(&start, NULL);

  if (BK_FLAG_ISCLEAR(pc->pc_flags, PC_RDONLY))
  {
    if (pc->pc_sourcebufs)
    {
      for (len=0;len<(int)pc->pc_size;len++)
	buf[len] = len;

      highwater = pc->pc_size;
      while (pc->pc_sourcebufs--)
      {
	pc->pc_ops++;
	if (bk_shmipc_write(B, pc->pc_bsi, buf, len, 0, BK_SHMIPC_WRITEALL) < 1)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Shared memory write failed: %s\n", strerror(bk_shmipc_errno(B, pc->pc_bsi, 0)));
	  BK_RETURN(B, -1);
	}
	pc->pc_cntr += len;
      }
    }
    else
    {
      while ((len = read(0, buf, pc->pc_size)) > 0)
      {
	if (bk_shmipc_write(B, pc->pc_bsi, buf, len, 0, BK_SHMIPC_WRITEALL) < 1)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Shared memory write failed: %s\n", strerror(bk_shmipc_errno(B, pc->pc_bsi, 0)));
	  BK_RETURN(B, -1);
	}
	highwater = MAX(highwater,len);
	pc->pc_ops++;
	pc->pc_cntr += len;
      }
      if (len < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not read: %s\n", strerror(errno));
	BK_RETURN(B, -1);
      }
    }
  }
  else
  {
    while ((len = bk_shmipc_read(B, pc->pc_bsi, buf, pc->pc_size, 0, 0)) > 0)
    {
      highwater = MAX(highwater,len);
      pc->pc_ops++;
      if (BK_FLAG_ISSET(pc->pc_flags, PC_SINK))
      {
	pc->pc_cntr += len;
      }
      else
      {
	while (len)
	{
	  int progress;
	  char *cbuf = buf;

	  if ((progress = write(1, cbuf, len)) < 1)
	  {
	    bk_error_printf(B, BK_ERR_ERR, "Write failed: %s\n", strerror(errno));
	    BK_RETURN(B, -1);
	  }
	  pc->pc_cntr += progress;
	  len -= progress;
	  cbuf += progress;
	}
      }
    }
    if (len < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Shared memory read failed: %s\n", strerror(bk_shmipc_errno(B, pc->pc_bsi, 0)));
      BK_RETURN(B, -1);
    }
  }

  gettimeofday(&end, NULL);
  BK_TV_SUB(&delta, &end, &start);

  bk_string_magnitude(B, (double)pc->pc_cntr/((double)delta.tv_sec + (double)delta.tv_usec/1000000.0), 3, "B/s", speed, sizeof(speed), 0);
  fprintf(stderr, "%llu bytes transferred in %ld.%06ld seconds: %s\n", (unsigned long long)pc->pc_cntr, (long int) delta.tv_sec, (long int) delta.tv_usec, speed);

  bk_shmipc_peek(B, pc->pc_bsi, NULL, NULL, &buffersize, NULL, 0);

  bk_string_magnitude(B, (double)pc->pc_cntr/(double)pc->pc_ops, 3, "B/op", speed, sizeof(speed), 0);
  fprintf(stderr, "Transferred in %llu operations with %u sized shmbuf: %s\n", (unsigned long long)pc->pc_ops, buffersize,  speed);
  fprintf(stderr, "High water mark: %d bytes\n", highwater);

  bk_shmipc_destroy(B, pc->pc_bsi, 0);

  free(buf);
  buf = NULL;

  BK_RETURN(B, 0);
}
