#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: bk_daemon.c,v 1.1 2003/06/27 23:12:24 seth Exp $";
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
 * This daemonizes a program to disassociate the calling user
 * with the executing program in all ways possible.
 *
 * I wrote this program over 10 years ago, and it really needs
 * a refresh to be in my current programming style, but who has the
 * time?
 *
 * -c		close all files > 2
 * -C		close all files
 * -D		Daemonize (-cdeu)
 * -d		chdir to / (get off possibly NFS mounted filesystems)
 * -e		bail on environmental variables (PATH=BSD default)
 * -u		bail on umask -> umask 0
 * -ppriority	change the priority to priority (default 15)
 *
 * NOTE: default is to redirect stdio files to /dev/null if they are attached
 * to a tty (and you can't stop that.  This is specifically designed to
 * eliminate of a program screwing up a pty.  And it bloody well better do it!)
 */

#include <libbk.h>
#include        <sys/ioctl.h>


#ifdef hpux			/* From carson@cs.columbia.edu */
#define TIOCNOTTY	_IO('t', 113) /* void tty association */
#define getdtablesize() NOFILE
#endif


extern char *optarg;
extern int optind;
extern char **environ;

static void reopen(char *tty);
static int child(int argc, char **argv, int optint);


int cfiles = 0;			/* Close all files > 2 */
int Cfiles = 0;			/* Close all files */
int cdir = 0;			/* Chdir to / */
int env = 0;			/* Bail on environmental variables */
int b_umask = 0;		/* Bail on umask */
char *newenviron[] = { "PATH=/bin:/usr/bin:/usr/sbin:/sbin:.", NULL };



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
  int pid;
  char *strptr;
  int priority = 15;		/* default priority to set */
  int error = 0;		/* Error on getopt? */
  int c;			/* getopt option */

  /* Lets process arguments! */
  while ((c=getopt(argc, argv, "+cCdDeup:")) != EOF)
    switch (c)
    {
    case 'c':
      cfiles = 1;
      break;
    case 'C':
      Cfiles = 1;
      break;
    case 'd':
      cdir = 1;
      break;
    case 'e':
      env = 1;
      break;
    case 'u':
      b_umask = 1;
      break;
    case 'D':
      cfiles = cdir = env = b_umask = 1;
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
    (void) fprintf (stderr, "usage: %s [-cCdDeEu] [-p priority] <cmd> [args]\n", argv[0]);
    exit (2);
  }

  nice(priority);		/* Change priority to priority, if possible */

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
#if defined(__svr4__)
  struct rlimit lim;

  getrlimit(RLIMIT_NOFILE,&lim);
  tablesize = lim.rlim_cur;
#else
  tablesize = getdtablesize();
#endif /* SVR4 */

  errno = 0;

  if (isatty(2))
    tty = ttyname(2);		/* Find out stderr's tty if known */

  /* no terminal-related job control signals */
#ifdef SIGTTOU
  if ((int) signal (SIGTTOU, SIG_IGN) == -1)
  {
    perror("signal");
    fprintf(stderr,"%s: Error: signal: SIGTTOU\n", argv[0]);
    exit(2);
  }
#endif
#ifdef SIGTTIN
  if ((int) signal (SIGTTIN, SIG_IGN) == -1)
  {
    perror("signal");
    fprintf(stderr,"%s: Error: signal: SIGTTIN\n", argv[0]);
    exit(2);
  }
#endif
#ifdef SIGTSTP
  if ((int) signal (SIGTSTP, SIG_IGN) == -1)
  {
    perror("signal");
    fprintf(stderr,"%s: Error: signal: SIGTSTP\n", argv[0]);
    exit(2);
  }
#endif

  /* Ignore Hangup and Quit */
  if ((int) signal (SIGHUP, SIG_IGN) == -1)
  {
    perror("signal");
    fprintf(stderr,"%s: Error: signal: SIGHUP\n", argv[0]);
    exit(2);
  }

  if ((int) signal (SIGQUIT, SIG_IGN) == -1)
  {
    perror("signal");
    fprintf(stderr,"%s: Error: signal: SIGQUIT\n", argv[0]);
    exit(2);
  }

  /* detach from process group */
#if defined(__sun__)
  if (setpgrp (0, (pid = getpid ())) == -1)
  {
    perror("setpgrp");
    fprintf(stderr,"%s: Error: setpgrp: %d\n", argv[0], pid);
    exit(2);
  }
#else
  setpgrp();
#endif

#if defined(__svr4__)
  setsid();			/* Not quite the same things as TIOCNOTTY (sigh) */
#else
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
#endif /*SVR4*/

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


  for (fd = 0; fd < tablesize; fd++)
  {
    if (isatty(fd))
    {
      int newfd;
      (void)close(fd);	/* Close the file (stdin)*/

      /* Open /dev/null for input */
      if((newfd = open ("/dev/null", O_RDWR)) == -1)
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
