#if !defined(lint) && !defined(__INSIGHT__)
#include "libbk_compiler.h"
UNUSED static const char libbk__rcsid[] = "$Id: b_exec.c,v 1.21 2004/07/08 04:40:16 lindauer Exp $";
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
 * All of the baka run public and private functions.
 */

#include <libbk.h>
#include "libbk_internal.h"

// Declaration of openpty
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


#ifdef BK_USING_PTHREADS
#ifdef MISSING_PTHREAD_RWLOCK_INIT
static pthread_rwlock_t bkenvlock;	///< Lock on enviornment access
static short bkenvlock_initialized = 0;
#else
static pthread_rwlock_t bkenvlock = PTHREAD_RWLOCK_INITIALIZER;	///< Lock on enviornment access
#endif /* MISSING_PTHREAD_RWLOCK_INIT */
#endif /* BK_USING_PTHREADS */




/**
 * Create a pipe to a subprocess and duplicate various descriptors. If the
 * either of the copy in variables are NonNULL and set to -1, then the
 * approrpriate descriptor (as determined fileno()) will be assumed. If
 * either is both nonNULL and not -1, then <em>that</em> descriptor value
 * is used.
 *
 * <WARNING>Avoid use of this function for best windows compatibility</WARNING>
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param fdinp Copy in/out (though mostly out) input descriptor. Creates child --> parent pipe.
 *	@param fdoutp Copy in/out (though mostly out) output descriptor. Creates parent --> child pipe.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success in the child.
 *	@return <i>pid</i> on success in the parent.
 */
pid_t
bk_pipe_to_process(bk_s B, int *fdinp, int *fdoutp, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int p2c[2] = { -1, -1};
  int c2p[2] = { -1, -1};
  int fdin;
  int fdout;
  int fderr;
  pid_t pid = 0;

  if (BK_FLAG_ISSET(flags, BK_PIPE_FLAG_USE_PTY))
#ifdef HAVE_OPENPTY
  {
    if (openpty(&p2c[1], &p2c[0], NULL, NULL, NULL) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not get pseudo-tty: %s\n",
		      strerror(errno));
      goto error;
    }
    c2p[0] = dup(p2c[1]);
    c2p[1] = dup(p2c[0]);
  }
  else
#else  /* !HAVE_OPENPTY */
  {
    bk_error_printf(B, BK_ERR_ERR, "No openpty support; using a pipe\n");
  }
  // <TRICKY>no "else" if no openpty support; use a pipe - don't fail</TRICKY>
