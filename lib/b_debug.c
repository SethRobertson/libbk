#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_debug.c,v 1.19 2002/11/11 22:53:58 jtt Exp $";
static const char libbk__copyright[] = "Copyright (c) 2001";
static const char libbk__contact[] = "<projectbaka@baka.org>";
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
 * The baka debug functionality, which allows per-function, file, group, package debugging.
 */

#include <libbk.h>
#include "libbk_internal.h"



#define MAXDEBUGLINE 8192			///< Maximum number of bytes of debugging information produced in one call



/**
 * Information about the current state debugging in the program
 */
struct bk_debug
{
  dict_h	bd_leveldb;			///< Debugging levels by fun/grp/pkg/prg
  u_int32_t	bd_defaultlevel;		///< Default debugging level
  FILE		*bd_fh;				///< Debugging info file handle
  int		bd_sysloglevel;			///< Debugging syslog level
  bk_flags	bd_flags;			///< Fun for the future
#ifdef BK_USING_PTHREADS
  pthread_rwlock_t bd_rwlock;			///< Fun locking activity
#endif /* BK_USING_PTHREADS */
};


/**
 * Information about one particular debugging name/value pair
 */
struct bk_debugnode
{
  char		*bd_name;			///< Name to debug
  u_int32_t	bd_level;			///< Level to debug at
};



/**
 * @name Defines: debug_clc
 * Stored debug levels association CLC definitions
 * to hide CLC choice.
 */
// @{
#define debug_create(o,k,f,a)		ht_create(o,k,f,a)
#define debug_destroy(h)		ht_destroy(h)
#define debug_insert(h,o)		ht_insert(h,o)
#define debug_insert_uniq(h,n,o)	ht_insert_uniq(h,n,o)
#define debug_append(h,o)		ht_append(h,o)
#define debug_append_uniq(h,n,o)	ht_append_uniq(h,n,o)
#define debug_search(h,k)		ht_search(h,k)
#define debug_delete(h,o)		ht_delete(h,o)
#define debug_minimum(h)		ht_minimum(h)
#define debug_maximum(h)		ht_maximum(h)
#define debug_successor(h,o)		ht_successor(h,o)
#define debug_predecessor(h,o)		ht_predecessor(h,o)
#define debug_iterate(h,d)		ht_iterate(h,d)
#define debug_nextobj(h,i)		ht_nextobj(h,i)
#define debug_iterate_done(h,i)		ht_iterate_done(h,i)
#define debug_error_reason(h,i)		ht_error_reason(h,i)

static int debug_oo_cmp(struct bk_debugnode *a, struct bk_debugnode *b);
static int debug_ko_cmp(char *a, struct bk_debugnode *b);
static unsigned int debug_obj_hash(struct bk_debugnode *b);
static unsigned int debug_key_hash(char *a);
static struct ht_args debug_args = { 127, 2, (ht_func)debug_obj_hash, (ht_func)debug_key_hash };
// @}



static int bk_debug_setconfig_i(bk_s B, struct bk_debug *bdinfo, struct bk_config *config, const char *program);




/**
 * Initialize the debugging structures (don't actually attempt to start debugging)
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state 
 *	@param flags Flags for future expansion--saved through run structure.
 *	@return <i>NULL</i> on allocation failure.
 *	@return <br><i>Debug handle</i> on success
 */
struct bk_debug *bk_debug_init(bk_s B, bk_flags flags)
{
  struct bk_debug *ret = NULL;

  if (!(ret = (struct bk_debug *)malloc(sizeof(*ret))))
  {
    bk_error_printf(B, BK_ERR_ERR, "%s: Could not allocate debug structure: %s\n",
		    BK_FUNCNAME, strerror(errno));
    return(NULL);
  }
  memset(ret, 0, sizeof(*ret));

  ret->bd_leveldb = NULL;
  ret->bd_fh = NULL;
  ret->bd_sysloglevel = BK_ERR_NONE;
#ifdef BK_USING_PTHREADS
  pthread_rwlock_init(&ret->bd_rwlock, NULL);
#endif /* BK_USING_PTHREADS */

