#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: bdtee.c,v 1.4 2003/12/25 06:27:18 seth Exp $";
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
// XXX - customize here
#define PERSON_NAME	      N_("World")	///< Default name to greet
#define PERSON_KEY	      "Greeter"		///< Name to query in config to greet



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
  // XXX - customize here
  bk_flags		pc_flags;		///< Flags are fun!
#define PC_VERBOSE		0x001		///< Verbose output
#define PC_LAST_WRITE_NEWLINE	0x002		///< Last write to tee file terminated in '\n
#define PC_NO_TEE		0x004		///< No more write to tee file.
  char *		pc_prog1;
  char *		pc_prog2;
  struct bk_run	*	pc_run;			///< Run structure.
  char *		pc_outfile;		///< File to be created.
  struct bk_ioh *	pc_out_ioh;		///< IOH on output file.
  struct bk_ioh *	pc_peer1_ioh;		///< IOH of one peer (doesn't matter which).
  struct bk_ioh *	pc_last_peer;		///< IOH of the last peer to write
};

#define FROM_PEER1 "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<"
#define FROM_PEER2 ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>"

static int proginit(bk_s B, struct program_config *pc);
static void relay_tee(bk_s B, void *opaque, struct bk_ioh *read_ioh, struct bk_ioh *write_ioh, bk_vptr *data,  bk_flags flags);
static void out_file_handler(bk_s B, bk_vptr data[], void *opaque, struct bk_ioh *ioh, bk_ioh_status_e state_flags);


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
  BK_ENTRY_MAIN(B, __FUNCTION__, __FILE__, "bdtee");
  int c;
  int getopterr = 0;
  int debug_level = 0;
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

    POPT_AUTOHELP
    POPT_TABLEEND
  };

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD(NULL, "BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, LOG_LOCAL0, 0)))
  {
    fprintf(stderr,"Could not perform basic initialization\n");
    exit(254);
  }
  bk_fun_reentry(B);

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
  // XXX - customize here

  if (!(optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not initialize options processing\n");
    bk_exit(B, 254);
  }
  // XXX - customize here
  poptSetOtherOptionHelp(optCon, _("[NON-FLAG ARGUMENTS]"));

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

  if (argc < 3 )
    bk_die(B, 254, stderr, _("Not enough arguments\n"), BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);

  pc->pc_prog1=argv[0];
  pc->pc_prog2=argv[1];
  pc->pc_outfile=argv[2];

  // XXX - customize here (argv test)
  if (c < -1 || getopterr)
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

  if (bk_run_run(B,pc->pc_run, 0)<0)
  {
    bk_die(B, 1, stderr, "Failure during run_run\n", BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
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
  BK_ENTRY(B, __FUNCTION__,__FILE__,"bdtee");
  int prog1_in, prog1_out, prog2_in, prog2_out;
  struct bk_ioh *ioh1, *ioh2;
  int fd;

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_RETURN(B, -1);
  }

  if (!(pc->pc_run = bk_run_init(B, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create run struct\n");
    goto error;
  }

  // Top of file starts with a new line.
  BK_FLAG_SET(pc->pc_flags, PC_LAST_WRITE_NEWLINE);

  if ((fd = open(pc->pc_outfile, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not open %s: %s\n", pc->pc_outfile, strerror(errno));
    goto error;
  }

  if (!(pc->pc_out_ioh = bk_ioh_init(B, -1, fd, out_file_handler, pc, 0, 0, 0, pc->pc_run, BK_IOH_RAW | BK_IOH_STREAM)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create ioh for out file\n");
    goto error;
  }

  if (!BK_STREQ(pc->pc_prog1,"-"))
  {
    if (bk_pipe_to_cmd_tokenize(B, &prog1_in, &prog1_out, pc->pc_prog1, NULL, 0, NULL, NULL, NULL, BK_STRING_TOKENIZE_MULTISPLIT, BK_EXEC_FLAG_SEARCH_PATH | BK_EXEC_FLAG_CLOSE_CHILD_DESC) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not fork first program\n");
      goto error;
    }
  }
  else
  {
    prog1_in = fileno(stdin);
    prog1_out = fileno(stdout);
  }


  if (!BK_STREQ(pc->pc_prog2,"-"))
  {
    if (bk_pipe_to_cmd_tokenize(B, &prog2_in, &prog2_out, pc->pc_prog2, NULL, 0, NULL, NULL, NULL, BK_STRING_TOKENIZE_MULTISPLIT, BK_EXEC_FLAG_SEARCH_PATH | BK_EXEC_FLAG_CLOSE_CHILD_DESC) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not fork second program\n");
      goto error;
    }
  }
  else
  {
    prog2_in = fileno(stdin);
    prog2_out = fileno(stdout);
  }

  if (!(ioh1=bk_ioh_init(B, prog1_in, prog1_out, NULL, pc, 0, 0, 0, pc->pc_run, BK_IOH_RAW | BK_IOH_STREAM)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create ioh1 for first prog\n");
  }

  pc->pc_peer1_ioh = ioh1;

  if (!(ioh2=bk_ioh_init(B, prog2_in, prog2_out, NULL, pc, 0, 0, 0, pc->pc_run, BK_IOH_RAW | BK_IOH_STREAM)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create ioh1 for second prog\n");
  }

  if (bk_relay_ioh(B, ioh1, ioh2, relay_tee, pc, NULL, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create relay\n");
    goto error;
  }

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}



/**
 * Callback prototype for ioh relay. This is called once per read and once
 * one everything is shutdown. While the relay is active, the callback is
 * called when the data has been read but before it has been
 * written. During shutdown, indicated by a NULL data argument, the
 * read_ioh and write_ioh no longer have these meanings, but are supplied
 * for convenience.
 *
 *	@param B BAKA thread/global state.
 *	@param opaque Data supplied by the relay initiator (optional of course).
 *	@param read_ioh Where the data came from.
 *	@param flags Where the data is going.
 *	@param data The data to be relayed.
 *	@param flags Flags for future use.
 */
static void
relay_tee(bk_s B, void *opaque, struct bk_ioh *read_ioh, struct bk_ioh *write_ioh, bk_vptr *data,  bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"bdtee");
  bk_vptr *outbuf;
  struct program_config *pc = (struct program_config *)opaque;

  if (!pc || !read_ioh || !write_ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (data)
  {
    if (BK_FLAG_ISSET(pc->pc_flags, PC_NO_TEE))
      BK_VRETURN(B);

    // Copy data to output file.
    if (!BK_MALLOC(outbuf))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create buffer for copying data: %s\n", strerror(errno));
      goto error;
    }
    if (!BK_MALLOC_LEN(outbuf->ptr, data->len))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not copy output data: %s\n", strerror(errno));
      goto error;
    }
    memmove(outbuf->ptr, data->ptr, data->len);
    outbuf->len = data->len;

    if (pc->pc_last_peer != read_ioh)
    {
      bk_ioh_printf(B, pc->pc_out_ioh, "%s%s\n",
		    BK_FLAG_ISCLEAR(pc->pc_flags, PC_LAST_WRITE_NEWLINE)?"\n":"",
		    (read_ioh==pc->pc_peer1_ioh)?FROM_PEER1:FROM_PEER2);
    }

    pc->pc_last_peer = read_ioh;

    if (bk_ioh_write(B, pc->pc_out_ioh, outbuf, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Failed to write out buffer\n");
      goto error;
    }

    if (((char *)outbuf->ptr)[outbuf->len-1] == '\n')
      BK_FLAG_SET(pc->pc_flags, PC_LAST_WRITE_NEWLINE);
    else
      BK_FLAG_CLEAR(pc->pc_flags, PC_LAST_WRITE_NEWLINE);
  }
  else
  {
    // We're shuting down, so close the output file.
    // If a write error had occured then this ioh == NULL
    if (pc->pc_out_ioh)
      bk_ioh_close(B, pc->pc_out_ioh, 0);
    bk_run_set_run_over(B, pc->pc_run);
  }

 error:
  BK_VRETURN(B);
}



/**
 * Handler for output writes.
 *
 *	@param B BAKA Thread/global state
 *	@param data List of data to be relayed
 *	@param opaque Callback data
 *	@param ioh IOH data/activity came in on
 *	@param state_flags Type of activity
 */
static void out_file_handler(bk_s B, bk_vptr data[], void *opaque, struct bk_ioh *ioh, bk_ioh_status_e state_flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"bdtee");
  struct program_config *pc = (struct program_config *)opaque;

  if (!ioh || !pc) // opaque should be NULL
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  switch (state_flags)
  {
  case BkIohStatusIncompleteRead:
  case BkIohStatusReadComplete:
  case BkIohStatusIohReadError:
  case BkIohStatusIohReadEOF:
    bk_error_printf(B, BK_ERR_ERR, "Who put a read in my write only ioh!\n");
    break;

  case BkIohStatusIohWriteError:
    bk_error_printf(B, BK_ERR_ERR, "Error detected during write. Terminating tee. Continuing relay.\n");
    BK_FLAG_SET(pc->pc_flags, PC_NO_TEE);
    bk_ioh_close(B, pc->pc_out_ioh, 0);
    pc->pc_out_ioh = NULL;
    break;

  case BkIohStatusWriteComplete:
  case BkIohStatusWriteAborted:
    free(data[0].ptr);
    free(data);
    break;

  case BkIohStatusIohClosing:
  case BkIohStatusIohSeekSuccess:
  case BkIohStatusIohSeekFailed:
  case BkIohStatusNoStatus:
    break;
  }

  BK_VRETURN(B);
}