#endif /* !HAVE_OPENPTY */
  {
    if (fdinp)
    {
      // We want child --> parent pipe.
      if (pipe(c2p) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not create child --> parent pipe: %s\n", strerror(errno));
	goto error;
      }

      bk_debug_printf_and(B,2,"Creating c2p pipe: [%d, %d]\n", c2p[0], c2p[1]);
    }

    if (fdoutp)
    {
      // We want parent --> child pipe.
      if (pipe(p2c) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not create parent --> child pipe: %s\n", strerror(errno));
	goto error;
      }
      bk_debug_printf_and(B,2,"Creating p2c pipe: [%d, %d]\n", p2c[0], p2c[1]);
    }
  }

  bk_debug_printf_and(B, 1, "Prepare to fork in pid %d\n", getpid());
  switch (pid = fork())
  {
  case -1:
    bk_error_printf(B, BK_ERR_ERR, "Fork failed: %s\n", strerror(errno));
    goto error;
    break;

  case 0:
    bk_debug_printf_and(B, 1, "Created child pid %d\n", getpid());
    // Child
    fdin = fileno(stdin);
    fdout = fileno(stdout);
    fderr = fileno(stderr);

    bk_debug_printf_and(B,4,"Child: stdin: %d, stdout: %d\n", fdin, fdout);

    if (BK_FLAG_ISSET(flags, BK_PIPE_FLAG_USE_PTY))
    {
#ifdef TIOCNOTTY
      int fd = -1;
#endif /* TIOCNOTTY */

#if defined(HAVE_SETPGID) && !defined(HAVE_SETSID)
      if (setpgid(0, (pid = getpid())) == -1)
      {
	bk_error_printf(B, BK_ERR_ERR, "setpgid: %s\n", strerror(errno));
	goto error;
      }
#else
      // should always succeed in child process
      setsid();
#endif

#ifdef TIOCNOTTY
      /* bail on a controlling tty */
      if ((fd = open("/dev/tty", O_RDWR)) >= 0)
      {
	if (ioctl(fd, TIOCNOTTY, (char *) 0) == -1)
	{
	  bk_error_printf(B, BK_ERR_ERR, "ioctl failed: %s\n", strerror(errno));
	  goto error;
	}
	if (close(fd) == -1)
	{
	  bk_error_printf(B, BK_ERR_ERR, "ioctl close: %s\n", strerror(errno));
	  goto error;
	}
      }
#endif /* TIOCNOTTY */
    }

    if (BK_FLAG_ISSET(flags, BK_PIPE_TO_PROCESS_FLAG_CLOSE_EXTRANEOUS_DESC))
    {
      int cnt;
      bk_debug_printf_and(B,2,"Closing fd's\n");

      for (cnt=0; cnt < getdtablesize(); cnt++)
      {
	if (cnt != fdin && cnt != fdout && cnt != fderr &&
	    cnt != c2p[0] && cnt != c2p[1] &&
	    cnt != p2c[0] && cnt != p2c[1])
	{
	  if (close(cnt) == 0)
	  {
	    bk_debug_printf_and(B,2,"Child: closing extraneous descriptor: %d\n", cnt);
	  }
	}
      }
    }

    if (fdoutp)
    {
      // Parent --> child (close child side write; dup child side read).
      close(p2c[1]);

      bk_debug_printf_and(B,2,"Child: closing p2c write: %d\n", p2c[1]);

      if (dup2(p2c[0],fdin) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "dup failed: %s\n", strerror(errno));
	goto error;
      }
      if (p2c[0] != fdin)
	close(p2c[0]);

      bk_debug_printf_and(B,2,"Child: closing p2c read (after dup to %d): %d\n", fdin, p2c[0]);

    }

    if (fdinp)
    {
      close(c2p[0]);

      bk_debug_printf_and(B,2,"Child: closing c2p read: %d\n", c2p[0]);

      if (dup2(c2p[1],fdout) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "dup failed: %s\n", strerror(errno));
	goto error;
      }

      if (BK_FLAG_ISSET(flags, BK_PIPE_FLAG_STDERR_ON_STDOUT) && dup2(c2p[1],fderr) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "dup failed: %s\n", strerror(errno));
	goto error;
      }
      if (c2p[1] != fdout && c2p[1] != fderr)
	close(c2p[1]);

      bk_debug_printf_and(B,2,"Child: closing c2p write (after dup to %d): %d\n", fdout, c2p[1]);
    }
    break;

  default:
    bk_debug_printf_and(B, 1, "Parent %d found child pid %d\n", getpid(), pid);

    // Parent
    if (fdinp)
    {
      // Child --> parent. Close parent side write. Return parent side read.
      close(c2p[1]);

      bk_debug_printf_and(B,2,"Parent: closing c2p write: %d\n", c2p[1]);

      if (fdinp && *fdinp == -1)
      {
	*fdinp = c2p[0];
      }
      else
      {
	if (dup2(c2p[0], *fdinp) < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "dup failed: %s\n", strerror(errno));
	  goto error;
	}
	close(c2p[0]);

	bk_debug_printf_and(B,2,"Parent: closing c2p read (after dup to %d): %d\n", *fdinp, p2c[0]);
      }
    }

    if (fdoutp)
    {
      // Parent --> child. Close parent side read. Return parent side write.
      close(p2c[0]);

      bk_debug_printf_and(B,2,"Parent: closing p2c read: %d\n", p2c[0]);

      if (fdoutp && *fdoutp == -1)
      {
	*fdoutp = p2c[1];
      }
      else
      {
	if (dup2(p2c[1], *fdoutp) < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "dup failed: %s\n", strerror(errno));
	  goto error;
	}
	close(p2c[1]);

	bk_debug_printf_and(B,2,"Parent: closing p2c write (after dup to %d): %d\n", *fdoutp, p2c[1]);
      }
    }
    break;
  }

  BK_RETURN(B,pid);

 error:
  /*
   * Since we haven't really done anything here SIGKILL should really be OK
   * and avoids the need to check for existing signal handlers.
   */
  if (pid)
    kill(pid, SIGKILL);

  // Close created descriptors here.
  close(p2c[0]);
  close(p2c[1]);
  close(c2p[0]);
  close(c2p[1]);

  BK_RETURN(B,-1);
}



