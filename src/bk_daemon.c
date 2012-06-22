#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2003-2011";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2003-2011 The Authors. All rights reserved.
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
 * This daemonizes a program to disassociate the calling user with the
 * executing program in all ways possible.
 *
 * Seth wrote this program over 10 years ago, and it really needs a refresh to
 * use the current baka programming style, but who has the time?  Answer -
 * the poor saps who get to port this to more modern OS's.
 *
 * -a		Set O_APPEND to files opened with -s
 * -c		close all files > 2
 * -C		close all files
 * -d		chdir to / (get off possibly NFS mounted filesystems)
 * -D		Daemonize (-cdeu)
 * -e		bail on environmental variables (PATH=BSD default)
 * -N		Open fds 0, 1, and 2 on /dev/null if not already open
 * -ppriority	change the priority to priority (default 15)
 * -q		Quiet--don't print child PID on stderr
 * -s		File to open output TTYs onto
 * -u		bail on umask -> umask 0
 *
 * NOTE: default is to redirect stdio files to /dev/null if they are attached
 * to a tty (and you can't stop that.  This is specifically designed to prevent
 * a program from screwing up a pty.  And it bloody well better do it!)
 */

/*
 * <TODO>This needs to be autoconf'd and should use the daemon() function
 * where available.</TODO>
 */

#include <libbk.h>
#include <sys/ioctl.h>


#ifdef hpux			/* From carson@cs.columbia.edu */
#define TIOCNOTTY	_IO('t', 113) /* void tty association */
#define getdtablesize() NOFILE
#endif


extern char *optarg;
extern int optind;

static void reopen(char *tty);
static int child(int argc, char **argv, int optint);


static int cfiles = 0;			/* Close all files > 2 */
static int Cfiles = 0;			/* Close all files */
static int cdir = 0;			/* Chdir to / */
static int env = 0;			/* Bail on environmental variables */
static int b_umask = 0;		/* Bail on umask */
static char *newenviron[] = { "PATH=/bin:/usr/bin:/usr/sbin:/sbin:.", NULL };
static char *outstream = NULL;
static int append = 0;
static int quiet = 0;
static int want_dev_null_stdio = 0;



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
  int pid = 0;
  int stupid;
  char *strptr;
  int priority = 15;		/* default priority to set */
  int error = 0;		/* Error on getopt? */
  int c;			/* getopt option */


  /* Lets process arguments! */
  while ((c=getopt(argc, argv, "+NcCdDeup:s:aq")) != EOF)
    switch (c)
    {
    case 'a':
      append = 1;
      break;
    case 'c':
      cfiles = 1;
      break;
    case 'C':
      // NB: This option overrides 's' (see below).
      Cfiles = 1;
      break;
    case 'd':
      cdir = 1;
      break;
    case 'D':
      cfiles = cdir = env = b_umask = 1;
      break;
    case 'e':
      env = 1;
      break;
    case 'N':
      want_dev_null_stdio = 1;
      break;
    case 'p':
      strptr=(char *)NULL;
      priority = strtol(optarg,&strptr,0);

      if ((priority==0 && *strptr != '\0') ||
	  (priority > 19) ||
	  (priority < 0 && getuid() != 0) ||
	  (priority < -20))
      {
	/* Don''t tell them about -20-0 since only root can use it */
	fprintf(stderr,"Bad Priority! (0-19)\n");
	error = 1;
      }
      break;
    case 'q':
      quiet=1;
    case 's':
      outstream = optarg;
      break;
    case 'u':
      b_umask = 1;
      break;

    case '?':
      error = 1;
      break;
    default:
      fprintf(stderr,"Help!!!\n");
      exit(1);
    }

  /* check for errors or a program to run */
  if (error || argc <= optind)
  {
    (void) fprintf (stderr, "usage: %s [-acCdDeNqsu] [-p priority] <cmd> [args]\n\n\
-a	Set O_APPEND to files opened with -s\n\
-c	close all files > 2\n\
-C	close all files\n\
-d	chdir to /\n\
-D	-cdeu\n\
-e	Bail on environmental variables (PATH=BSD default)\n\
-N	Open fds 0, 1, and 2 on /dev/null if not already open\n\
-p pri	Change the priority (default 15)\n\
-q	Quiet=don't print child PID on stderr\n\
-s	File to open output TTYs onto instead of /dev/null\n\
-u	Set umask to umask 0\n", argv[0]);
    exit (2);
  }

  stupid = nice(priority);	/* Change priority to priority, if possible. Ignore errors here. */

  if (stupid && pid)
  {
    perror("Can never happen.  Stupid compiler forcing me to check return codes I do not care about");
  }

  // <TODO>daemon() function performs fork, add it here (not in child())</TODO>

