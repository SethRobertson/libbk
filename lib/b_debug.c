#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_debug.c,v 1.5 2001/08/19 14:07:12 seth Exp $";
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

#define MAXDEBUGLINE 8192

#define debug_create(o,k,f,a)		ht_create(o,k,f,a)
#define debug_destroy(h)		ht_destroy(h)
#define debug_insert(h,o)		ht_insert(h,o)
#define debug_insert_uniq(h,n,o)	ht_insert(h,n,o)
#define debug_search(h,k)		ht_search(h,k)
#define debug_delete(h,o)		ht_delete(h,o)
#define debug_minimum(h)		ht_minimum(h)
#define debug_maximum(h)		ht_maximum(h)
#define debug_successor(h,o)		ht_successor(h,o)
#define debug_predecessor(h,o)		ht_predecessor(h,o)
#define debug_iterate(h,d)		ht_iterate(h,d)
#define debug_nextobj(h)		ht_nextobj(h)
#define debug_error_reason(h,i)		ht_error_reason(h,i)

static int debug_oo_cmp(struct bk_debugnode *a, struct bk_debugnode *b);
static int debug_ko_cmp(char *a, struct bk_debugnode *b);
static unsigned int debug_obj_hash(struct bk_debugnode *b);
static unsigned int debug_key_hash(char *a);
static struct ht_args debug_args = { 127, 2, (ht_func)debug_obj_hash, (ht_func)debug_key_hash };



/*
 * Initialize the debugging structures (don't actually attempt to start debugging)
 */
struct bk_debug *bk_debug_init(bk_s B, bk_flags flags)
{
  struct bk_debug *ret = NULL;

  if (!(ret = (struct bk_debug *)malloc(sizeof(*ret))))
  {
    bk_error_printf(B, BK_ERR_ERR, __FUNCTION__/**/": Could not allocate debug structure: %s\n",strerror(errno));
    return(NULL);
  }
  memset(ret, 0, sizeof(*ret));

  ret->bd_leveldb = NULL;
  ret->bd_fh = NULL;

  return(ret);
}



/*
 * Get rid of all debugging memory
 */
void bk_debug_destroy(bk_s B, struct bk_debug *bd)
{
  if (!bd)
  {
    bk_error_printf(B, BK_ERR_NOTICE, __FUNCTION__/**/": Invalid argument\n");
    return;
  }

  if (bd->bd_leveldb)
  {
    struct bk_debugnode *cur;
    DICT_NUKE_CONTENTS(bd->bd_leveldb, debug, cur, bk_error_printf(B, BK_ERR_ERR,"Could not delete item from front of CLC: %s\n",debug_error_reason(bd->bd_leveldb,NULL)), if (cur->bd_name) free(cur->bd_name); free(cur));
    debug_destroy(bd->bd_leveldb);
  }
}



/*
 * Reinitialize debugging
 *  -- reset the debugging database from the most recent config file
 *  -- reset the per-function debug levels
 *
 * Assumes that all debug entries are from default configuration file.
 */
void bk_debug_reinit(bk_s B, struct bk_debug *bd)
{
  struct bk_debugnode *cur;

  if (!bd->bd_leveldb)
    return;					/* Nothing to reinit */

  DICT_NUKE_CONTENTS(bd->bd_leveldb, debug, cur, bk_error_printf(B, BK_ERR_ERR,"Could not delete item from front of CLC: %s\n",debug_error_reason(bd->bd_leveldb,NULL)), if (cur->bd_name) free(cur->bd_name); free(cur));
  bk_debug_setconfig(B, bd, BK_GENERAL_CONFIG(B), BK_GENERAL_PROGRAM(B));

  return;
}



/*
 * Discovery what the debug level is for the current function/package/group/program
 */
u_int32_t bk_debug_query(bk_s B, struct bk_debug *bdinfo, const char *funname, const char *pkgname, const char *grp, bk_flags flags)
{
  struct bk_debugnode *node;

  if (!bdinfo)
  {
    bk_error_printf(B, BK_ERR_ERR, __FUNCTION__/**/": Invalid arguments\n");
    return(0);
  }

  if (node = debug_search(bdinfo->bd_leveldb, (char *)funname))
    return(node->bd_level);

  if (node = debug_search(bdinfo->bd_leveldb, (char *)pkgname))
    return(node->bd_level);

  if (node = debug_search(bdinfo->bd_leveldb, (char *)grp))
    return(node->bd_level);

  return(bdinfo->bd_defaultlevel);
}