/**
 * Exec a process.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param proc The proces to exec.
 *	@param args The argument vector to use.
 *	@param env The enviroment to use (NULL means keep current).
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>does not return</i> of child on success.
 */
int
bk_exec(bk_s B, const char *proc, char *const *args, char *const *env, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int (*execptr)(const char *, char *const *) = execv;
  char *exec = NULL;

  if (!proc)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (BK_FLAG_ISSET(flags, BK_EXEC_FLAG_SEARCH_PATH))
  {
    if (env)
      proc = exec = bk_search_path(B, proc, NULL, X_OK, 0);
    else
      execptr = execvp;
  }

  if (env)
  {
    if (execve(proc, args, env) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "exec failed: %s\n", strerror(errno));
      goto error;
    }
  }
  else
  {
    if ((*execptr)(proc, args) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "exec failed: %s\n", strerror(errno));
      goto error;
    }
  }

  /* NOTREACHED */
  BK_RETURN(B,0);

 error:

  if (exec)
    free(exec);
  BK_RETURN(B,-1);
}




/**
 * Exececute a process using a command line interface.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param cmd The command to execute.
 *	@param env The enviroment to use (NULL means keep current).
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>does not return</i> of child on success.
 */
int
bk_exec_cmd(bk_s B, const char *cmd, char *const *env, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!cmd)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  BK_RETURN(B,bk_exec_cmd_tokenize(B, cmd, env, 0, NULL, NULL, NULL, 0, flags));
}



/**
 * Execute a process using a command line interface. This function allows
 * control over the tokenizer.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param cmd The command to execute.
 *	@param env The enviroment to use (NULL means keep current).
 *	@param limit the number of tokens to create.
 *	@param spliton String used to split up the command.
 *	@param kvht_vardb Hash Table Variable
 *	@param variabledb DB of expansion variables.
 *	@param tokenizd_flags Flags to pass to tokenize routine
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>does not return</i> of child on success.
 */
int
bk_exec_cmd_tokenize(bk_s B, const char *cmd, char *const *env, u_int limit, const char *spliton, const dict_h kvht_vardb, const char **variabledb, bk_flags tokenize_flags, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char **args = NULL;

  if (!cmd)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!(args = bk_string_tokenize_split(B, cmd, limit, spliton, kvht_vardb, variabledb, tokenize_flags)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not tokenize string\n");
    goto error;
  }

  if (bk_exec(B, *args, args, env, flags) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "exec failed: %s\n", strerror(errno));
    goto error;
  }

  /* NOTREACHED */
  /* except for error */

 error:
  if (args)
    bk_string_tokenize_destroy(B, args);
  BK_RETURN(B,-1);
}



/**
 * Create a pipe to a process. NB child process does not return if successfull.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param fdinp Copy in/copy out (mostly latter) input descriptor
 *	@param fdoutp Copy in/copy out (mostly latter) output descriptor
 *	@param proc Process to exec.
 *	@param args Process args.
 *	@param env Enviornment (NULL means keep current env).
 *	@param flags Flags for fun
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>pid</i> of child on success in parent.
 *
 *	@bugs It is not really possible the check the validity of the child
 *	process in any meaningful way. Callers are encouraged to use
 *	kill(pid,0) to ensure that the child is running before sending
 *	anything down the pipe.
 */
