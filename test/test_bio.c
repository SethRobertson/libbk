#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: test_bio.c,v 1.7 2002/08/15 04:16:27 jtt Exp $";
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
  int			pc_cntl;		///< Control the on demand function.
  struct bk_iohh_bnbio *pc_bib;
  struct bk_run *	pc_run;
  bk_flags 		pc_flags;		///< Flags are fun!
#define PC_VERBOSE	0x001			///< Verbose output.
  void *		pc_on_demand;		///< On demand handle.
  const char *		pc_check;
  const char *		pc_output;
  const char *		pc_input;
  int			pc_check_fd;
  int			pc_output_fd;
};



static int proginit(bk_s B, struct program_config *pconfig);
static void progrun(bk_s B, struct program_config *pconfig);
static int do_read(bk_s B, struct bk_run *run, void *opaque, volatile int *demand, const struct timeval *starttime, bk_flags flags);



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
    {"output-file", 'o', POPT_ARG_STRING, NULL, 'o', "The file in which to dump the the output", "output-file" },
    {"input-file", 'i', POPT_ARG_STRING, NULL, 'i', "The file in which to dump the the input", "input-file" },
    {"checkpoint-file", 'c', POPT_ARG_STRING, NULL, 'c', "The file in which to dump the the checkpoint output", "checkpoint-file" },
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
    case 0x1000:				// no-seatbelts
      BK_FLAG_CLEAR(BK_GENERAL_FLAGS(B), BK_BGFLAGS_FUNON);
      break;
    case 'i':
      pconfig->pc_input = poptGetOptArg(optCon);
      break;
    case 'o':
      pconfig->pc_output = poptGetOptArg(optCon);
      break;
    case 'c':
      pconfig->pc_check = poptGetOptArg(optCon);
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
  int fd;
  struct bk_ioh *ioh;
  struct bk_iohh_bnbio *bib;
  struct bk_run *run;

  if (!pconfig)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_RETURN(B, -1);
  }

  if ((fd = open(pconfig->pc_input?pconfig->pc_input:"/usr/share/dict/words", O_RDONLY)) < 0)
  {
    perror("Could not open words");
    BK_RETURN(B,-1);    
  }

  if (!(run = bk_run_init(B, 0)))
  {
    fprintf(stderr,"Could not create run\n");
    BK_RETURN(B,-1);
  }
  pconfig->pc_run = run;

  // <TODO> make the mode an option </TODO>
  if (!(ioh = bk_ioh_init(B, fd, fd, NULL, NULL, 0, 0, 0, run, BK_IOH_RAW|BK_IOH_STREAM)))
  {
    fprintf(stderr,"Could not create ioh structure\n");
    BK_RETURN(B,-1);    
  }

  if (!(bib = bk_iohh_bnbio_create(B, ioh, 0)))
  {
    fprintf(stderr,"Could not creat blocking read structure\n");
    BK_RETURN(B,-1);    
  }
  pconfig->pc_bib = bib;

  if (bk_run_on_demand_add(B, run, do_read, pconfig, &pconfig->pc_cntl, &pconfig->pc_on_demand) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "\n");
    BK_RETURN(B,-1);    
  }

  pconfig->pc_cntl = 1;				// turn on on demand function.

  if (pconfig->pc_check)
  {
    if ((pconfig->pc_check_fd = open(pconfig->pc_check, O_WRONLY | O_TRUNC | O_CREAT, 0600)) < 0 )
    {
      perror("opening check point file");
      BK_RETURN(B,-1);      
    }
  }

  if (pconfig->pc_output)
  {
    if ((pconfig->pc_output_fd = open(pconfig->pc_output, O_WRONLY | O_TRUNC | O_CREAT, 0600)) < 0 )
    {
      perror("opening output file");
      BK_RETURN(B,-1);      
    }
  }

  BK_RETURN(B, 0);
}



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

  if (!pconfig)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_VRETURN(B);
  }

  if (bk_run_run(B, pconfig->pc_run, 0) < 0)
  {
    fprintf(stderr, "run run died");
    exit(1);
  }

  BK_VRETURN(B);
}



/**
 * Callback type for on demand functions
 * 
 *	@param B BAKA thread/global state 
 *	@param run The @a bk_run structure to use.
 *	@param opaque User args passed back.
 *	@param demand The flag which when raised causes this function to run.
 *	@param starttime The start time of the latest invokcation of @a bk_run_once.
 *	@param flags Flags for your enjoyment.
 */
static int 
do_read(bk_s B, struct bk_run *run, void *opaque, volatile int *demand, const struct timeval *starttime, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
  struct program_config *pc = opaque;
  bk_vptr *data = NULL;
  int ret;
  int done = 0;
  char line[1024];

  if (!run || !pc || !demand || !starttime)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  while (!done)
  {
    printf("\ncmd> ");
    fflush(stdout);

    fgets(line, 1024, stdin);
    bk_string_rip(B, line, NULL, 0);
    

    *demand = 0;
    if (BK_STREQ(line, "read") || BK_STREQ(line, "readall"))
    {
    reread:
      if ((ret = bk_iohh_bnbio_read(B, pc->pc_bib, &data, 0)) < 0)
      {
	fprintf(stderr,"Could not read data from ioh\n");
	exit(1);
      }
  
      if (!data)
      {
	if (!ret)
	{
	  printf("EOF seen\n");
	  //<TODO> Call clean up later </TODO>
	  done = 1;
	}
	else
	{
	  printf("Saw no data with return of: %d\n", ret);
	}
      }
      else
      {
	write(pc->pc_output_fd, data->ptr, data->len);
	bk_polling_io_data_destroy(B, data);
	if (BK_STREQ(line,"readall"))
	  goto reread;
      }
    }
    else if (BK_STREQ(line, "tell"))
    {
      printf("Tell: %lld\n", bk_iohh_bnbio_tell(B, pc->pc_bib, 0));
    }
    else if (BK_STREQ(line,"seek"))
    {
      u_int offset;
      printf(" offset> ");
      fflush(stdout);
      fgets(line, 1024, stdin);

      
      if (bk_string_atou(B, line, &offset, 0) != 0)
      {
	fprintf(stderr,"Bad integer: %s\n", line);
	continue;
      }
      if (bk_iohh_bnbio_seek(B, pc->pc_bib, offset, SEEK_SET, 0) < 0)
      {
	fprintf(stderr,"Seek failed\n");
	done = 1;
      }
    }
    else if (BK_STREQ(line, "exit") || BK_STREQ(line, "quit"))
    {
      done = 1;
    }
    *demand = 1;
  }

  *demand = 0;

  bk_iohh_bnbio_close(B, pc->pc_bib, BK_IOHH_BNBIO_FLAG_LINGER);
  // Must do this *after* lingering close
  bk_run_on_demand_remove(B, pc->pc_run, pc->pc_on_demand);
  bk_run_set_run_over(B, pc->pc_run);

  BK_RETURN(B,0);
}
