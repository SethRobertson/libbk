#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_general.c,v 1.6 2001/07/06 00:57:30 seth Exp $";
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

#include <libbk.h>
#include "libbk_internal.h"


extern char **environ;				/* Some morons (e.g. linux) do not define */


static struct bk_proctitle *bk_general_proctitle_init(bk_s B, int argc, char ***argv, char ***envp, char **program, bk_flags flags);
static void bk_general_proctitle_destroy(bk_s B, struct bk_proctitle *bkp, bk_flags flags);



/*
 * Grand creation of libbk state structure
 */
bk_s bk_general_init(int argc, char ***argv, char ***envp, char *configfile, int error_queue_length, int log_facility, int syslogthreshhold, bk_flags flags)
{
  bk_s B;

  if (!(B = bk_general_thread_init(NULL, "*MAIN*")))
    goto error;

  if (!(BK_BT_GENERAL(B) = (struct bk_general *)malloc(sizeof(*BK_BT_GENERAL(B)))))
    goto error;
  memset(BK_BT_GENERAL(B), 0, sizeof(*BK_BT_GENERAL(B)));

  if (!(BK_GENERAL_ERROR(B) = bk_error_init(B, error_queue_length, NULL, log_facility, 0)))
    goto error;

  if (!(BK_GENERAL_DEBUG(B) = bk_debug_init(B)))
    goto error;

  if (!(BK_GENERAL_DESTROY(B) = bk_funlist_init(B)))
    goto error;

  if (!(BK_GENERAL_REINIT(B) = bk_funlist_init(B)))
    goto error;

  if (!(BK_GENERAL_CONFIG(B) = bk_config_init(B, configfile, 0)))
    goto error;

  if (!(BK_GENERAL_PROCTITLE(B) = bk_general_proctitle_init(B, argc, argv, envp, &BK_GENERAL_PROGRAM(B), 0)))
    goto error;

  if (log_facility && BK_GENERAL_PROGRAM(B))
    openlog(BK_GENERAL_PROGRAM(B), LOG_NDELAY|LOG_PID, log_facility);

  return(B);

 error:
  bk_general_destroy(B);
  return(NULL);
}



/*
 * Destroy libbk state (generally not a good idea :-)
 */
void bk_general_destroy(bk_s B)
{
  if (B)
  {
    if (BK_BT_GENERAL(B))
    {
      if (BK_GENERAL_DESTROY(B))
      {
	bk_funlist_call(B,BK_GENERAL_DESTROY(B), 0);
	bk_funlist_destroy(B,BK_GENERAL_DESTROY(B));
      }

      if (BK_GENERAL_PROCTITLE(B))
	bk_general_proctitle_destroy(B,BK_GENERAL_PROCTITLE(B), 0);

      if (BK_GENERAL_CONFIG(B))
	bk_config_destroy(B, BK_GENERAL_CONFIG(B));

      if (BK_GENERAL_REINIT(B))
	bk_funlist_destroy(B,BK_GENERAL_REINIT(B));

      if (BK_GENERAL_ERROR(B))
	bk_error_destroy(B,BK_GENERAL_ERROR(B));

      if (BK_GENERAL_DEBUG(B))
	bk_debug_destroy(B,BK_GENERAL_DEBUG(B));

      free(BK_BT_GENERAL(B));
    }
    bk_general_thread_destroy(B);
  }

  return;
}



/*
 * Go through reinitialization
 */
void bk_general_reinit(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (BK_GENERAL_REINIT(B))
    bk_funlist_call(B, BK_GENERAL_REINIT(B), 0);

  BK_VRETURN(B);
}



/*
 * Add to the reinitialization database
 */
int bk_general_reinit_insert(bk_s B, void (*bf_fun)(bk_s, void *, u_int), void *args)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  BK_RETURN(B, bk_funlist_insert(B, BK_GENERAL_REINIT(B), bf_fun, args));
}



/*
 * Remove from the reinitialization database
 */
int bk_general_reinit_delete(bk_s B, void (*bf_fun)(bk_s, void *, u_int), void *args)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  BK_RETURN(B, bk_funlist_delete(B, BK_GENERAL_REINIT(B), bf_fun, args));
}



/*
 * Add to the destruction database
 */
int bk_general_destroy_insert(bk_s B, void (*bf_fun)(bk_s, void *, u_int), void *args)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  BK_RETURN(B, bk_funlist_insert(B, BK_GENERAL_REINIT(B), bf_fun, args));
}



/*
 * Remove from the reinitialization database
 */
int bk_general_destroy_delete(bk_s B, void (*bf_fun)(bk_s, void *, u_int), void *args)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  BK_RETURN(B, bk_funlist_delete(B, BK_GENERAL_REINIT(B), bf_fun, args));
}



/*
 * Set up the per-thread information
 */
bk_s bk_general_thread_init(bk_s B, char *name)
{
  bk_s B1 = NULL;

  if (!name)
  {
    if (B)
      bk_error_printf(B, BK_ERR_WARNING, "Invalid argument\n");
    return(NULL);
  }

  if (!(B1 = (bk_s)malloc(sizeof(*B1))))
  {
    if (B)
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate new bk_thread information\n");
    return(NULL);
  }
  memset(B1,0,sizeof(*B1));

  if (!(BK_BT_FUNSTACK(B1) = bk_fun_init()))
    goto error;

  if (!(BK_BT_THREADNAME(B1) = strdup(name)))
    goto error;

  if (B)
    BK_BT_GENERAL(B1) = BK_BT_GENERAL(B);

  return(B1);

 error:
  bk_general_thread_destroy(B1);
  return(NULL);
}



