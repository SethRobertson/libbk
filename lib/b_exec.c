#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_exec.c,v 1.5 2002/03/18 21:40:18 jtt Exp $";
static char libbk__copyright[] = "Copyright (c) 2001";
static char libbk__contact[] = "<projectbaka@baka.org>";
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
 * All of the baka run public and private functions.
 */

#include <libbk.h>
#include "libbk_internal.h"


/**
 * Create a pipe to a subprocess and duplicate various descriptors. If the
 * either of the copy in variables are NonNULL and set to -1, then the
 * approrpriate descriptor (as determined fileno()) will be assumed. If
 * either is both nonNULL and not -1, then <em>that</em> descriptor value
 * is used.
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
  pid_t pid = 0;

  if (fdinp)
  {
    // We want child --> parent pipe.
    if (pipe(c2p) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create child --> parent pipe: %s\n", strerror(errno));
      goto error;
    }
  }

  if (fdoutp)
  {
    // We want parent --> child pipe.
    if (pipe(p2c) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create parent --> child pipe: %s\n", strerror(errno));
      goto error;
    }
  }

  switch (pid = fork())
  {
  case -1:
    bk_error_printf(B, BK_ERR_ERR, "Fork failed: %s\n", strerror(errno));
    goto error;
    break;

  case 0:
    // Child
    fdin = fileno(stdin);
    fdout = fileno(stdout);

    if (fdoutp)
    {
      // Parent --> child (close child side write; dup child side read).
      close(p2c[1]);
      if (dup2(p2c[0],fdin) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "dup failed: %s\n", strerror(errno));
	goto error;
      }
      close(p2c[0]);
    }

    if (fdinp)
    {
      close(c2p[0]);
      if (dup2(c2p[1],fdout) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "dup failed: %s\n", strerror(errno));
	goto error;
      }
      close(c2p[1]);
    }
    break;
    
  default:
    // Parent
    if (fdinp)
    {
      // Child --> parent. Close parent side write. Return parent side read.
      close(c2p[1]);
      
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
      }
    }

    if (fdoutp)
    {
      // Parent --> child. Close parent side read. Return parent side write.
      close(p2c[0]);
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

  BK_RETURN(B,bk_exec_cmd_tokenize(B, cmd, env, 0, NULL, NULL, 0, flags));  
}




/**
 * Execute a process using a command line interface. This function allows
 * control over the tokenizer.
 *
 *	@param B BAKA thread/global state.
 *	@param cmd The command to execute.
 *	@param env The enviroment to use (NULL means keep current).
 *	@param limit the number of tokens to create.
 *	@param spliton String used to split up the command.
 *	@param variabledb DB of expansion variables.
 *	@param tokenizd_flags Flags to pass to tokenize routine
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>does not return</i> of child on success.
 */
int
bk_exec_cmd_tokenize(bk_s B, const char *cmd, char *const *env, u_int limit, const char *spliton, void *variabledb, bk_flags tokenize_flags, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char **args = NULL;

  if (!cmd)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  if (!(args = bk_string_tokenize_split(B, cmd, limit, spliton, variabledb, tokenize_flags)))
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
  
  pid = bk_pipe_to_process(B, fdinp, fdoutp, 0);

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
      long dummy;
      char *path = BK_GWD(B, "bk_path_dev_null", _PATH_DEVNULL);

      if ((nullfd = open(path, O_WRONLY)) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not open %s: %s\n", path, strerror(errno));
	// Whatever, forge on.
      }
      else
      {
	if (dup2(nullfd, fd) < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not dup stderr to %s: %s\n", path, strerror(errno));
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
 *	@param B BAKA thread/global state.
 *	@param fdinp Copy in/out (though mostly out) input descriptor. Creates child --> parent pipe.
 *	@param fdoutp Copy in/out (though mostly out) output descriptor. Creates parent --> child pipe.
 *	@param cmd The command to execute.
 *	@param env The enviroment to use (NULL means keep current).
 *	@param limit the number of tokens to create.
 *	@param spliton String used to split up the command.
 *	@param variabledb DB of expansion variables.
 *	@param tokenizd_flags Flags to pass to tokenize routine
 *	@param flags Flags  are fun.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>pid</i> on success.
 */
pid_t
bk_pipe_to_cmd_tokenize(bk_s B, int *fdinp, int *fdoutp, const char *cmd, char *const *env, u_int limit, const char *spliton, void *variabledb, bk_flags tokenize_flags, bk_flags flags)
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
  
  pid = bk_pipe_to_process(B, fdinp, fdoutp, 0);

  switch(pid)
  {
  case -1:
    bk_error_printf(B, BK_ERR_ERR, "Could not create pipes or new process\n");
    goto error;
    break;
      
  case 0:
    if (bk_exec_cmd_tokenize(B, cmd, env, limit, spliton, variabledb, tokenize_flags, flags) < 0)
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
  
  BK_RETURN(B, bk_pipe_to_cmd_tokenize(B, fdinp, fdoutp, cmd, env, 0, NULL, NULL, 0, flags));
}




/**
 * Seach the (possibly supplied) path for the given process.
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
    if (!(p = getenv("PATH")))
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
