#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_error.c,v 1.4 2001/08/31 05:03:35 seth Exp $";
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


#define MAXERRORLINE 8192

#define errq_create(o,k,f,a)		dll_create(o,k,f)
#define errq_destroy(h)			dll_destroy(h)
#define errq_insert(h,o)		dll_insert(h,o)
#define errq_insert_uniq(h,n,o)		dll_insert_uniq(h,n,o)
#define errq_search(h,k)		dll_search(h,k)
#define errq_delete(h,o)		dll_delete(h,o)
#define errq_minimum(h)			dll_minimum(h)
#define errq_maximum(h)			dll_maximum(h)
#define errq_successor(h,o)		dll_successor(h,o)
#define errq_predecessor(h,o)		dll_predecessor(h,o)
#define errq_iterate(h,d)		dll_iterate(h,d)
#define errq_nextobj(h)			dll_nextobj(h)
#define errq_error_reason(h,i)		dll_error_reason(h,i)


static char bk_error_sysloglevel_char(int sysloglevel);
static void be_error_output(bk_s B, FILE *fh, int sysloglevel, struct bk_error_node *node, bk_flags flags);
#define BE_ERROR_SYSLOG_WANT_FULL  1



/*
 * Initialize the error queue and structure
 */
struct bk_error *bk_error_init(bk_s B, u_int16_t queuelen, FILE *fh, int syslogthreshhold, bk_flags flags)
{
  struct bk_error *beinfo;

  if (!(beinfo = malloc(sizeof(*beinfo))))
  {
    if (fh)
      fprintf(fh, __FUNCTION__/**/": Could not allocate storage for error queue structure: %s\n",strerror(errno));
    return(NULL);
  }

  beinfo->be_fh = fh;
  beinfo->be_hilo_pivot = BK_ERROR_PIVOT;
  beinfo->be_sysloglevel = syslogthreshhold;
  beinfo->be_curHiSize = 0;
  beinfo->be_curLowSize = 0;
  beinfo->be_maxsize = queuelen;

  if (!(beinfo->be_hiqueue = errq_create(NULL, NULL, DICT_UNORDERED, NULL)))
  {
    if (fh)
      fprintf(fh, __FUNCTION__/**/": Could not create hi error queue: %s\n",errq_error_reason(NULL, NULL));
    goto error;
  }

  if (!(beinfo->be_lowqueue = errq_create(NULL, NULL, DICT_UNORDERED, NULL)))
  {
    if (fh)
      fprintf(fh, __FUNCTION__/**/": Could not create low error queue: %s\n",errq_error_reason(NULL, NULL));
    goto error;
  }

  return(beinfo);

 error:
  bk_error_destroy(B, beinfo);
  return(NULL);
}



/*
 * Get rid of the error queues and all other information.
 */
void bk_error_destroy(bk_s B, struct bk_error *beinfo)
{
  struct bk_error_node *node;

  if (!beinfo)
  {
    /* XXX - invalid argument */
    return;
  }

  DICT_NUKE_CONTENTS(beinfo->be_hiqueue, errq, node, break, if (node->ben_msg) free(node->ben_msg); free(node));
  errq_destroy(beinfo->be_hiqueue);

  DICT_NUKE_CONTENTS(beinfo->be_lowqueue, errq, node, break, if (node->ben_msg) free(node->ben_msg); free(node));
  errq_destroy(beinfo->be_lowqueue);

  free(beinfo);

  return;
}



/*
 * Allow modification of configuration parameters
 */
void bk_error_config(bk_s B, struct bk_error *beinfo, u_int16_t queuelen, FILE *fh, int syslogthreshhold, int hilo_pivot, bk_flags flags)
{
  if (!beinfo)
  {
    /* XXX - Invalid arguments */
    return;
  }

  beinfo->be_fh = fh;
  beinfo->be_hilo_pivot = hilo_pivot;
  beinfo->be_sysloglevel = syslogthreshhold;
  beinfo->be_maxsize = queuelen;

  return;
}



/*
 * Internal version print -- save time, buffer marked up with function
 * name and error level, and if necessary output.
 */
void bk_error_iprint(bk_s B, int sysloglevel, struct bk_error *beinfo, char *buf)
{
  const char *funname;
  char *outline;
  time_t curtime = time(NULL);
  struct bk_error_node *node;
  int tmp;
  int level = bk_error_sysloglevel_char(sysloglevel);
  dict_h be_queue;
  u_short *be_cursize;
  
  if (!(funname = bk_fun_funname(B, 0, 0)))
  {
    /* XXX - Cannot determine function name */
    funname = "?";
  }

  if (!(node = malloc(sizeof(*node))))
  {
    /* XXX - cannot allocate storage for error node */
    return;
  }
  node->ben_time = curtime;
  node->ben_level = sysloglevel;

  tmp = strlen(funname) + strlen(buf) + 6;
  if (!(node->ben_msg = malloc(tmp)))
  {
    /* XXX - cannot allocate storage for error message */
    goto error;
  }
  snprintf(node->ben_msg, tmp, "%s/%c: %s",funname, level, buf);

  /* Encoded information about OS LOG_* manifest constant numbering */
  if (sysloglevel <= beinfo->be_hilo_pivot && sysloglevel != BK_ERR_NONE)
  {
    be_queue = beinfo->be_hiqueue;
    be_cursize = &beinfo->be_curHiSize;
  }
  else
  {
    be_queue = beinfo->be_lowqueue;
    be_cursize = &beinfo->be_curLowSize;
  }

  /*
   * Nuke stuff off the end until we have one free slot
   * (normally only one, unless maxsize has changed)
   */
  while (*be_cursize > beinfo->be_maxsize)
  {
    struct bk_error_node *last = errq_maximum(be_queue);
    errq_delete(be_queue, last);
    if (last->ben_msg) free(last->ben_msg);
    free(last);
    (*be_cursize)--;
  }

  if (errq_insert(be_queue, node) != DICT_OK)
  {
    /* XXX - CLC insert failed for some reason or another */
    goto error;
  }
  (*be_cursize)++;

  be_error_output(B, beinfo->be_fh, beinfo->be_sysloglevel, node, 0);
  return;

 error:
  if (node)
  {
    if (node->ben_msg) free(node->ben_msg);
    free(node);
  }
  return;
}



