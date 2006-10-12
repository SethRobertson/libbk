/*
 * Dedicated to the public (domain).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef SIGCHLD
#define SIGCHLD SIGCLD
#endif

#define DEFAULT_TIMEOUT 10
#define PROGNAME "timeout: "

int sig = SIGTERM;
unsigned timeout = 0;
int waitaftersignal = 0;
volatile int finished = 0;
int pid = 0;

/*
 * If a fatal error occurs, this program exits with an unusual exit status so
 * that it's more likely that user scripts will be able to determine if the
 * exit status derived from the subprocess or this program.  The default value
 * used is one that would be returned by the shell for a process terminated by
 * SIGCHLD (impossible, since the default action for that signal is SIG_IGN).
 *
 * As the value of SIGCHLD may be different on different platforms, if a
 * portable error exit code is needed, it can be specified with the -e option.	
 *
 * 127 may be a useful choice in some cases, as the shell uses it when unable to
 * fork or exec a program.
 */
#define ERR_EXIT (128 + SIGCHLD)
int error_exit = ERR_EXIT;
volatile int status = W_EXITCODE(ERR_EXIT,0);


void reaper(int signum);
void status_exit(int childstatus);
void usage(void);
int sigcompare(const void *a, const void *b);
int sigparse(const char *sigspec);



/*
 * Time-limit a command that may take a long time (or even forever).
 */
int main(int argc, char **argv)
{
  char opt;
  unsigned long arg;
  char *argend;

  while ((opt = getopt(argc, argv, "+s:t:e:w")) != EOF)
  {
    switch(opt)
    {
    case 'e':
      arg = strtoul(optarg, &argend, 0);
      if (arg > 0 && arg < 256 && !*argend)
	error_exit = arg;
      else
	usage();
      break;
    case 's':
      if ((sig = sigparse(optarg)) <= 0)
	usage();
      break;
    case 't':
      arg = strtoul(optarg, &argend, 0);
      if (arg > 0 && arg <= UINT_MAX && !*argend)
	timeout = (unsigned) arg;
      else
	usage();
      break;
    case 'w':
      waitaftersignal++;
      break;
    default:
      usage();
    }
  }
  argc -= optind;
  argv += optind;

  if (!argc)
    usage();

  /* compatibility with netatalk timeout(1): timeout secs command args */
  if (!timeout && argc > 1)
  {
    arg = strtoul(*argv, &argend, 0);
    if (arg > 0 && arg <= UINT_MAX && !*argend)
    {
      timeout = (unsigned) arg;
      argv++;
    }
  }

  if (!timeout)
    timeout = DEFAULT_TIMEOUT;

  if (signal(SIGCHLD, reaper) == SIG_ERR)
  {
    perror(PROGNAME "signal");
    exit(error_exit);
  }

  if (!(pid=fork()))
  {
    if (execvp(*argv, argv) < 0)
    {
      if (**argv)
	perror(*argv);
      else
	perror(PROGNAME "exec");
    }

    /* shouldn't be reached if execvp succeeds, but just in case */
    exit(error_exit);
  }

  if (pid < 0)
  {
    perror(PROGNAME "fork");
    exit(error_exit);
  }

  if (!finished)
    sleep(timeout);

  if (!finished)
  {
    if (kill(pid, sig) < 0 && errno != ESRCH)
      perror(PROGNAME "kill");
  }

  /* always check for child status (possibly waiting), to handle signal races */
  if (!finished && waitpid(pid, &status, (waitaftersignal ? 0 : WNOHANG)))
  {
    finished++;
  }

  if (finished)
  {
    status_exit(status);
  }

  /* child not reaped; we don't want to wait, so fake a signal termination */
  signal(sig, SIG_DFL);
  raise(sig);
  
  /* if signal is not fatal, exit with shell-style exit code */
  exit(128 + sig);
}



/*
 * Child process reaper, in case there are other siblings somehow
 */
void reaper(int signum)
{
  int child;

  while ((child = waitpid(-1, &status, WNOHANG)) >= 0)
  {
    /* if exec fails, child may exit before parent returns from fork() */
    if (child == pid || (pid == 0 && WEXITSTATUS(status) == error_exit))
    {
      /* set flag just in case we can't or don't exit properly */
      finished++;
      status_exit(status);
    }
  }
}



/*
 * Reflect child exit status, with or without signal
 */
void status_exit(int code)
{
  if (WIFEXITED(code))
    exit(WEXITSTATUS(code));

  if (WIFSIGNALED(code))
  {
    signal(WTERMSIG(code), SIG_DFL);
    raise(WTERMSIG(code));
    /* this should not be reached, but just in case */
    exit(128 + WTERMSIG(code));
  }

  /* this should not be reached, but perhaps child is traced somehow? */
}


