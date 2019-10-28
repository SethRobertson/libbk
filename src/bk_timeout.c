#if !defined(lint)
static const char libbk__copyright[] __attribute__((unused)) = "Copyright © 2009-2019";
static const char libbk__contact[] __attribute__((unused)) = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2009-2019 The Authors. All rights reserved.
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
 * Timeout program
 */
#include <libbk.h>
#include <libbk_i18n.h>


#define STD_LOCALEDIR_KEY     "LOCALEDIR"	///< Key in bkconfig to find the locale translation files
#define STD_LOCALEDIR_ENV     "BAKA_HOME"	///< Key in Environment to find base of locale directory
#define STD_LOCALEDIR_DEF     "/usr/local/baka"	///< Default base of where locale directory might be found
#define STD_LOCALEDIR_SUB     "locale"		///< Sub-component from install base where locale might be found
#define ERRORQUEUE_DEPTH      32		///< Default error queue depth
#define DEFAULT_TIMEOUT	      15		///< Default seconds to time out
#define DEFAULT_SIGNAL	      SIGTERM		///< Default signal to send
#define DEFAULT_EXITCODE      243		///< Default return to signal problems of any sort



/**
 * Information of international importance to everyone
 * which cannot be passed around.
 */
struct global_structure
{
  pid_t	gs_child;
  struct program_config *gs_pc;
} Global;



/**
 * Information about basic program runtime configuration
 * which must be passed around.
 */
struct program_config
{
  double		pc_timeout;		///< How long to wait for a timeout (seconds)
  u_int			pc_signal;		///< What signal to deliver when terminating
  u_int			pc_exitcode;		///< What code to return on exit
  bk_flags		pc_flags;		///< Flags are fun!
#define PC_VERBOSE	0x001			///< Verbose output
};


typedef struct mapstruct {
  const char *name;
  u_int num;
} mapstruct;


// From procps-3.2.7/proc/sig.c
/* BEGIN GPL CODE */
static const mapstruct sigtable[] = {
  {"ABRT",   SIGABRT},  /* IOT */
  {"ALRM",   SIGALRM},
  {"BUS",    SIGBUS},
  {"CHLD",   SIGCHLD},  /* CLD */
  {"CONT",   SIGCONT},
#ifdef SIGEMT
  {"EMT",    SIGEMT},
#endif
  {"FPE",    SIGFPE},
  {"HUP",    SIGHUP},
  {"ILL",    SIGILL},
  {"INT",    SIGINT},
  {"KILL",   SIGKILL},
  {"PIPE",   SIGPIPE},
  {"POLL",   SIGPOLL},  /* IO */
  {"PROF",   SIGPROF},
  {"PWR",    SIGPWR},
  {"QUIT",   SIGQUIT},
  {"SEGV",   SIGSEGV},
#ifdef SIGSTKFLT
  {"STKFLT", SIGSTKFLT},
#endif
  {"STOP",   SIGSTOP},
  {"SYS",    SIGSYS},   /* UNUSED */
  {"TERM",   SIGTERM},
  {"TRAP",   SIGTRAP},
  {"TSTP",   SIGTSTP},
  {"TTIN",   SIGTTIN},
  {"TTOU",   SIGTTOU},
  {"URG",    SIGURG},
  {"USR1",   SIGUSR1},
  {"USR2",   SIGUSR2},
  {"VTALRM", SIGVTALRM},
  {"WINCH",  SIGWINCH},
  {"XCPU",   SIGXCPU},
  {"XFSZ",   SIGXFSZ}
};
static const int number_of_signals = sizeof(sigtable)/sizeof(mapstruct);