/*
 * Varargs error buffer add
 */
void bk_error_iprintf(bk_s B, int sysloglevel, struct bk_error *beinfo, char *format, ...)
{
  va_list args;
  char buf[MAXERRORLINE];

  if (!beinfo || !format)
  {
    /* XXX - Invalid argument */
    return;
  }

  va_start(args, format);
  vsnprintf(buf,sizeof(buf),format,args);
  va_end(args);

  bk_error_iprint(B, sysloglevel, beinfo, buf);

  return;
}



/*
 * Print a buffer into an error buffer
 */
void bk_error_iprintbuf(bk_s B, int sysloglevel, struct bk_error *beinfo, char *intro, char *prefix, bk_vptr *buf)
{
  char *out;

  if (!(out = bk_string_printbuf(B, intro, prefix, buf, 0)))
  {
    /* XXX - Could not convert buffer for debug printing */
    return;
  }

  bk_error_iprintf(B, sysloglevel, beinfo, out);
  return;
}



/*
 * Put prevectored buffer into error queue
 */
void bk_error_ivprintf(bk_s B, int sysloglevel, struct bk_error *beinfo, char *format, va_list ap)
{
  char buf[MAXERRORLINE];

  if (!beinfo || !format)
  {
    /* XXX - Invalid argument */
    return;
  }
    
  vsnprintf(buf,sizeof(buf),format,ap);
  bk_error_iprint(B, sysloglevel, beinfo, buf);

  return;
}



/*
 * Dump error queues
 */
void bk_error_idump(bk_s B, struct bk_error *beinfo, FILE *fh, int sysloglevel, bk_flags flags)
{
  struct bk_error_node *hi, *lo, *cur;

  hi = errq_maximum(beinfo->be_hiqueue);
  lo = errq_maximum(beinfo->be_lowqueue);

  while (hi || lo)
  {
    int wanthi;

    if (!hi || !lo)
      wanthi = !lo;
    else
      wanthi = (hi->ben_time < lo->ben_time);

    if (wanthi)
    {
      cur = hi;
      hi = errq_predecessor(beinfo->be_hiqueue, hi);
    }
    else
    {
      cur = lo;
      lo = errq_predecessor(beinfo->be_lowqueue, lo);
    }

    be_error_output(B, fh, sysloglevel, cur, 0);
  }
}



/*
 * Syslog level conversions
 */
static char bk_error_sysloglevel_char(int sysloglevel)
{
  switch(sysloglevel)
  {
  case BK_ERR_CRIT: return('C');
  case BK_ERR_ERR: return('E');
  case BK_ERR_WARN: return('W');
  case BK_ERR_NOTICE: return('N');
  case BK_ERR_DEBUG: return('D');
  default: return('?');
  }
}



/*
 * Output a error node to file handle or syslog
 */
static void be_error_output(bk_s B, FILE *fh, int sysloglevel, struct bk_error_node *node, bk_flags flags)
{
  int tmp;
  char timeprefix[40];
  char fullprefix[40];
  struct tm *tm = localtime(&node->ben_time);

  if ((tmp = strftime(timeprefix, sizeof(timeprefix), "%m/%d %T", tm)) != 14)
  {
    bk_error_printf(B, BK_ERR_ERR, __FUNCTION__/**/": Somehow strftime produced %d bytes instead of the expected\n",tmp);
    return;
  }

  if (BK_GENERAL_PROGRAM(B))
    snprintf(fullprefix, sizeof(fullprefix), "%s %s[%d]", timeprefix, (char *)BK_GENERAL_PROGRAM(B), getpid());
  else
    strncpy(fullprefix,timeprefix,sizeof(fullprefix));
  fullprefix[sizeof(fullprefix)-1] = 0;		/* Ensure terminating NULL */

 if (sysloglevel != BK_ERR_NONE)
  {
    if (flags & BE_ERROR_SYSLOG_WANT_FULL)
    {
      bk_general_syslog(B, sysloglevel, 0, "%s: %s",fullprefix, node->ben_msg);
    }
    else
    {
      bk_general_syslog(B, sysloglevel, 0, "%s",node->ben_msg);
    }
  }

  if (fh)
  {
    fprintf(fh, "%s: %s",fullprefix, node->ben_msg);
  }

  return;
}
