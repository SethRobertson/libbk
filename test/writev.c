#if !defined(lint)
static const char libbk__copyright[] __attribute__((unused)) = "Copyright © 2006-2019";
static const char libbk__contact[] __attribute__((unused)) = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2006-2019 The Authors. All rights reserved.
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
 */
#include <libbk.h>
#include <libbk_i18n.h>


#define STD_LOCALEDIR_KEY     "LOCALEDIR"	///< Key in bkconfig to find the locale translation files
#define STD_LOCALEDIR_ENV     "BAKA_HOME"	///< Key in Environment to find base of locale directory
#define STD_LOCALEDIR_DEF     "/usr/local/baka"	///< Default base of where locale directory might be found
#define STD_LOCALEDIR_SUB     "locale"		///< Sub-component from install base where locale might be found
#define ERRORQUEUE_DEPTH      32		///< Default error queue depth
#define FILENAME	      "testfile"	///< Default filename to use


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
  int			pc_filesize;		///< How big the end file should be
  int			pc_vectorsize;		///< How big a writevector should be
  const char	       *pc_filename;		///< Default filename
  bk_flags		pc_flags;		///< Flags are fun!
#define PC_VERBOSE	0x001			///< Verbose output
#define PC_NODELETE	0x002			///< Do not delete testfile
#define PC_SYNC		0x004			///< Want O_SYNC
#define PC_FSYNC	0x008			///< Want FSYNC
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

    {"nodelete", 0, POPT_ARG_NONE, NULL, 'D', N_("Do not delete testfile"), NULL },
    {"sync", 0, POPT_ARG_NONE, NULL, 0x2001, N_("Want open with O_SYNC"), NULL },
    {"fsync", 0, POPT_ARG_NONE, NULL, 0x2003, N_("Want fsync after write"), NULL },
    {"filesize", 0, POPT_ARG_INT, NULL, 's', N_("Size of file to create"), N_("bytes") },
    {"filename", 0, POPT_ARG_STRING, NULL, 0x2002, N_("Name of file to use"), N_("filename") },
    {"vectorsize", 0, POPT_ARG_INT, NULL, 'S', N_("Size of each vector we write"), N_("bytes") },
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
  pc->pc_filename = FILENAME;

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

    case 'D':					// nodelete
      BK_FLAG_SET(pc->pc_flags, PC_NODELETE);
      break;
    case 0x2001:				// sync
      BK_FLAG_SET(pc->pc_flags, PC_SYNC);
      break;
    case 0x2002:				// filename
      pc->pc_filename = poptGetOptArg(optCon);
      break;
    case 0x2003:				// fsync
      BK_FLAG_SET(pc->pc_flags, PC_FSYNC);
      break;
    case 's':					// filesize
      {
	double tmplimit = bk_string_demagnify(B, poptGetOptArg(optCon), 0);
	if (isnan(tmplimit))
	  getopterr++;
	pc->pc_filesize = tmplimit;
      }
      break;
    case 'S':					// vectorsize
      {
	double tmplimit = bk_string_demagnify(B, poptGetOptArg(optCon), 0);
	if (isnan(tmplimit))
	  getopterr++;
	pc->pc_vectorsize = tmplimit;
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

  if (c < -1 || getopterr)
  {
    if (c < -1)
    {
      fprintf(stderr, "%s\n", poptStrerror(c));
    }
    poptPrintUsage(optCon, stderr, 0);
    bk_exit(B, 254);
  }

  if (pc->pc_filesize < 1)
  {
    fprintf(stderr, "Must specify file size\n");
    poptPrintUsage(optCon, stderr, 0);
    bk_exit(B, 254);
  }

  progrun(B, pc);

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
static void progrun(bk_s B, struct program_config *pc)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
  struct iovec *vectors;
  char *buffer;
  int numbuf;
  int i;
  int x;
  int flags = O_WRONLY|O_TRUNC|O_CREAT|O_LARGEFILE|O_NONBLOCK;
  struct timeval start, end, delta;
  char speed[128];

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_VRETURN(B);
  }

  if (BK_FLAG_ISSET(pc->pc_flags, PC_SYNC))
    flags |= O_SYNC;

  if ((x = open(pc->pc_filename,flags,0666)) < 0)
  {
    fprintf(stderr, "Could not open testfile: %s\n", strerror(errno));
    exit(2);
  }

  if (pc->pc_vectorsize < 1)
  {
    int byteswritten = 0;
    int bytestowrite = pc->pc_filesize;

    buffer = malloc(pc->pc_filesize);
    for(i=0;i<pc->pc_filesize;i++)
      buffer[i] = i;
    gettimeofday(&start, NULL);
    while (bytestowrite)
    {
      int ret = write(x,buffer+byteswritten,bytestowrite);

      if (ret < 1)
      {
	if (errno == EAGAIN)
	  continue;
	perror("write");
	exit(2);
      }

      bytestowrite -= ret;
      byteswritten += ret;
    }
  }
  else
  {
    int byteswritten = 0;
    int bytestowrite = pc->pc_filesize;

    numbuf = (pc->pc_filesize + pc->pc_vectorsize - 1) / pc->pc_vectorsize;
    vectors = malloc(numbuf * sizeof(*vectors));
    for(i=0;i<numbuf;i++)
    {
      int j;
      buffer = vectors[i].iov_base = malloc(pc->pc_vectorsize);
      for(j=0;j<pc->pc_vectorsize;j++)
	buffer[j] = j;
      vectors[i].iov_len = pc->pc_vectorsize;
    }
    // Adjust for possible last buffer resize
    vectors[numbuf-1].iov_len += pc->pc_filesize - pc->pc_vectorsize * numbuf;
    gettimeofday(&start, NULL);
    while (bytestowrite)
    {
      int offset = byteswritten / pc->pc_vectorsize;
      int ret;

      ret = writev(x,vectors+offset,MIN(numbuf-offset,1024));

      if (ret < 1)
      {
	if (errno == EAGAIN)
	  continue;
	perror("writev");
	exit(2);
      }

      bytestowrite -= ret;
      byteswritten += ret;

      for (i=offset;i<numbuf && ret > 0;i++)
      {
	if ((unsigned)ret > vectors[i].iov_len)
	  ret -= vectors[i].iov_len;
	else
	{
	  vectors[i].iov_len -= ret;
	  ret = 0;
	}
      }
    }
  }
  if (BK_FLAG_ISSET(pc->pc_flags, PC_FSYNC))
    fsync(x);
  close(x);
  gettimeofday(&end, NULL);

  BK_TV_SUB(&delta, &end, &start);

  bk_string_magnitude(B, (double)pc->pc_filesize/((double)delta.tv_sec + (double)delta.tv_usec/1000000.0), 3, "B/s", speed, sizeof(speed), 0);

  fprintf(stderr, "%d bytes written in %ld.%06ld seconds: %s\n", pc->pc_filesize, (long int) delta.tv_sec, (long int) delta.tv_usec, speed);


  if (BK_FLAG_ISCLEAR(pc->pc_flags, PC_NODELETE))
    unlink(pc->pc_filename);

  BK_VRETURN(B);
}
