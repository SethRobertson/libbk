#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2001-2010";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2001-2010 The Authors. All rights reserved.
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
 * Shared memory mapping administrative interface
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
  struct bk_shmmap     *pc_shmmap;		////< Shared memory
  char		       *pc_shmname;		///< Shared memory name
  bk_flags		pc_flags;		///< Flags are fun!
#define PC_VERBOSE	0x001			///< Verbose output
#define PC_UNLINK	0x002			///< Verbose output
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

    {"unlink", 'u', POPT_ARG_NONE, NULL, 'u', N_("Unlink shmmap"), NULL },
    {"name", 'n', POPT_ARG_STRING, &Pconfig.pc_shmname, 0, N_("Shared memory to attach to"), N_("shm name") },
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

    case 'u':					// unlink
      BK_FLAG_SET(pc->pc_flags, PC_UNLINK);
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

  if (!pc->pc_shmname)
  {
    fprintf(stderr,"--name option is required\n");
    poptPrintUsage(optCon, stderr, 0);
    bk_exit(B, 254);
  }

  if (BK_FLAG_ISSET(pc->pc_flags, PC_UNLINK))
  {
    int ret = 0;

    if (mq_unlink(pc->pc_shmname))
    {
      if (errno != ENOENT)
      {
	fprintf(stderr, "Could not unlink message queue %s: %s\n",pc->pc_shmname,strerror(errno));
	ret++;
      }
    }
    if (shm_unlink(pc->pc_shmname))
    {
      if (errno != ENOENT)
      {
	fprintf(stderr, "Could not unlink shared memory %s: %s\n",pc->pc_shmname,strerror(errno));
	ret++;
      }
    }
    exit(ret);
  }

  // Potentially recoverable.  Should we have a retry option?
  mqd_t mq = mq_open(pc->pc_shmname, O_WRONLY, 0, NULL);

  if (mq == -1)
  {
    fprintf(stderr,"Could not open message queue %s: %s: Ignoring\n", pc->pc_shmname, strerror(errno));
  }

  int shmfd = shm_open(pc->pc_shmname, O_RDONLY, 0);

  if (shmfd == -1)
  {
    fprintf(stderr,"Could not open message queue %s: %s\n", pc->pc_shmname, strerror(errno));
    exit(1);
  }


  int size = sizeof(struct bk_shmmap_header);
  struct bk_shmmap_header *sm_addr = NULL;

  if ((sm_addr = mmap(NULL, sizeof(struct bk_shmmap_header), PROT_READ, MAP_SHARED, shmfd, 0)) == MAP_FAILED)
  {
    fprintf(stderr,"Could not map shared memory %s: %s\n", pc->pc_shmname, strerror(errno));
    exit(2);
  }

  printf("%20s %p\n","Address",sm_addr->sh_addr);
  printf("%20s %p\n","User Address",sm_addr->sh_user);
  printf("%20s %lld\n","Segment size",(long long)sm_addr->sh_size);
  printf("%20s %lld\n","User size",(long long)sm_addr->sh_usersize);
  printf("%20s %lld (%lld seconds old) %s","Creatortime",(long long)sm_addr->sh_creatortime, (long long)(time(NULL)-sm_addr->sh_creatortime),ctime(&sm_addr->sh_creatortime));
  printf("%20s %u %s\n","Fresh interval",sm_addr->sh_fresh, sm_addr->sh_creatortime+sm_addr->sh_fresh > time(NULL)?"ISFRESH":"NOTFRESH");
  printf("%20s %u\n","Num Client Slots",sm_addr->sh_numclients);
  printf("%20s %u\n","Num Attaches",sm_addr->sh_numattach);
  printf("%20s %04x %s\n","State",sm_addr->sh_state,sm_addr->sh_state==BK_SHMMAP_READY?"READY":sm_addr->sh_state==BK_SHMMAP_CLOSE?"CLOSE":"UNINITIALIZED");

  if (sm_addr->sh_state==BK_SHMMAP_READY || sm_addr->sh_state==BK_SHMMAP_CLOSE)
  {
    struct bk_shmmap_header *tmp_addr = sm_addr->sh_addr;
    size = sm_addr->sh_size-sm_addr->sh_usersize;

    if (munmap(sm_addr, sizeof(struct bk_shmmap_header)) < 0)
    {
      fprintf(stderr, "Huh? Cannot unmap temporary memory segment: %s\n", strerror(errno));
      exit(3);
    }

    if ((sm_addr = mmap(tmp_addr, size, PROT_READ, MAP_SHARED, shmfd, 0)) == MAP_FAILED)
    {
      fprintf(stderr,"Could not second-map shared memory %s: %s\n", pc->pc_shmname, strerror(errno));
      exit(4);
    }

    int x;
    for(x=0;x<sm_addr->sh_numclients;x++)
    {
      char *state;

      switch (sm_addr->sh_client[x].su_state)
      {
      case BK_SHMMAP_USER_STATEEMPTY: state = "EMPTY"; break;
      case BK_SHMMAP_USER_STATEPREP: state = "PREP "; break;
      case BK_SHMMAP_USER_STATEINIT: state = "INIT "; break;
      case BK_SHMMAP_USER_STATEREADY: state = "READY"; break;
      case BK_SHMMAP_USER_STATECLOSE: state = "CLOSE"; break;
      default: state="OTHER";
      }

      printf("-%64.64s %5d %04x %5s %11lld %s",sm_addr->sh_client[x].su_name,
	     sm_addr->sh_client[x].su_pid,
	     sm_addr->sh_client[x].su_state,
	     state,
	     (long long)sm_addr->sh_client[x].su_clienttime,
	     ctime(&sm_addr->sh_client[x].su_clienttime));
    }
  }

  poptFreeContext(optCon);
  bk_exit(B, 0);
  return(255);					// Stupid INSIGHT stuff.
}