pid_t
bk_pipe_to_exec(bk_s B, int *fdinp, int *fdoutp, const char *proc, char *const *args, char *const *env, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  pid_t pid;

  if (!proc)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  /*
   * Technically this flag is not necessary. We could require proper
   * initialization of these pointers, but the general concensus is that
   * such an API is so cumbersome as to be broken. Hence the flag.
   */
  if (BK_FLAG_ISCLEAR(flags, BK_EXEC_FLAG_USE_SUPPLIED_FDS))
  {
    if (fdinp)
      *fdinp = -1;
    if (fdoutp)
      *fdoutp = -1;
  }

  pid = bk_pipe_to_process(B, fdinp, fdoutp, ((BK_FLAG_ISSET(flags, BK_EXEC_FLAG_CLOSE_CHILD_DESC)?BK_PIPE_TO_PROCESS_FLAG_CLOSE_EXTRANEOUS_DESC:0) |
					      (BK_FLAG_ISSET(flags, BK_EXEC_FLAG_STDERR_ON_STDOUT)?BK_PIPE_FLAG_STDERR_ON_STDOUT:0) |
					      (BK_FLAG_ISSET(flags, BK_EXEC_FLAG_USE_PTY)?BK_PIPE_FLAG_USE_PTY:0)));

  switch(pid)
  {
  case -1:
    bk_error_printf(B, BK_ERR_ERR, "Could not create pipes or new process\n");
    goto error;
    break;

  case 0:
    if (BK_FLAG_ISSET(flags, BK_EXEC_FLAG_TOSS_STDERR) && stderr)
    {
      int fd = fileno(stderr);
      int nullfd;
      char *path = _PATH_DEVNULL;

      if ((nullfd = open(path, O_WRONLY)) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not open %s: %s\n",
			path, strerror(errno));
	// Whatever, forge on.
      }
      else
      {
	if (dup2(nullfd, fd) < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not dup stderr to %s: %s\n",
			  path, strerror(errno));
	}
	close(nullfd);
      }
    }

    if (BK_FLAG_ISSET(flags, BK_EXEC_FLAG_TOSS_STDOUT) && stdout)
    {
      int fd = fileno(stdout);
      int nullfd;
      char *path = _PATH_DEVNULL;

      if ((nullfd = open(path, O_WRONLY)) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not open %s: %s\n",
			path, strerror(errno));
	// Whatever, forge on.
      }
      else
      {
	if (dup2(nullfd, fd) < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not dup stdout to %s: %s\n",
			  path, strerror(errno));
	}
	close(nullfd);
      }
    }

    if (bk_exec(B, proc, args, env, flags) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not exec child process\n");
      // <TODO> syslog? </TODO>
      exit(1);
    }
    // Child
    break;

  default:
    break;
  }

  BK_RETURN(B,pid);

 error:

  // <TODO> Should we kill of the process here if we have had an error? </TODO>

  // If we set the fd's then close them
  if (BK_FLAG_ISCLEAR(flags, BK_EXEC_FLAG_USE_SUPPLIED_FDS))
  {
    if (fdinp && *fdinp != -1)
      close(*fdinp);
    if (fdoutp && *fdoutp != -1)
      close(*fdoutp);
  }
  BK_RETURN(B,-1);
}



/**
 * Create a pipe to a subprocess using a command line argument with fine
 * control over the the tokenizer. NB: the child does not return.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param fdinp Copy in/out (though mostly out) input descriptor. Creates child --> parent pipe.
 *	@param fdoutp Copy in/out (though mostly out) output descriptor. Creates parent --> child pipe.
 *	@param cmd The command to execute.
 *	@param env The enviroment to use (NULL means keep current).
 *	@param limit the number of tokens to create.
 *	@param spliton String used to split up the command.
 *	@param kvht_vardb Hash Table Variable
 *	@param variabledb DB of expansion variables.
 *	@param tokenizd_flags Flags to pass to tokenize routine
 *	@param flags Flags  are fun.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>pid</i> on success.
 */