/*
 * Set an individual debug level
 *
 * N.B. Calling this function externally may be temporary--a reinit will flush
 * *ALL* items including manually set items (even if it didn't the config
 * file would override the manual items).
 */
int bk_debug_set(bk_s B, struct bk_debug *bdinfo, const char *name, u_int32_t level)
{
  struct bk_debugnode *node;

  if (!bdinfo || !name)
  {
    bk_error_printf(B, BK_ERR_ERR, __FUNCTION__/**/": Invalid arguments\n");
    return(-1);
  }

  if (!bdinfo->bd_leveldb)
  {
    bk_error_printf(B, BK_ERR_NOTICE, __FUNCTION__/**/": Debugging not configured yet\n");
    return(-1);
  }

  if (node = debug_search(bdinfo->bd_leveldb, (char *)name))
  {
    node->bd_level = level;
    return(0);
  }

  if (!(node = malloc(sizeof(*node))))
  {
    bk_error_printf(B, BK_ERR_ERR, __FUNCTION__/**/": Could not allocate memory to set debug level: %s\n",strerror(errno));
    goto error;
  }

  if (!(node->bd_name = strdup(name)))
  {
    bk_error_printf(B, BK_ERR_ERR, __FUNCTION__/**/": Could not strdup debug name %s: %s\n",name,strerror(errno));
    goto error;
  }

  node->bd_level = level;

  if (debug_insert(bdinfo->bd_leveldb, node) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, __FUNCTION__/**/": Could not insert debug node for %s: %s\n",name, debug_error_reason(bdinfo->bd_leveldb, NULL));
    goto error;
  }

  /* Set the program default debug level if the current name is the default */
  if (!strcmp(BK_DEBUG_DEFAULTLEVEL, name))
    bdinfo->bd_defaultlevel = level;

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
  return(-1);
}



/*
 * Set debugging levels from the config file.  Return number of non-fatal errors
 * (or -1 for fatal error).
 */
int bk_debug_setconfig(bk_s B, struct bk_debug *bdinfo, struct bk_config *config, const char *program)
{
  int ret = 0;
  char *value = NULL;
  char **tokenized;

  if (!bdinfo || !config || !program)
  {
    bk_error_printf(B, BK_ERR_ERR, __FUNCTION__/**/": Invalid arguments\n");
    return(-1);
  }

  while (value = bk_config_getnext(B, config, BK_DEBUG_KEY, value))
  {
    u_int32_t level;

    if (!(tokenized = bk_string_tokenize_split(B, value, 0, BK_STRING_TOKENIZE_WHITESPACE, NULL, BK_STRING_TOKENIZE_SIMPLE)))
    {
      bk_error_printf(B, BK_ERR_WARN, __FUNCTION__/**/": Could not tokenize value %s\n",value);
      ret++;
      continue;
    }
    if (!tokenized[0] || !tokenized[1] || !tokenized[2])
    {
      bk_error_printf(B, BK_ERR_WARN, __FUNCTION__/**/": Invalid number of tokens while parsing `%s'\n",value);
      ret++;
      goto next;
    }
    if(!strcmp(tokenized[0], program))
      goto next;

    if (bk_string_atou(B, tokenized[2], &level, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_WARN, __FUNCTION__/**/": Invalid debugging level `%s' in `%s'\n",tokenized[2],value);
      ret++;
      goto next;
    }
    if (!level)
      goto next;

    if (bk_debug_set(B, bdinfo, tokenized[1], level) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, __FUNCTION__/**/": Could not set debugging level for `%s'\n",value);
      ret++;
      goto next;
    }

  next:
    bk_string_tokenize_destroy(B, tokenized);
    tokenized = NULL;
  }

  return(ret);
}



/*
 * Set some debug configuration information -- note you MUST do this before
 * set'ing or setconfig'ing the actual debug levels.
 */
void bk_debug_config(bk_s B, struct bk_debug *bdinfo, FILE *fh, int sysloglevel, bk_flags flags)
{
  if (!bdinfo)
  {
    bk_error_printf(B, BK_ERR_ERR, __FUNCTION__/**/": Invalid arguments\n");
    return;
  }

  if (!(bdinfo->bd_leveldb = debug_create((dict_function)debug_oo_cmp, (dict_function)debug_ko_cmp, DICT_HT_STRICT_HINTS, &debug_args)))
  {
    bk_error_printf(B, BK_ERR_ERR, __FUNCTION__/**/": Could not creat debug queue: %s\n", debug_error_reason(NULL, NULL));
    return;
  }

  bdinfo->bd_fh = fh;
  bdinfo->bd_sysloglevel = sysloglevel;
  bdinfo->bd_flags = flags;

  return;
}