#ifdef TEST
  switch (pid = 0)
#else
    switch (pid = fork ())
#endif
    {

    case -1:			/* Failed */
      perror (argv[0]);
      exit (1);			/* So what can I do? */
      break;

    case 0:			/* Child */
      child(argc, argv, optind);
      break;

    default:			/* Parent */
      if (!quiet)
	(void) fprintf (stderr, "[%d]\n", pid);
      exit (0);
      break;
    }
  exit(2);
}



/*
 * child
 *
 * Procedure to take the child's process and rip it apart until it's
 * detached from most everything.
 */
static int child(int argc, char **argv, int optint)
{
  register int fd;
  char *tty = NULL;
  int tablesize;
#ifdef RLIMIT_NOFILE
  struct rlimit lim;
  char *new_outstream;
  int flags;

  getrlimit(RLIMIT_NOFILE,&lim);
  tablesize = lim.rlim_cur;
#else
  tablesize = getdtablesize();
#endif /* RLIMIT_NOFILE */

  errno = 0;

  if (isatty(2))
    tty = ttyname(2);		/* Find out stderr's tty if known */

  /* no terminal-related job control signals */
#ifdef SIGTTOU
  if (signal (SIGTTOU, SIG_IGN) == SIG_ERR)
  {
    perror("signal");
    fprintf(stderr,"%s: Error: signal: SIGTTOU\n", argv[0]);
    exit(2);
  }
#endif
#ifdef SIGTTIN
  if (signal (SIGTTIN, SIG_IGN) == SIG_ERR)
  {
    perror("signal");
    fprintf(stderr,"%s: Error: signal: SIGTTIN\n", argv[0]);
    exit(2);
  }
#endif
#ifdef SIGTSTP
  if (signal (SIGTSTP, SIG_IGN) == SIG_ERR)
  {
    perror("signal");
    fprintf(stderr,"%s: Error: signal: SIGTSTP\n", argv[0]);
    exit(2);
  }
#endif

  /* Ignore Hangup and Quit */
  if (signal (SIGHUP, SIG_IGN) == SIG_ERR)
  {
    perror("signal");
    fprintf(stderr,"%s: Error: signal: SIGHUP\n", argv[0]);
    exit(2);
  }

  if (signal (SIGQUIT, SIG_IGN) == SIG_ERR)
  {
    perror("signal");
    fprintf(stderr,"%s: Error: signal: SIGQUIT\n", argv[0]);
    exit(2);
  }

#if defined(HAVE_SETPGID) && !defined(HAVE_SETSID)
  if (setpgid (0, (pid = getpid ())) == -1)
  {
    perror("setpgid");
    fprintf(stderr,"%s: Error: setpgrp: %d\n", argv[0], pid);
    exit(2);
  }
#else
  // should always succeed in child process
  setsid();
#endif

#ifdef TIOCNOTTY
  /* bail on a controlling tty */
  if ((fd = open ("/dev/tty", O_RDWR)) >= 0)
  {
    if (ioctl (fd, TIOCNOTTY, (char *) 0) == -1)
    {
      perror("ioctl");
      fprintf(stderr,"%s: Error: ioctl: TIOCNOTTY\n", argv[0]);
      exit(2);
    }
    if (close (fd) == -1)
    {
      perror("close");
      fprintf(stderr,"%s: Error: close: %d\n", argv[0], fd);
      exit(2);
    }
  }
#endif /* TIOCNOTTY */

  if (cfiles)
  {
    /* close all files > 2 */
    for (fd = 3; fd < tablesize; fd++)
      (void) close (fd);
  }

  if (Cfiles)
  {
    /* close all files */
    for (fd = 0; fd < tablesize; fd++)
      (void) close (fd);
  }

  if (cdir)			/* move off a possibly mounted file system */
    if (chdir ("/") == -1)
    {
      reopen(tty);
      perror("chdir");
      fprintf(stderr,"%s: Error: chdir: /\n", argv[0]);
      exit(2);
    }


  if (b_umask)			/* no inherited umask */
    umask(0);

  /*
   * *Explictly* Open stdin, stdout, and stderr on /dev/null
   */
  if (want_dev_null_stdio)
  {
    int dev_null_fd;
    int fdin, fdout, fderr;
    long fd_flags;

    fdin = fileno(stdin);

    if ((fcntl(fdin, F_GETFL, &fd_flags) < 0) && (errno == EBADF))
    {
      if ((dev_null_fd = open(_PATH_DEVNULL, O_RDONLY)) < 0)
      {
	fprintf(stderr, "Could not open %s: %s\n", _PATH_DEVNULL, strerror(errno));
	exit(2);
      }


      if (dup2(dev_null_fd, fdin) < 0)
      {
	perror("dup2 of stdin");
	exit(2);
      }

      if (dev_null_fd != fdin)
	close(dev_null_fd);
    }

    if ((dev_null_fd = open(_PATH_DEVNULL, O_WRONLY)) < 0)
    {
      fprintf(stderr, "Could not open %s: %s\n", _PATH_DEVNULL, strerror(errno));
      exit(2);
    }

    fdout = fileno(stdout);

    if ((fcntl(fdout, F_GETFL, &fd_flags) < 0) && (errno == EBADF))
    {
      if (dup2(dev_null_fd, fdout) < 0)
      {
	perror("dup2 of stdout");
	exit(2);
      }
    }

    fderr = fileno(stderr);

    if ((fcntl(fderr, F_GETFL, &fd_flags) < 0) && (errno == EBADF))
    {
      if (dup2(dev_null_fd, fderr) < 0)
      {
	perror("dup2 of stderr");
	exit(2);
      }
    }

    if (dev_null_fd != fdout && dev_null_fd != fderr)
      close(dev_null_fd);
  }


  for (fd = 0; fd < tablesize; fd++)
  {
    if (isatty(fd))
    {
      int newfd;
      (void)close(fd);	/* Close the file (stdin)*/

      if ((fd == fileno(stdout)) || (fd == fileno(stderr)))
      {
	flags = O_WRONLY | O_CREAT;

	// Switch output to /dev/null unless user asked otherwise.
	if (outstream)
	  new_outstream = outstream;
	else
	  new_outstream = _PATH_DEVNULL;

	if (append)
	  flags |= O_APPEND;
      }
      else
      {
	// Switch input to /dev/null;
	new_outstream = _PATH_DEVNULL;
	flags = O_RDONLY;
      }

      if((newfd = open (new_outstream, flags, 0666)) == -1)
      {
	reopen(tty);
	perror("open");
	fprintf(stderr,"%s: Error: open: /dev/null\n", argv[0]);
	exit(2);
      }


      if (newfd != fd)
      {
	if (dup2(newfd,fd) == -1)
	{
	  reopen(tty);
	  perror("umask");
	  fprintf(stderr,"%s: Error: umask: 0\n", argv[0]);
	  exit(2);
	}
	close(newfd);
      }
    }
  }

  if (env)
    environ = newenviron;

  execvp (argv[optind], &argv[optind]);


  reopen(tty);
  perror("execvp");
  fprintf(stderr,"%s: Error: execvp: %s\n",argv[0],argv[optind]);
  exit (1);
}


static void reopen(char *tty)
{
  int fd;

  if (tty)
  {
    if ((fd = open(tty,O_WRONLY)) < 0)
      exit(5);
    if (fd != 2)
      dup2(fd,2);
    return;
  }
}