pid_t
bk_pipe_to_cmd_tokenize(bk_s B, int *fdinp, int *fdoutp, const char *cmd, char *const *env, u_int limit, const char *spliton, const dict_h kvht_vardb, const char **variabledb, bk_flags tokenize_flags, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  pid_t pid;

  if (!cmd)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  /*
   * Technically this flag is not necessary. We could require proper
   * initialization of these pointers, but the general concensus is that
   * such an API is so cumbersome as to be broken. Hence the flag.
   */
  if (BK_FLAG_ISCLEAR(flags, BK_EXEC_FLAG_USE_SUPPLIED_FDS))
  {
    if (fdinp)
      *fdinp = -1;
    if (fdoutp)
      *fdoutp = -1;
  }

  pid = bk_pipe_to_process(B, fdinp, fdoutp, ((BK_FLAG_ISSET(flags, BK_EXEC_FLAG_CLOSE_CHILD_DESC)?BK_PIPE_TO_PROCESS_FLAG_CLOSE_EXTRANEOUS_DESC:0) |
					      (BK_FLAG_ISSET(flags, BK_EXEC_FLAG_STDERR_ON_STDOUT)?BK_PIPE_FLAG_STDERR_ON_STDOUT:0) |
					      (BK_FLAG_ISSET(flags, BK_EXEC_FLAG_USE_PTY)?BK_PIPE_FLAG_USE_PTY:0)));

  switch(pid)
  {
  case -1:
    bk_error_printf(B, BK_ERR_ERR, "Could not create pipes or new process\n");
    goto error;
    break;

  case 0:
    if (bk_exec_cmd_tokenize(B, cmd, env, limit, spliton, kvht_vardb, variabledb, tokenize_flags, flags) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not exec child process\n");
      // <TODO> syslog? </TODO>
      exit(1);
    }
    // Child
    break;

  default:
    break;
  }

  BK_RETURN(B,pid);

 error:

  // <TODO> Should we kill of the process here if we have had an error? </TODO>

  // If we set the fd's then close them
  if (BK_FLAG_ISCLEAR(flags, BK_EXEC_FLAG_USE_SUPPLIED_FDS))
  {
    if (fdinp && *fdinp != -1)
      close(*fdinp);
    if (fdoutp && *fdoutp != -1)
      close(*fdoutp);
  }
  BK_RETURN(B,-1);

}



/**
 * Create a pipe to a subprocess using a command line argument.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param fdinp Copy in/out (though mostly out) input descriptor. Creates child --> parent pipe.
 *	@param fdoutp Copy in/out (though mostly out) output descriptor. Creates parent --> child pipe.
 *	@param cmd The command to execute.
 *	@param env The enviroment to use (NULL means keep current).
 *	@param flags Flags  are fun.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>pid</i> of child on success.
 */
pid_t
bk_pipe_to_cmd(bk_s B, int *fdinp,int *fdoutp, const char *cmd, char *const *env, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  BK_RETURN(B, bk_pipe_to_cmd_tokenize(B, fdinp, fdoutp, cmd, env, 0, NULL, NULL, NULL, 0, flags));
}




/**
 * Seach the (possibly supplied) path for the given process.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param proc The proces to search for.
 *	@param path The path to use. NULL means use PATH.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return allocated <i>path</i> on success.
 */