struct slist {
  char *name;
  int number;
} sigs[] = {
#ifdef SIGHUP
  { "HUP",	SIGHUP },
#endif
#ifdef SIGINT
  { "INT",	SIGINT },
#endif
#ifdef SIGQUIT
  { "QUIT",	SIGQUIT },
#endif
#ifdef SIGILL
  { "ILL",	SIGILL },
#endif
#ifdef SIGTRAP
  { "TRAP",	SIGTRAP },
#endif
#ifdef SIGABRT
  { "ABRT",	SIGABRT },
# ifdef SIGIOT
#  if SIGIOT != SIGABRT
  { "IOT",	SIGIOT },
#  endif
# endif
#else
# ifdef SIGIOT
  { "IOT",	SIGIOT },
# endif
#endif
#ifdef SIGBUS
  { "BUS",	SIGBUS },
#endif
#ifdef SIGEMT
  { "EMT",	SIGEMT },
#endif
#ifdef SIGFPE
  { "FPE",	SIGFPE },
#endif
#ifdef SIGKILL
  { "KILL",	SIGKILL },
#endif
#ifdef SIGUSR1
  { "USR1",	SIGUSR1 },
#endif
#ifdef SIGSEGV
  { "SEGV",	SIGSEGV },
#endif
#ifdef SIGUSR2
  { "USR2",	SIGUSR2 },
#endif
#ifdef SIGPIPE
  { "PIPE",	SIGPIPE },
#endif
#ifdef SIGALRM
  { "ALRM",	SIGALRM },
#endif
#ifdef SIGTERM
  { "TERM",	SIGTERM },
#endif
#ifdef SIGSTKFLT
  { "STKFLT",	SIGSTKFLT },
#endif
#ifdef SIGCHLD
  { "CHLD",	SIGCHLD },
#endif
#ifdef SIGCONT
  { "CONT",	SIGCONT },
#endif
#ifdef SIGSTOP
  { "STOP",	SIGSTOP },
#endif
#ifdef SIGTSTP
  { "TSTP",	SIGTSTP },
#endif
#ifdef SIGTTIN
  { "TTIN",	SIGTTIN },
#endif
#ifdef SIGTTOU
  { "TTOU",	SIGTTOU },
#endif
#ifdef SIGURG
  { "URG",	SIGURG },
#endif
#ifdef SIGXCPU
  { "XCPU",	SIGXCPU },
#endif
#ifdef SIGXFSZ
  { "XFSZ",	SIGXFSZ },
#endif
#ifdef SIGVTALRM
  { "VTALRM",	SIGVTALRM },
#endif
#ifdef SIGPROF
  { "PROF",	SIGPROF },
#endif
#ifdef SIGWINCH
  { "WINCH",	SIGWINCH },
#endif
#ifdef SIGINFO
  { "INFO",	SIGINFO },
#endif
#ifdef SIGIO
  { "IO",	SIGIO },
#endif
#ifdef SIGPWR
  { "PWR",	SIGPWR },
#endif
#ifdef SIGSYS
  { "SYS",	SIGSYS },
#endif
  { 0,		0 }
};



/*
 * Explain how to use this program.
 */
void usage(void)
{
  int i;

  fprintf(stderr, "timeout [-w] [-e error-exit ] [-s signal]"
	  " [[-t] seconds] program [args]\n");
  fprintf (stderr, "  (use a signal number, or one of these signal names):\n");

  qsort(sigs, sizeof(sigs)/sizeof(sigs[0]), sizeof(sigs[0]), sigcompare);

#define COLS 8

  for (i = 0; sigs[i].name; i++) {
    if (i % COLS == 0)
      fprintf (stderr, "\n\t");

    fprintf (stderr, "%s", sigs[i].name);

    if ((i + 1) % COLS != 0)
      fprintf (stderr, "\t");
  }

  fprintf (stderr, "\n");

#ifdef SIGRTMIN
  fprintf (stderr, "\tRTMIN\tRTMIN+1\tRTMIN+2\t...\tRTMAX-2\tRTMAX-1\tRTMAX\n");
#endif

  fprintf (stderr, "\n");

  exit(error_exit);
}



/*
 * Qsort helper function to sort signal names.
 */
int sigcompare(const void *a, const void *b)
{
  const struct slist *first = a;
  const struct slist *second = b;
  
  /* sort all zero (uninitialized) entries to end */
  if (!second->number)
    return -1;
  if (!first->number)
    return 1;
  
  /* numeric sort */
  return first->number - second->number;

#if 0
  /* alphabetic sort by name */
  return strcmp(first->name, second->name);
#endif
}



/*
 * Convert signal name to number.
 */
int sigparse(const char *sigspec)
{
  unsigned long arg;
  char *argend;
  int i;

  arg = strtoul(sigspec, &argend, 0);
  if (arg > 0 && arg < _NSIG && !*argend)
    return arg;

  /* skip over leading SIG in case user added it from force of habit */
  if (!strncmp(sigspec, "SIG", 3))
    sigspec += 3;

  for (i = 0; sigs[i].name; i++)
  {
    if (!strcmp (sigs[i].name, sigspec))
      return sigs[i].number;
  }

#ifdef SIGRTMIN
  if (!strcmp(sigspec, "RTMIN"))
    return SIGRTMIN;

#define SIX sizeof("RTMIN")

# ifdef SIGRTMAX
  if (!strcmp(sigspec, "RTMAX"))
    return SIGRTMAX;

  if (!strncmp(sigspec, "RTMIN+", SIX))
  {
    arg = strtoul(sigspec + SIX, &argend, 0);
    if (arg > 0 && arg <= (unsigned)(SIGRTMAX - SIGRTMIN) && !*argend)
      return SIGRTMIN + arg;
  }
  else if (!strncmp(sigspec, "RTMAX-", SIX))
  {
    arg = strtoul(sigspec + SIX, &argend, 0);
    if (arg > 0 && arg <= (unsigned)(SIGRTMAX - SIGRTMIN) && !*argend)
      return SIGRTMAX - arg;
  }
# endif
#endif

  return 0;
}