static int compare_signal_names(const void *a, const void *b);
static int compare_signal_names(const void *a, const void *b){
  return strcasecmp( ((const mapstruct*)a)->name, ((const mapstruct*)b)->name );
}
static int signal_name_to_number(const char *name);
int signal_name_to_number(const char *name){
  long val;
  int offset;

  /* clean up name */
  if(!strncasecmp(name,"SIG",3)) name += 3;

  if(!strcasecmp(name,"CLD")) return SIGCHLD;
  if(!strcasecmp(name,"IO"))  return SIGPOLL;
  if(!strcasecmp(name,"IOT")) return SIGABRT;

  /* search the table */
  {
    const mapstruct ms = {name,0};
    const mapstruct *const ptr = bsearch(
      &ms,
      sigtable,
      number_of_signals,
      sizeof(mapstruct),
      compare_signal_names
    );
    if(ptr) return ptr->num;
  }

  if(!strcasecmp(name,"RTMIN")) return SIGRTMIN;
  if(!strcasecmp(name,"EXIT"))  return 0;
  if(!strcasecmp(name,"NULL"))  return 0;

  offset = 0;
  if(!strncasecmp(name,"RTMIN+",6)){
    name += 6;
    offset = SIGRTMIN;
  }

  /* not found, so try as a number */
  {
    char *endp;
    val = strtol(name,&endp,10);
    if(*endp || endp==name) return -1; /* not valid */
  }
  if(val+SIGRTMIN>127) return -1; /* not valid */
  return val+offset;
}
/* END GPL CODE */
static void stupid(int signum);
void stupid(int signum) { ; }


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
    {"verbose", 'v', POPT_ARG_NONE, NULL, 'v', N_("Turn on verbose message (specifically child timeout or child signal exit)"), NULL },
    {"no-seatbelts", 0, POPT_ARG_NONE, NULL, 0x1000, N_("Sealtbelts off & speed up"), NULL },
    {"seatbelts", 0, POPT_ARG_NONE, NULL, 0x1001, N_("Enable function tracing"), NULL },
    {"profiling", 0, POPT_ARG_STRING, NULL, 0x1002, N_("Enable and write profiling data"), N_("filename") },

    {"timeout", 't', POPT_ARG_STRING, NULL, 't', N_("Fractional seconds to wait for command to terminate (default 15)"), N_("Seconds") },
    {"signal", 's', POPT_ARG_STRING, NULL, 's', N_("Signal name or number to send (default TERM)"), N_("signal") },
    {"badexit", 'e', POPT_ARG_STRING, NULL, 'e', N_("Exit code to return on any bk_timeout error (default 243)"), N_("exit code") },
    POPT_AUTOHELP
    POPT_TABLEEND
  };
  timer_t timerid;
  struct sigevent ev;
  struct itimerspec its;
  struct sigaction act;
  int status = -1;

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD(NULL, "BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, LOG_LOCAL0, 0)))
  {
    fprintf(stderr,"%s: Could not perform basic initialization\n",argv[0]);
    exit(DEFAULT_EXITCODE);
  }
  bk_fun_reentry(B);

  // Enable error output
  bk_error_config(B, BK_GENERAL_ERROR(B), ERRORQUEUE_DEPTH, stderr, BK_ERR_CRIT,
		  BK_ERR_CRIT, BK_ERROR_CONFIG_FH |
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
  pc->pc_timeout = DEFAULT_TIMEOUT;
  pc->pc_signal = DEFAULT_SIGNAL;
  pc->pc_exitcode = DEFAULT_EXITCODE;

  if (!(optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, POPT_CONTEXT_POSIXMEHARDER)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not initialize options processing\n");
    bk_exit(B, DEFAULT_EXITCODE);
  }
  poptSetOtherOptionHelp(optCon, _("<command> [args]..."));

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

    case 't':					// timeout
      if (bk_string_atod(B, poptGetOptArg(optCon), &pc->pc_timeout, 0) < 0)
      {
	getopterr++;
	fprintf(stderr, "%s: Invalid timeout\n", BK_GENERAL_PROGRAM(B));
      }
      break;
    case 's':					// signal
      {
	const char *s = poptGetOptArg(optCon);
	if (bk_string_atou32(B, s, &pc->pc_signal, 0) < 0)
	{
	  int ret;
	  if ((ret = signal_name_to_number(s)) < 0)
	  {
	    getopterr++;
	    fprintf(stderr, "%s: Invalid signal name or number\n", BK_GENERAL_PROGRAM(B));
	  }
	  pc->pc_signal = ret;
	}
      }
      break;
    case 'e':					// exitcode
      if (bk_string_atou32(B, poptGetOptArg(optCon), &pc->pc_exitcode, 0) < 0)
      {
	getopterr++;
	fprintf(stderr, "%s: Invalid exit code\n", BK_GENERAL_PROGRAM(B));
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

  if (c < -1 || getopterr || argc < 1)
  {
    if (c < -1)
    {
      fprintf(stderr, "%s\n", poptStrerror(c));
    }
    poptPrintUsage(optCon, stderr, 0);
    bk_exit(B, 254);
  }

  if (pc->pc_timeout < .000000001)
  {
    fprintf(stderr, "%s: Invalid time to wait (must be at least 1ns)\n", BK_GENERAL_PROGRAM(B));
    bk_exit(B, pc->pc_exitcode);
  }

  Global.gs_pc = pc;
  Global.gs_child = -1;
  memset(&ev,0,sizeof(ev));
  ev.sigev_notify = SIGEV_SIGNAL;
  ev.sigev_signo = SIGALRM;
  if (timer_create(CLOCK_MONOTONIC, &ev, &timerid) < 0)
  {
    fprintf(stderr, "%s: Could not create timer: %s\n", BK_GENERAL_PROGRAM(B), strerror(errno));
    bk_exit(B, pc->pc_exitcode);
  }

  memset(&its,0,sizeof(its));
  its.it_value.tv_sec = pc->pc_timeout;
  pc->pc_timeout -= (int)pc->pc_timeout;
  its.it_value.tv_nsec = pc->pc_timeout * 1000000000;

  memset(&act, 0, sizeof(act));
  act.sa_handler = stupid;

  if (sigaction(SIGALRM, &act, NULL) < 0)
  {
    fprintf(stderr, "%s: Could not set up signal handler: %s\n", BK_GENERAL_PROGRAM(B), strerror(errno));
    bk_exit(B, pc->pc_exitcode);
  }

  if ((Global.gs_child = bk_fork_exec(B, bk_search_path(B,argv[0],NULL,X_OK,0), argv, NULL, 0)) < 0)
  {
    fprintf(stderr, "%s: Could not fork (or exec) child: %s\n", BK_GENERAL_PROGRAM(B), strerror(errno));
    bk_exit(B, pc->pc_exitcode);
  }

  if (timer_settime(timerid, 0, &its, NULL) < 0)
  {
    fprintf(stderr, "%s: Could not set timer: %s\n", BK_GENERAL_PROGRAM(B), strerror(errno));
    kill(Global.gs_child, pc->pc_signal);
    bk_exit(B, pc->pc_exitcode);
  }

  if (waitpid(Global.gs_child, &status, 0) < 0 && errno == EINTR)
  {
    // We presumably have timed out
    if (BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE))
      fprintf(stderr, "%s: timeout, killing child %d with signal %d\n", BK_GENERAL_PROGRAM(B), Global.gs_child, pc->pc_signal);
    kill(Global.gs_child, pc->pc_signal);
    bk_exit(B, pc->pc_exitcode);
  }

  if (WIFSIGNALED(status))
  {
    if (BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE))
      fprintf(stderr, "%s: child died with signal %d\n", BK_GENERAL_PROGRAM(B), WTERMSIG(status));
    bk_exit(B, pc->pc_exitcode);
  }

  // Normal termination
  bk_exit(B, WEXITSTATUS(status));

  poptFreeContext(optCon);
  return(255);					// Stupid INSIGHT stuff.
}