  return(ret);
}



/**
 * Get rid of all debugging memory.
 *
 * THREADS: REENTRANT
 *
 *	@param B BAKA thread/global state
 *	@param bd Baka debug handle
 */
void bk_debug_destroy(bk_s B, struct bk_debug *bd)
{
  if (!bd)
  {
    bk_error_printf(B, BK_ERR_NOTICE, "%s: Invalid argument\n", BK_FUNCNAME);
    return;
  }

  if (bd->bd_leveldb)
  {
    struct bk_debugnode *cur;
    DICT_NUKE_CONTENTS(bd->bd_leveldb, debug, cur, bk_error_printf(B, BK_ERR_ERR,"Could not delete item from front of CLC: %s\n",debug_error_reason(bd->bd_leveldb,NULL)), if (cur->bd_name) free(cur->bd_name); free(cur));
    debug_destroy(bd->bd_leveldb);
  }
  free(bd);
}



/**
 * Reinitialize debugging
 *  <BR>-- reset the debugging database from the most recent config file
 *  <BR>-- reset the per-function debug levels
 *
 * Assumes that all debug entries are from default configuration file.
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA Thread/global state
 *	@param bd Baka debug handle
 */
void bk_debug_reinit(bk_s B, struct bk_debug *bd)
{
  struct bk_debugnode *cur;

  if (!bd->bd_leveldb)
    return;					/* Nothing to reinit */

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_rwlock_wrlock(&bd->bd_rwlock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  DICT_NUKE_CONTENTS(bd->bd_leveldb, debug, cur, bk_error_printf(B, BK_ERR_ERR,"Could not delete item from front of CLC: %s\n",debug_error_reason(bd->bd_leveldb,NULL)), if (cur->bd_name) free(cur->bd_name); free(cur));
  bk_debug_setconfig_i(B, bd, BK_GENERAL_CONFIG(B), BK_GENERAL_PROGRAM(B));

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_rwlock_unlock(&bd->bd_rwlock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  return;
}



/**
 * Discovery what the debug level is for the current function/package/group/program
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state
 *	@param bdinfo Debug handle
 *	@param funname Function name we should look for
 *	@param pkgname Package name we should look for
 *	@param grp Group name we should look for
 *	@param flags Fun for the future
 *	@return <i>debug level</i> found for this particular function/package/group or the default debug level
 */
u_int32_t bk_debug_query(bk_s B, struct bk_debug *bdinfo, const char *funname, const char *pkgname, const char *grp, bk_flags flags)
{
  struct bk_debugnode *node;
  u_int32_t ret;

  if (!bdinfo)
  {
    bk_error_printf(B, BK_ERR_WARN, "%s: Invalid arguments\n", BK_FUNCNAME);
    return(0);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_rwlock_rdlock(&bdinfo->bd_rwlock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (node = debug_search(bdinfo->bd_leveldb, (char *)funname))
  {
    ret = node->bd_level;
    goto done;
  }

  if (node = debug_search(bdinfo->bd_leveldb, (char *)pkgname))
  {
    ret = node->bd_level;
    goto done;
  }

  if (node = debug_search(bdinfo->bd_leveldb, (char *)grp))
  {
    ret = node->bd_level;
    goto done;
  }

  ret = bdinfo->bd_defaultlevel;

 done:
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_rwlock_unlock(&bdinfo->bd_rwlock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */
  
  return(ret);
}



/**
 * Set an individual debug level
 *
 * N.B. Calling this function externally may be temporary--a reinit will flush
 * *ALL* items including manually set items (even if it didn't the config
 * file would override the manual items).
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA Thread/global state
 *	@param bdinfo Debug handle
 *	@param name Name of function/group/package element to set to a particular debug @a level
 *	@param level The debug level we wish returned when queried.
 *	@see bk_debug_config
 *	@return <i>-1</i> on call failure, initialization failure, allocation failure, other error
 *	@return <BR><i>0</i> on success
 */
int bk_debug_set(bk_s B, struct bk_debug *bdinfo, const char *name, u_int32_t level)
{
  struct bk_debugnode *node;

  if (!bdinfo || !name)
  {
    bk_error_printf(B, BK_ERR_ERR, "%s: Invalid arguments\n", BK_FUNCNAME);
    return(-1);
  }

  if (!bdinfo->bd_leveldb)
  {
    bk_error_printf(B, BK_ERR_NOTICE, "%s: Debugging not configured yet\n",
		    BK_FUNCNAME);
    return(-1);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_rwlock_wrlock(&bdinfo->bd_rwlock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (node = debug_search(bdinfo->bd_leveldb, (char *)name))
  {
    node->bd_level = level;
    goto done;
  }

  if (!(node = malloc(sizeof(*node))))
  {
    bk_error_printf(B, BK_ERR_ERR, "%s: Could not allocate memory to set debug level: %s\n",
		    BK_FUNCNAME, strerror(errno));
    goto error;
  }

  if (!(node->bd_name = strdup(name)))
  {
    bk_error_printf(B, BK_ERR_ERR, "%s: Could not strdup debug name %s: %s\n",name,
		    BK_FUNCNAME, strerror(errno));
    goto error;
  }

  node->bd_level = level;

  if (debug_insert(bdinfo->bd_leveldb, node) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "%s: Could not insert debug node for %s: %s\n",
		    BK_FUNCNAME, name, debug_error_reason(bdinfo->bd_leveldb, NULL));
    goto error;
  }

  /* Set the program default debug level if the current name is the default */
  if (!strcmp(BK_DEBUG_DEFAULTLEVEL, name))
    bdinfo->bd_defaultlevel = level;

 done:
#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_rwlock_unlock(&bdinfo->bd_rwlock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  return(0);

 error:
  if (node)
  {
    if (node->bd_name)
    {
      debug_delete(bdinfo->bd_leveldb, node);
      free(node->bd_name);
    }
    free(node);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_rwlock_unlock(&bdinfo->bd_rwlock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  return(-1);
}



/**
 * External wrapper for bk_debug_setconfig_i --- set debugging levels from the config file
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA Thread/global state
 *	@param bdinfo Debug handle
 *	@param config The config structure to use to source the debugging information
 *	@param program The name of the program to disambiguate debugging configuration for different programs
 *	@see bk_debug_config
 *	@return <i>-1</i> on call failure
 *	@return <br><i>0</i> on total success
 *	@return <br><i>positive</i> on partial success--returning the number of
 *	non-fatal errors (misformatted debug lines)
 */
int bk_debug_setconfig(bk_s B, struct bk_debug *bdinfo, struct bk_config *config, const char *program)
{
  int ret;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_rwlock_wrlock(&bdinfo->bd_rwlock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */
  
  ret = bk_debug_setconfig_i(B, bdinfo, config, program);

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_rwlock_unlock(&bdinfo->bd_rwlock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  return(ret);
}



/**
 * Set debugging levels from the config file.
 *
 * THREADS: REENTRANT
 *
 *	@param B BAKA Thread/global state
 *	@param bdinfo Debug handle
 *	@param config The config structure to use to source the debugging information
 *	@param program The name of the program to disambiguate debugging configuration for different programs
 *	@see bk_debug_config
 *	@return <i>-1</i> on call failure
 *	@return <br><i>0</i> on total success
 *	@return <br><i>positive</i> on partial success--returning the number of
 *	non-fatal errors (misformatted debug lines)
 */
int bk_debug_setconfig_i(bk_s B, struct bk_debug *bdinfo, struct bk_config *config, const char *program)
{
  int ret = 0;
  char *value = NULL;
  char **tokenized;

  if (!bdinfo || !config || !program)
  {
    bk_error_printf(B, BK_ERR_ERR, "%s: Invalid arguments\n", BK_FUNCNAME);
    return(-1);
  }

  while (value = bk_config_getnext(B, config, BK_DEBUG_KEY, value))
  {
    u_int32_t level;

    if (!(tokenized = bk_string_tokenize_split(B, value, 0, BK_WHITESPACE, NULL, BK_STRING_TOKENIZE_SIMPLE)))
    {
      bk_error_printf(B, BK_ERR_WARN, "%s: Could not tokenize value %s\n",
		      BK_FUNCNAME, value);
      ret++;
      continue;
    }
    if (!tokenized[0] || !tokenized[1] || !tokenized[2])
    {
      bk_error_printf(B, BK_ERR_WARN, "%s: Invalid number of tokens while parsing `%s'\n",
		      BK_FUNCNAME, value);
      ret++;
      goto next;
    }
    if(strcmp(tokenized[0], program))
      goto next;				/* Incorrect program name */

    if (bk_string_atou(B, tokenized[2], &level, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_WARN, "%s: Invalid debugging level `%s' in `%s'\n",
		      BK_FUNCNAME, tokenized[2], value);
      ret++;
      goto next;
    }
    if (!level)
      goto next;

    if (bk_debug_set(B, bdinfo, tokenized[1], level) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "%s: Could not set debugging level for `%s'\n",
		      BK_FUNCNAME, value);
      ret++;
      goto next;
    }

  next:
    bk_string_tokenize_destroy(B, tokenized);
    tokenized = NULL;
  }

  return(ret);
}



/**
 * Set some debug configuration information.
 *
 * Note you MUST do this before set'ing or setconfig'ing the actual debug levels.
 *
 * THREADS: THREAD-RENTRANT
 *
 *	@param B BAKA Thread/global state
 *	@param bdinfo Debug handle
 *	@param fh The file handle to output debug messages as they occur (NULL to disable)
 *	@param sysloglevel The system log level to output debug messages (BK_ERR_NONE to disable)
 *	@param flags Fun for the future
 *	@see bk_debug_set
 *	@see bk_debug_setconfig
 */
void bk_debug_config(bk_s B, struct bk_debug *bdinfo, FILE *fh, int sysloglevel, bk_flags flags)
{
  if (!bdinfo)
  {
    bk_error_printf(B, BK_ERR_ERR, "%s: Invalid arguments\n", BK_FUNCNAME);
    return;
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_rwlock_wrlock(&bdinfo->bd_rwlock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  if (!(bdinfo->bd_leveldb = debug_create((dict_function)debug_oo_cmp, (dict_function)debug_ko_cmp, DICT_HT_STRICT_HINTS|DICT_THREAD_NOCOALESCE, &debug_args)))
  {
    bk_error_printf(B, BK_ERR_ERR, "%s: Could not creat debug queue: %s\n",
		    BK_FUNCNAME, debug_error_reason(NULL, NULL));
    return;
  }

  bdinfo->bd_fh = fh;
  bdinfo->bd_sysloglevel = sysloglevel;
  bdinfo->bd_flags = flags;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADON(B) && pthread_rwlock_unlock(&bdinfo->bd_rwlock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  return;
}



/**
 * Simple debug print function (normally used through macro)
 *
 * Produced format: "MM/DD HH:MM:SS: program[pid]: function: buf"
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA Thread/global state
 *	@param bdinfo Debug handle
 *	@param buf The string to use as debugging information
 */
void bk_debug_iprint(bk_s B, struct bk_debug *bdinfo, const char *buf)
{
  const char *funname;
  int tmp;
  
  if (!(funname = bk_fun_funname(B, 0, 0)))
  {
    bk_error_printf(B, BK_ERR_NOTICE, "%s: Cannot determine function name\n",
		    BK_FUNCNAME);
    funname = "?";
  }

  if (bdinfo->bd_sysloglevel != BK_ERR_NONE)
  {
    bk_general_syslog(B, bdinfo->bd_sysloglevel, 0, "%s: %s",funname, buf);
  }

  if (bdinfo->bd_fh)
  {
    char timeprefix[40];
    char fullprefix[40];
    time_t curtime = time(NULL);
    struct tm tm;
    localtime_r(&curtime, &tm);			// <TODO>Does this need to be autoconf'd</TODO>

    /* <WARNING>this check should really be done with assert</WARNING> */
    if ((tmp = strftime(timeprefix, sizeof(timeprefix), "%m/%d %H:%M:%S", &tm)) != 14)
    {
      bk_error_printf(B, BK_ERR_ERR, "%s: strftime returns %d != 14\n",
		      BK_FUNCNAME, tmp);
      return;
    }

    if (BK_GENERAL_PROGRAM(B))
      snprintf(fullprefix, sizeof(fullprefix), "%s %s[%d]", timeprefix, (char *)BK_GENERAL_PROGRAM(B), (int)getpid());
    else
      strncpy(fullprefix,timeprefix,sizeof(fullprefix));
    fullprefix[sizeof(fullprefix)-1] = 0;		/* Ensure terminating NULL */


    fprintf(bdinfo->bd_fh, "%s: %s: %s",fullprefix, funname, buf);
  }

  return;
}



/**
 * Debugging print via varargs (normally used through macro)
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA Thread/global state
 *	@param bdinfo Debug handle
 *	@param format The printf-style string to use as debugging information
 *	@param ... The printf-style arguments
 *	@see bk_debug_iprint
 */
void bk_debug_iprintf(bk_s B, struct bk_debug *bdinfo, const char *format, ...)
{
  va_list args;
  char buf[MAXDEBUGLINE];

  if (!bdinfo || !format)
  {
    bk_error_printf(B, BK_ERR_ERR, "%s: Invalid argument\n", BK_FUNCNAME);
    return;
  }
    
  va_start(args, format);
  vsnprintf(buf,sizeof(buf),format,args);
  va_end(args);

  bk_debug_iprint(B, bdinfo, buf);

  return;
}



/**
 * Debugging print of binary data (normally used through macro)
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA Thread/global state
 *	@param bdinfo Debug handle
 *	@param intro The description of the issue or the binary data
 *	@param prefix The data placed in front of every line of ascii data representation
 *	@param buf The vectored binary data to be printed
 *	@see bk_debug_iprint
 */
void bk_debug_iprintbuf(bk_s B, struct bk_debug *bdinfo, const char *intro, const char *prefix, const bk_vptr *buf)
{
  char *out;

  if (!(out = bk_string_printbuf(B, intro, prefix, buf, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "%s: Could not convert buffer for debug printing\n", BK_FUNCNAME);
    return;
  }

  bk_debug_iprintf(B, bdinfo, out);
  free(out);

  return;
}



/**
 * Debugging print of varags data already vararged (normally used through macro)
 *
 * THREADS: THREAD-REENTRANT
 *
 *	@param B BAKA Thread/global state
 *	@param bdinfo Debug handle
 *	@param format The printf-style string of debugging information
 *	@param ap The varargs arguments for the "printf"
 *	@see bk_debug_iprint
 */
void bk_debug_ivprintf(bk_s B, struct bk_debug *bdinfo, const char *format, va_list ap)
{
  char buf[MAXDEBUGLINE];

  if (!bdinfo || !format)
  {
    bk_error_printf(B, BK_ERR_ERR, "%s: Invalid argument\n", BK_FUNCNAME);
    return;
  }
    
  vsnprintf(buf,sizeof(buf),format,ap);
  bk_debug_iprint(B, bdinfo, buf);

  return;
}




/** CLC helper functions and structures for debug_clc */
static int debug_oo_cmp(struct bk_debugnode *a, struct bk_debugnode *b)
{
  return(strcmp(a->bd_name, b->bd_name));
}

/** CLC helper functions and structures for debug_clc */
static int debug_ko_cmp(char *a, struct bk_debugnode *b)
{
  return(strcmp(a,b->bd_name));
}

/** CLC helper functions and structures for debug_clc */
static unsigned int debug_obj_hash(struct bk_debugnode *a)
{
  return(bk_strhash(a->bd_name, BK_STRHASH_NOMODULUS));
}

/** CLC helper functions and structures for debug_clc */
static unsigned int debug_key_hash(char *a)
{
  return(bk_strhash(a, BK_STRHASH_NOMODULUS));
}
