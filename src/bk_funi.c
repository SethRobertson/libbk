/*
 * User-group id manipulation program plus chroot
 *
 * This MUST NOT EVER be installed SUID
 *
 * Seth wrote this program over 10 years ago, and it really needs a refresh to
 * use the current baka programming style, but who has the time?  Answer -
 * the poor saps who get to port this to more modern OS's.
 *
 * Seth Robertson
 * seth@baka.org
 *
 * ++Copyright BAKA++
 *
 * Copyright © 2004-2019 The Authors. All rights reserved.
 *
 * This source code is licensed to you under the terms of the file
 * LICENSE.TXT in this release for further details.
 *
 * Send e-mail to <projectbaka@baka.org> for further information.
 *
 * - -Copyright BAKA- -
*/

#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int getuidbyargv(char *argv, int *error, int *gid);
int getgidbyargv(char *argv, int *error);


int main(int argc, char **argv, char **envp)
{
  int uid;
  int gid;
  int err = 0;
  int resetXtraGroups = 1;
  int InitXtraGroups = 0;
  char *user_init = NULL;
  int errflg = 0;
  int c;
  char *dir = NULL;
  char *shell;

  if ((uid = getuidbyargv("nobody",&err,NULL)) == -1)
    if ((uid = getuidbyargv("noone",&err,NULL)) == -1)
      uid = 65534;

  if ((gid = getgidbyargv("nobody",&err)) == -1)
    if ((gid = getgidbyargv("noone",&err)) == -1)
      if ((gid = getgidbyargv("nogroup",&err)) == -1)
	gid = 65534;

  if (!(shell = getenv("SHELL")))
    shell = "/bin/sh";

  while ((c=getopt(argc,argv,"+Zc:G:g:u:U:")) != -1)
    switch(c)
      {
      case 'Z':
	resetXtraGroups = 0;
	break;

      case 'u':
	uid = getuidbyargv(optarg,NULL,NULL);
	break;

      case 'U':			/* fall through case */
	uid = getuidbyargv(optarg,NULL,&gid);

      case 'G':
	resetXtraGroups = 1;
	InitXtraGroups = 1;
	user_init = optarg;
	break;

      case 'g':
	gid = getgidbyargv(optarg,NULL);
	break;

      case 'c':
	dir = optarg;
	break;

      case '?':
	errflg++;
      }

  if (errflg)
    {
      fprintf(stderr,"Usage: %s [-u <uid>] [-Z] [-U <user>] [-G <user>] [-g <gid>] [-c <chroot>] [program] [argv] ...\n",argv[0]);
      exit(2);
    }

  /* Initialize the extended group list */
  if (resetXtraGroups && (initgroups(InitXtraGroups?user_init:"zzzError",gid) < 0))
    {
      perror("initgroups");
      exit(3);
    }

  if (dir && chroot(dir) < 0)
    {
      perror("chroot");
      exit(3);
    }
  if (dir && chdir("/") < 0)
    {
      perror("Cannot chdir to new root");
      exit(3);
    }

  /* reset user and group */
  if ((setgid(gid) < 0) || (setuid(uid) < 0))
    {
      perror("setid");
      exit(3);
    }

  /* exec the program in question */
  if (optind >= argc)
    execl(shell, shell, NULL);
  else
    execvp(argv[optind], argv+optind);

  perror("funi: exec");
  exit(-1);
}


/*
** Find out the gid if the argument is a number or a group name
*/
int getgidbyargv(char *argv, int *error)
{
  int gid;

  if (isdigit(*argv))
    {
      gid = atoi(argv);
    }
  else
    {
      struct group *EE;

      errno = 0;
      EE = getgrnam(argv);
      if (!EE)
	{
	  if (error)
	    {
	      *error = 1;
	      return(-1);
	    }
	  else
	    {
	      fprintf(stderr,"Failed getgrnam with %s: %s\n", argv, strerror(errno));
	      exit(2);
	    }
	}
      gid = EE->gr_gid;
    }

  return(gid);
}

/*
** Find of the uid if the argument is a number or a user name
*/
int getuidbyargv(char *argv, int *error, int *gid)
{
  int uid;

  if (isdigit(*argv))
    {
      uid = atoi(argv);
    }
  else
    {
      struct passwd *pwd;

      errno = 0;
      pwd = getpwnam(argv);

      if (!pwd)
	{
	  if (error)
	    {
	      *error = 1;
	      return(-1);
	    }
	  else
	    {
	      fprintf(stderr,"Failed getpwnam with %s: %s\n", argv, strerror(errno));
	      exit(2);
	    }
	}
      uid = pwd->pw_uid;
      if (gid) *gid = pwd->pw_gid;
    }

  return(uid);
}