/*
 * Destroy per-thread information
 */
void bk_general_thread_destroy(bk_s B)
{
  if (B)
  {
    if (BK_BT_FUNSTACK(B))
      bk_fun_destroy(BK_BT_FUNSTACK(B));
    if (BK_BT_THREADNAME(B))
      free((char *)BK_BT_THREADNAME(B));

    /* Specifically do not muck with bt_general which is shared */

    free(B);
  }
}



/*
 * Set the process title (if possible)
 */
void bk_general_proctitle_set(bk_s B, char *title)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  size_t len, rest;

  if (!title)
  {
    bk_error_printf(B,BK_ERR_ERR,"Invalid arguments\n");
    BK_VRETURN(B);
  }

  len = MIN(strlen(title),BK_GENERAL_PROCTITLE(B)->bp_title.len-1);
  rest = BK_GENERAL_PROCTITLE(B)->bp_title.len - len;

  memcpy(BK_GENERAL_PROCTITLE(B)->bp_title.ptr, title, len);
  memset((char *)BK_GENERAL_PROCTITLE(B)->bp_title.ptr + len, 0, rest);

  BK_VRETURN(B);
}


/*
 * Initialize process title information & process name
 */
static struct bk_proctitle *bk_general_proctitle_init(bk_s B, int argc, char ***argv, char ***envp, char **program, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_proctitle *PT;

  if (!(PT = (struct bk_proctitle *)malloc(sizeof(*PT))))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate proctitle buffer: %s\n",strerror(errno));
    BK_RETURN(B, NULL);
  }

  PT->bp_argc = argc;
  PT->bp_argv = argv?*argv:NULL;
  PT->bp_envp = envp?*envp:NULL;
  PT->bp_title.ptr = NULL;
  PT->bp_title.len = 0;
  PT->bp_flags = flags;

  if (!argv || !envp)
    BK_FLAG_SET(PT->bp_flags, BK_PROCTITLE_OFF);

  /*
   * If the OS can support this functionality, create new copy of argv&envp,
   * repoint original pointers to this location, and set up proctitle vector
   */
  if (BK_FLAG_ISCLEAR(flags, BK_PROCTITLE_OFF))
  {
    int envc = 0;

#define ARRAY_DUPLICATE(new,orig,size) \
    { \
      int tmp; \
      \
      if (!((new) = (char **)malloc(((size)+1)*sizeof(char *)))) \
      { \
	bk_error_printf(B, BK_ERR_ERR, "Could not allocate duplicate array: %s\n",strerror(errno)); \
	goto error; \
      } \
      \
      for(tmp=0;tmp<size;tmp++) \
      { \
	if (!((new)[tmp] = (char *)malloc(strlen((orig)[tmp])+1))) \
	{ \
	  bk_error_printf(B, BK_ERR_ERR, "Could not allocate duplicate array entry %d: %s\n",tmp,strerror(errno)); \
	  goto error; \
	} \
      } \
      (new)[tmp] = NULL; \
    }

    /* Create duplicate argv */
    ARRAY_DUPLICATE(PT->bp_argv,*argv,argc);

    /* Spin through envp array looking for last element */
    for(envc = 0; (*envp)[envc]; envc++) ;

    /* Create duplicate envp */
    ARRAY_DUPLICATE(PT->bp_envp,*envp,envc);
    environ = PT->bp_envp;

    /* Figure out the usable vector for overwriting */
    PT->bp_title.ptr = **argv;

    /* If we have an environment--go to end of last element */
    if (envc > 0)
      PT->bp_title.len = ((*envp)[envc-1] + strlen((*envp)[envc-1])) - (char *)PT->bp_title.ptr;
    else
      PT->bp_title.len = (char *)(*envp) - (char *)PT->bp_title.ptr;

    PT->bp_title.len -= 2;			/* BSD wierdness... */
  }

  /* Discover and save program name if required and able */
  if (program)
  {
    char *tmp = NULL;

    if (PT->bp_argv)				/* Do we have info to find program name? */
    {
      if (tmp = strrchr(*PT->bp_argv,'/'))
	tmp++;
      else
	tmp = *PT->bp_argv;
    }

    *program = tmp;
  }

  BK_RETURN(B, PT);

 error:
  bk_general_proctitle_destroy(B, PT, 0);
  BK_RETURN(B, NULL);
}



/*
 * Destroy process title information
 *
 * N.B. Normally processes feel free to keep items like argv pointers
 * around for long periods of time--after all main() is not going to exit.
 * However, once you destroy, the (changed after proctitle_init) argv pointers
 * will be invalid.  Sigh.  Same with environment.
 */
static void bk_general_proctitle_destroy(bk_s B, struct bk_proctitle *PT, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!PT)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_VRETURN(B);
  }

  if (BK_FLAG_ISCLEAR(PT->bp_flags, BK_PROCTITLE_OFF))
  {
    int cntr;

    if (PT->bp_argv)
    {
      for(cntr=0;PT->bp_argv[cntr];cntr++)
	free(PT->bp_argv[cntr]);
      free(PT->bp_argv);
    }
    if (PT->bp_envp)
    {
      for(cntr=0;PT->bp_envp[cntr];cntr++)
	free(PT->bp_envp[cntr]);
      free(PT->bp_envp);
    }
  }
  free(PT);

  BK_VRETURN(B);
}