char *
bk_search_path(bk_s B, const char *proc, const char *path, int mode, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *ret = NULL;
  char *tmp_path = NULL;
  int proc_len;
  char *p, *q;
  int len;

  if (!proc)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  proc_len = strlen(proc);

  if (!path)
  {
    if (!(p = (char *)bk_getenv(B, "PATH")))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not determine path\n");
      goto error;
    }
  }
  else
  {
    p = (char *)path;
  }

  if (!(tmp_path = strdup(p)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not make local copy of path\n");
    goto error;
  }

  q = tmp_path;
  while(p = strchr(q, BK_PATH_SEPARATOR[0]))
  {
    *p++ = '\0';
    len = strlen(q) + proc_len + 2;
    if (ret)
      free(ret);
    if (!(ret = malloc(len)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not malloc space for return path: %s\n", strerror(errno));
      goto error;
    }
    // <TODO> autoconf the directory separator maybe (Windows allows /) </TODO>
    snprintf(ret, len, "%s/%s", q, proc);
    if (access(ret, mode) == 0)
    {
      free(tmp_path);
      BK_RETURN(B,ret);
    }
    q = p;
  }

  // Deliberate fall through
 error:
  if (tmp_path)
    free(tmp_path);

  if (ret)
    free(ret);
  BK_RETURN(B,NULL);
}



#ifdef BK_USING_PTHREADS
#ifdef MISSING_PTHREAD_RWLOCK_INIT
/**
 * Initialize the rwlock if we're on a system with funky pthreads.
 *
 * @param B BAKA thread/global state.
 * @return <i>0</i> on success<br>
 * @return <i>non-zero</i> otherwise
 */
int
bk_envlock_init(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret = 0;
  if (!bkenvlock_initialized)
  {
    bkenvlock_initialized = 1;
    ret = pthread_rwlock_init(&bkenvlock, NULL);
  }

  BK_RETURN(B, ret);
}
#endif /* MISSING_PTHREAD_RWLOCK_INIT */
#endif /* BK_USING_PTHREADS */



/**
 * Put an environment variable into the system using the setenv(2) API but
 * the putenv(2) function.
 *
 * <WARNING>
 * This is too stupid for words! The SUSv2 specification for putenv(3), for
 * reasons passing *all* understanding, requires that the *pointer* to the
 * string be saved in the evironment, *not* a copy of the data. Hence it is
 * illegal to use any memory which might go be freed during the use of the
 * variable. So in this function we are *forced* to *leak memory* (!!) by
 * allocating the string space and then forgetting about the string.
 *
 * NB: It *appears* (from some, but not all, putenv(3) manpages) that it is
 * safe to call putenv with an automatic variable (or one of an unknown
 * source) if there is no "=" in the string. This (evidently) causes the
 * variable to be unset so it shouldn't matter if then gets freed.
 * </WARNING>
 *
 * THREADS: THREAD-REENTRANT (assuming all environment accesses are via bk_*env)
 *
 *	@param B BAKA thread/global state.
 *	@param key Environment variable name
 *	@param value The value of the variable.
 *	@param overwrite. Ignored (overwrite assumed), but needed for the API.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_setenv_with_putenv(bk_s B, const char *key, const char *value, int overwrite)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *str = NULL;
  int len;

  if (!key || !value)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  len = strlen(key) + strlen(value) + 2; // 2: provide space for "=" and '\0'

  if (!BK_MALLOC_LEN(str, len))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate space for environment string: %s\n", strerror(errno));
    goto error;
  }

  snprintf(str, len, "%s=%s", key,value);

#ifdef BK_USING_PTHREADS
  if (pthread_rwlock_wrlock(&bkenvlock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  len = putenv(str);

#ifdef BK_USING_PTHREADS
  if (pthread_rwlock_unlock(&bkenvlock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (len < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert \'%s\' into environment: %s\n", str, strerror(errno));
    goto error;
  }

  BK_RETURN(B,0);

 error:
  // Presumably it's OK to free the string if an error occured.
  if (str) free(str);
  BK_RETURN(B,-1);
}



/**
 * Get an environment variable from the system
 *
 * THREADS: MT-SAFE (assuming all environment accesses are bk_ protected)
 *
 *	@param B BAKA thread/global state.
 *	@param key Environment variable name
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>Variable</i> on success.
 */
const char *bk_getenv(bk_s B, const char *key)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *str = NULL;

  if (!key)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

#ifdef BK_USING_PTHREADS
  if (pthread_rwlock_rdlock(&bkenvlock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  str = getenv(key);

#ifdef BK_USING_PTHREADS
  if (pthread_rwlock_unlock(&bkenvlock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B, str);
}