/*
 * Internal--simple print function
 * MM/DD HH:MM:SS: program[pid]: function: 
 */
void bk_debug_iprint(bk_s B, struct bk_debug *bdinfo, char *buf)
{
  const char *funname;
  int tmp;
  
  if (!(funname = bk_fun_funname(B, 0, 0)))
  {
    bk_error_printf(B, BK_ERR_NOTICE, __FUNCTION__/**/": Cannot determine function name\n");
    funname = "?";
  }

  if (bdinfo->bd_sysloglevel != BK_ERR_NONE)
  {
    char *outline;

    tmp = strlen(funname) + strlen(buf) + 10;
    if (!(outline = malloc(tmp)))
    {
      bk_error_printf(B, BK_ERR_ERR, __FUNCTION__/**/": Cannot allocate %d byte string for debug print: %s\n",tmp,strerror(errno));
      return;
    }

    snprintf(outline, tmp, "%s: %s\n",funname, buf);
    syslog(bdinfo->bd_sysloglevel, outline, strlen(outline));
    free(outline);
  }

  if (bdinfo->bd_fh)
  {
    char fullprefix[40];
    time_t curtime = time(NULL);
    struct tm *tm = localtime(&curtime);

    if ((tmp = strftime(fullprefix, sizeof(fullprefix), "%m/%d %T", tm)) != 14)
    {
      bk_error_printf(B, BK_ERR_ERR, __FUNCTION__/**/": Somehow strftime produced %d bytes instead of the expected\n",tmp);
      return;
    }

    if (BK_GENERAL_PROGRAM(B))
      snprintf(fullprefix, sizeof(fullprefix), "%s %s[%d]", fullprefix, BK_GENERAL_PROGRAM(B), getpid());
    fullprefix[sizeof(fullprefix)-1] = 0;		/* Ensure terminating NULL */


    fprintf(bdinfo->bd_fh, "%s: %s: %s\n",fullprefix, funname, buf);
  }

  return;
}



/*
 * Debugging print via varargs
 */
void bk_debug_iprintf(bk_s B, struct bk_debug *bdinfo, char *format, ...)
{
  va_list args;
  char buf[MAXDEBUGLINE];

  if (!bdinfo || !format)
  {
    bk_error_printf(B, BK_ERR_ERR, __FUNCTION__/**/": Invalid argument\n");
    return;
  }
    
  va_start(args, format);
  vsnprintf(buf,sizeof(buf),format,args);
  va_end(args);

  bk_debug_iprint(B, bdinfo, buf);

  return;
}



/*
 * Print a buffer
 */
void bk_debug_iprintbuf(bk_s B, struct bk_debug *bdinfo, char *intro, char *prefix, bk_vptr *buf)
{
  char *out;

  if (!(out = bk_string_printbuf(B, intro, prefix, buf)))
  {
    bk_error_printf(B, BK_ERR_ERR, __FUNCTION__/**/": Could not convert buffer for debug printing\n");
    return;
  }

  bk_debug_iprintf(B, bdinfo, out);
  return;
}



/*
 * Debugging print prevectored
 */
void bk_debug_ivprintf(bk_s B, struct bk_debug *bdinfo, char *format, va_list ap)
{
  char buf[MAXDEBUGLINE];

  if (!bdinfo || !format)
  {
    bk_error_printf(B, BK_ERR_ERR, __FUNCTION__/**/": Invalid argument\n");
    return;
  }
    
  vsnprintf(buf,sizeof(buf),format,ap);
  bk_debug_iprint(B, bdinfo, buf);

  return;
}



/*
 * CLC functions for debug queue
 */
static int debug_oo_cmp(struct bk_debugnode *a, struct bk_debugnode *b)
{
  return(strcmp(a->bd_name, b->bd_name));
}
static int debug_ko_cmp(char *a, struct bk_debugnode *b)
{
  return(strcmp(a,b->bd_name));
}
static unsigned int debug_obj_hash(struct bk_debugnode *a)
{
  return(bk_strhash(a->bd_name, BK_STRHASH_NOMODULUS));
}
static unsigned int debug_key_hash(char *a)
{
  return(bk_strhash(a, BK_STRHASH_NOMODULUS));
}
