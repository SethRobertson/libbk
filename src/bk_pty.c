#if !defined(lint) && !defined(__INSIGHT__)
#include "libbk_compiler.h"
UNUSED static const char libbk__rcsid[] = "$Id: bk_pty.c,v 1.4 2005/09/02 17:13:55 dupuy Exp $";
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
 * Run a program under a pty. 
 * Example libbk user with main() prototype.
 */
#include <libbk.h>
#include <libbk_i18n.h>


// Declaration of openpty and forkpty
#ifdef HAVE_OPENPTY
#ifdef HAVE_PTY_H
#include <pty.h>
#endif /* HAVE_PTY_H */
#ifdef HAVE_UTIL_H
#include <util.h>
#endif /* HAVE_UTIL_H */
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif /* HAVE_LIBUTIL_H */
#endif /* HAVE_OPENPTY */


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
  struct program_config *gs_pc;
} Global;



/**
 * Information about basic program runtime configuration
 * which must be passed around.
 */
struct program_config
{
  bk_flags			pc_flags;	///< Flags are fun!
#define PC_VERBOSE	0x001			///< Verbose output
  char **			pc_cmd;		///< Command to execute.
  pid_t				pc_pid;		///< Child pid.
};


static int proginit(bk_s B, struct program_config *pc);
static void progrun(bk_s B, struct program_config *pc);
static void reaper(int sig);



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
  BK_ENTRY_MAIN(B, __FUNCTION__, __FILE__, "bk_pty");
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
  Global.gs_pc = pc;
  pc->pc_pid = -1;  
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
  pc->pc_cmd = argv = (char **)poptGetArgs(optCon);
  argc = 0;
  
  if (argv)
    for (; argv[argc]; argc++)
      ; // Intentially left blank

  if (c < -1 || getopterr)
  {
    if (c < -1)
    {
      fprintf(stderr, "%s\n", poptStrerror(c));
    }
    poptPrintUsage(optCon, stderr, 0);
    bk_exit(B, 254);
  }

  if (BK_STREQ(pc->pc_cmd[0], ""))
  {
    fprintf(stderr, "%s: no cmd to execute\n", argv[0]);
    exit(1);
  }

  if (proginit(B, pc) < 0)
  {
    bk_die(B, 254, stderr, _("Could not perform program initialization\n"), BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  }

  progrun(B, pc);

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
  BK_ENTRY(B, __FUNCTION__,__FILE__,"bk_pty");

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_RETURN(B, -1);
  }

  // XXX - customize here

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
static void progrun(bk_s B, struct program_config *pc)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"bk_pty");
  int fd;
  struct bk_run *run = NULL;
  struct bk_ioh *ioh_child;
  struct bk_ioh *ioh_stdio;

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_VRETURN(B);
  }

  if (!(run = bk_run_init(B, 0)))
  {
    fprintf(stderr, "Could not create run\n");
    goto error;
  }

  if (!(ioh_stdio = bk_ioh_init(B, fileno(stdin), fileno(stdout), NULL, NULL, 0, 0, 0, run, BK_IOH_NO_HANDLER | BK_IOH_RAW | BK_IOH_STREAM)))
  {
    fprintf(stderr, "Could not create io handler around child process\n");
    goto error;
  }

  if (bk_signal(B, SIGCHLD, reaper, 0) < 0)
  {
    fprintf(stderr, "Could not declare child process reaper\n");
    goto error;
  }

#ifdef HAVE_OPENPTY
  switch (pc->pc_pid = forkpty(&fd, NULL, NULL, NULL))
  {
  case -1:
    fprintf(stderr, "Could not fork new process under a pty: %s\n", strerror(errno));
    goto error;
    break;
    
  case 0: // Child
    /*
     * Unfortunately it is possible (as in it has been seen) that the child
     * process can complete and signal the parent before forkpty() has a
     * chance to complete and assign the child pid to pc->pc_pid. This
     * leaves the reaper function executing waitpid() on a bogus value. So
     * we wait for second to make sure that this assignment has occured.
     */
    sleep(1); 
    if (bk_exec(B, pc->pc_cmd[0], pc->pc_cmd, environ, 0) < 0)
    {
      fprintf(stderr, "Could not fork: %s\n", pc->pc_cmd[0]);
      bk_exit(B, 1); // DO NOT GOTO ERROR HERE.
    }
    // NOTREACHED
    break;

  default:
    if (!(ioh_child = bk_ioh_init(B, fd, fd, NULL, NULL, 0, 0, 0, run, BK_IOH_NO_HANDLER | BK_IOH_RAW | BK_IOH_STREAM)))
    {
      fprintf(stderr, "Could not create io handler around child process\n");
      goto error;
    }

    if (bk_relay_ioh(B, ioh_child, ioh_stdio, NULL, NULL, NULL, NULL, BK_RELAY_IOH_DONE_AFTER_ONE_CLOSE))
    {
      fprintf(stderr, "Could not configure relay");
      goto error;
    }

    if (bk_run_run(B, run, 0) < 0)
    {
      fprintf(stderr, "Program failed\n");
      goto error;
    }

    break;
  }
#else  /* !HAVE_OPENPTY */
  fprintf(stderr, "no forkpty on this system; use socketpair or two pipes\n");
  bk_exit(B, 254); // DO NOT GOTO ERROR HERE.
#endif

  bk_run_destroy(B, run);
  BK_VRETURN(B);

 error:
  if (run)
    bk_run_destroy(B, run);
  bk_exit(B, 1);
  BK_VRETURN(B);
}



static void 
reaper(int sig)
{
  struct program_config *pc = Global.gs_pc;

  if (waitpid(pc->pc_pid, NULL, WNOHANG) < 0)
    fprintf(stderr, "waitpid failed: %s\n", strerror(errno));

  return;
}
