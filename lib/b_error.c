#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_error.c,v 1.16 2001/11/29 17:29:23 jtt Exp $";
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
 * The baka error functionality, producing error logs to allow
 * error reporting without assumptions about the consumer or
 * destination of these logs.  baka errors are generally not expected
 * to be used for end-user reporting of errors.  See bk_error_* macros
 */

#include <libbk.h>
#include "libbk_internal.h"



#define MAXERRORLINE 8192



/**
 * Information about the general state of errors in the system
 */
struct bk_error
{
  FILE		*be_fh;				///< Error info file handle
  u_int		be_seqnum;			///< Sequence number
  dict_h	be_markqueue;			///< Queue of marks of queue locations
  dict_h	be_hiqueue;			///< Queue of high priority error messages
  dict_h	be_lowqueue;			///< Queue of low priority error messages
  char		be_hilo_pivot;			///< Pivot value--error severity threshold which determins into which of the above two queues messages will go.
#define BK_ERROR_PIVOT	BK_ERR_ERR		///< Default HI-LO pivot
  char		be_sysloglevel;			///< Error syslog level
  u_short	be_curHiSize;			///< Current queue size
  u_short	be_curLowSize;			///< Current queue size
  u_short	be_maxsize;			///< Maximum queue size
  bk_flags	be_flags;			///< Flags
};


/**
 * Information about one particular error message
 */
struct bk_error_node
{
  time_t	ben_time;			///< Timestamp
  u_int		ben_seq;			///< Sequence number
  int		ben_level;			///< Level of message
  char		*ben_msg;			///< Actual message
};



/**
 * @name Defines: errq_clc
 * Lists of error messages CLC definitions
 * to hide CLC choice.
 */
// @{
#define errq_create(o,k,f,a)		dll_create(o,k,f)
#define errq_destroy(h)			dll_destroy(h)
#define errq_insert(h,o)		dll_insert(h,o)
#define errq_insert_uniq(h,n,o)		dll_insert_uniq(h,n,o)
#define errq_append(h,o)		dll_append(h,o)
#define errq_append_uniq(h,n,o)		dll_append_uniq(h,n,o)
#define errq_search(h,k)		dll_search(h,k)
#define errq_delete(h,o)		dll_delete(h,o)
#define errq_minimum(h)			dll_minimum(h)
#define errq_maximum(h)			dll_maximum(h)
#define errq_successor(h,o)		dll_successor(h,o)
#define errq_predecessor(h,o)		dll_predecessor(h,o)
#define errq_iterate(h,d)		dll_iterate(h,d)
#define errq_nextobj(h,i)		dll_nextobj(h,i)
#define errq_iterate_done(h,i)		dll_iterate_done(h,i)
#define errq_error_reason(h,i)		dll_error_reason(h,i)
// @}



static struct bk_error_node *bk_error_marksearch(bk_s B, struct bk_error *beinfo, const char *mark, bk_flags flags);
static char bk_error_sysloglevel_char(int sysloglevel);
static void be_error_output(bk_s B, FILE *fh, int sysloglevel, struct bk_error_node *node, bk_flags flags);
#define BE_ERROR_SYSLOG_WANT_FULL  1		///< Want long-style syslog messages with timestamps, hostnames over and above what syslog gives



/**
 * Initialize the error queue and structure.
 *	@param B BAKA thread/global state 
 *	@param queuelen The number of messages that will be kept in the each (high and low priority message) queue
 *	@param fh The stdio file handle to print error messages to when errors occur (typically for debugging)
 *	@param syslogthreshhold The syslog level which high priority error messages will be logged at (BK_ERR_NONE to disable logging)
 *	@param flags Flags for future expansion--saved through run structure.
 *	@return <i>NULL</i> on call failure, allocation failure, or other fatal error.
 *	@return <br><i>Error structure</i> if successful, which has been initialized.
 */
struct bk_error *bk_error_init(bk_s B, u_int16_t queuelen, FILE *fh, int syslogthreshhold, bk_flags flags)
{
  struct bk_error *beinfo;

  if (!(beinfo = malloc(sizeof(*beinfo))))
  {
    if (fh)
      fprintf(fh, __FUNCTION__ ": Could not allocate storage for error queue structure: %s\n",strerror(errno));
    return(NULL);
  }

  beinfo->be_fh = fh;
  beinfo->be_seqnum = 0;
  beinfo->be_hilo_pivot = BK_ERROR_PIVOT;
  beinfo->be_sysloglevel = syslogthreshhold;
  beinfo->be_curHiSize = 0;
  beinfo->be_curLowSize = 0;
  beinfo->be_maxsize = queuelen;

  if (!(beinfo->be_markqueue = errq_create(NULL, NULL, DICT_UNORDERED, NULL)))
  {
    if (fh)
      fprintf(fh, __FUNCTION__ ": Could not create mark error queue: %s\n",errq_error_reason(NULL, NULL));
    goto error;
  }

  if (!(beinfo->be_hiqueue = errq_create(NULL, NULL, DICT_UNORDERED, NULL)))
  {
    if (fh)
      fprintf(fh, __FUNCTION__ ": Could not create hi error queue: %s\n",errq_error_reason(NULL, NULL));
    goto error;
  }

  if (!(beinfo->be_lowqueue = errq_create(NULL, NULL, DICT_UNORDERED, NULL)))
  {
    if (fh)
      fprintf(fh, __FUNCTION__ ": Could not create low error queue: %s\n",errq_error_reason(NULL, NULL));
    goto error;
  }

  return(beinfo);

 error:
  bk_error_destroy(B, beinfo);
  return(NULL);
}



/**
 * Get rid of the error queues and all other information.
 *	@param B BAKA thread/global state 
 *	@param beinfo The error state structure.
 */
void bk_error_destroy(bk_s B, struct bk_error *beinfo)
{
  struct bk_error_node *node;

  if (!beinfo)
  {
    /* <KLUDGE>invalid argument</KLUDGE> */
    return;
  }

  DICT_NUKE_CONTENTS(beinfo->be_markqueue, errq, node, break, if (node->ben_msg) free(node->ben_msg); free(node));
  errq_destroy(beinfo->be_markqueue);

  DICT_NUKE_CONTENTS(beinfo->be_hiqueue, errq, node, break, if (node->ben_msg) free(node->ben_msg); free(node));
  errq_destroy(beinfo->be_hiqueue);

  DICT_NUKE_CONTENTS(beinfo->be_lowqueue, errq, node, break, if (node->ben_msg) free(node->ben_msg); free(node));
  errq_destroy(beinfo->be_lowqueue);

  free(beinfo);

  return;
}



/**
 * Allow modification of configuration parameters.
 *	@param B BAKA thread/global state 
 *	@param beinfo The error state structure. 
 *	@param queuelen The number of messages that will be kept in the each (high and low priority message) queue
 *	@param fh The stdio file handle to print error messages to when errors occur (typically for debugging)
 *	@param syslogthreshhold The syslog level which high priority error messages will be logged at (NONE to disable logging)
 *	@param hilo_pivot The BK_ERR level which seperates messages into the high and low error queues (default BK_ERR_ERR which means BK_ERR_WARN messages and less important go into the low priority queue).
 *	@param flags Controls which of the previous configuration values are set (BK_ERROR_(FH, HILO_PIVOT, SYSLOGTHRESHHOLD, QUEUELEN))
*/
void bk_error_config(bk_s B, struct bk_error *beinfo, u_int16_t queuelen, FILE *fh, int syslogthreshhold, int hilo_pivot, bk_flags flags)
{
  if (!beinfo)
  {
    /* <KLUDGE>invalid argument</KLUDGE> */
    return;
  }

  if (BK_FLAG_ISSET(flags, BK_ERROR_CONFIG_FH))
    beinfo->be_fh = fh;
  if (BK_FLAG_ISSET(flags, BK_ERROR_CONFIG_HILO_PIVOT))
    beinfo->be_hilo_pivot = hilo_pivot;
  if (BK_FLAG_ISSET(flags, BK_ERROR_CONFIG_SYSLOGTHRESHHOLD))
    beinfo->be_sysloglevel = syslogthreshhold;
  if (BK_FLAG_ISSET(flags, BK_ERROR_CONFIG_QUEUELEN))
    beinfo->be_maxsize = queuelen;

  return;
}



/**
 * Add an error string to the error queue -- save time, buffer marked up with function
 * name and error level, and if necessary, output.
 *
 *	@param B BAKA thread/global state 
 *	@param sysloglevel The BK_ERR level of important of this message
 *	@param beinfo The error state structure. 
 *	@param buf The error string to print.
 */
void bk_error_iprint(bk_s B, int sysloglevel, struct bk_error *beinfo, char *buf)
{
  const char *funname;
  time_t curtime = time(NULL);
  struct bk_error_node *node;
  int tmp;
  int level = bk_error_sysloglevel_char(sysloglevel);
  dict_h be_queue;
  u_short *be_cursize;
  const char fmt[]="%s/%c: %s";
  
  if (!(funname = bk_fun_funname(B, 0, 0)))
  {
    /* <KLUDGE>Cannot determine function name</KLUDGE> */
    funname = "?";
  }

  if (!(node = malloc(sizeof(*node))))
  {
    /* <KLUDGE>cannot allocate storage for error node</KLUDGE> */
    return;
  }
  node->ben_time = curtime;
  node->ben_seq = beinfo->be_seqnum++;
  node->ben_level = sysloglevel;

  tmp = strlen(funname) + strlen(buf) + sizeof(fmt);
  if (!(node->ben_msg = malloc(tmp)))
  {
    /* <KLUDGE>cannot allocate storage for error message</KLUDGE> */
    goto error;
  }
  snprintf(node->ben_msg, tmp, fmt,funname, level, buf);
  

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
    /* <KLUDGE>CLC insert failed for some reason or another</KLUDGE> */
    goto error;
  }
  (*be_cursize)++;

  if ((sysloglevel <= beinfo->be_hilo_pivot && sysloglevel != BK_ERR_NONE) && beinfo->be_fh)
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



/**
 * Add a varags printf style buffer to the error queue in the style of bk_error_iprint.
 *	@param B BAKA thread/global state 
 *	@param sysloglevel The BK_ERR level of important of this message
 *	@param beinfo The error state structure. 
 *	@param format The error string to interpret in printf style.
 *	@param ... printf style arguments
 */
void bk_error_iprintf(bk_s B, int sysloglevel, struct bk_error *beinfo, char *format, ...)
{
  va_list args;
  char buf[MAXERRORLINE];

  if (!beinfo || !format)
  {
    /* <KLUDGE>Invalid argument</KLUDGE> */
    return;
  }

  va_start(args, format);
  vsnprintf(buf,sizeof(buf),format,args);
  va_end(args);

  bk_error_iprint(B, sysloglevel, beinfo, buf);

  return;
}



/**
 * Convert a chunk of raw data into printable string form, and call it an error.  There may be more space-efficient
 * ways of doing this for pure-ascii data with a (potentially non-null termianted) length.
 *
 *	@param B BAKA thread/global state 
 *	@param sysloglevel The BK_ERR level of important of this message
 *	@param beinfo The error state structure. 
 *	@param intro The description of the buffer or error that occured.
 *	@param prefix The leading characters in front of each line of error buffer output
 *	@param buf The raw data to be converted.
 */
void bk_error_iprintbuf(bk_s B, int sysloglevel, struct bk_error *beinfo, char *intro, char *prefix, bk_vptr *buf)
{
  char *out;

  if (!(out = bk_string_printbuf(B, intro, prefix, buf, 0)))
  {
    /* <KLUDGE>Could not convert buffer for debug printing</KLUDGE> */
    return;
  }

  bk_error_iprintf(B, sysloglevel, beinfo, out);
  free(out);

  return;
}



/**
 * Add an error string, in printf style, to the error queue.
 *	@param B BAKA thread/global state 
 *	@param sysloglevel The BK_ERR level of important of this message
 *	@param beinfo The error state structure. 
 *	@param format The error string to print in printf style.
 *	@param ap The printf arguments.
 */
void bk_error_ivprintf(bk_s B, int sysloglevel, struct bk_error *beinfo, char *format, va_list ap)
{
  char buf[MAXERRORLINE];

  if (!beinfo || !format)
  {
    /* <KLUDGE>Invalid argument</KLUDGE> */
    return;
  }
    
  vsnprintf(buf,sizeof(buf),format,ap);
  bk_error_iprint(B, sysloglevel, beinfo, buf);

  return;
}



/**
 * Flush (empty) high and low error queues.  If a mark is supplied,
 * clear all entries made *after* the mark, including the mark.
 *	@param B BAKA thread/global state 
 *	@param beinfo The error state structure. 
 *	@param mark The constant pointer/key which represents a location in the error queue: only newer messages will be flushed.
 *	@param flags Future expansion
 */
void bk_error_iflush(bk_s B, struct bk_error *beinfo, const char *mark, bk_flags flags)
{
  struct bk_error_node *node, pode, *mode, *tode;
  dict_iter iter;
  dict_h queues[] = { beinfo->be_markqueue, beinfo->be_hiqueue, beinfo->be_lowqueue, NULL };
  dict_h *curq;

  tode = NULL;
  mode = NULL;

  if (mark)
  {
    if (!(tode = bk_error_marksearch(B, beinfo, mark, flags)))
    {
      // <KLUDGE>We cannot do anything, but cannot tell anyone about it</KLUDGE>
      return;
    }
  }

  // If we have an existing mark, save off the information
  if (tode)
  {
    pode = *tode;
    mode = &pode;
  }

  // Flush all three queues
  for (curq = queues; *curq; curq++)
  {
    tode = mode;

    // Iterate over queue
    if (iter = errq_iterate(*curq, DICT_FROM_START))
    {
      while (node = errq_nextobj(beinfo->be_markqueue, iter))
      {

	// Tode is set if we have a mark we need to filter for and have not see it before
	if (tode)
	{
	  if (tode->ben_time < node->ben_time ||
	      tode->ben_time == node->ben_time && (((int)(tode->ben_seq - node->ben_seq)) <= 0))
	  {
	    tode = NULL;			// We have found the mark location--flush from here on out
	  }
	  else
	  {
	    continue;
	  }
	}

	if (node->ben_msg)
	  free(node->ben_msg);
	errq_delete(beinfo->be_markqueue, node);
	free(node);
      }
      errq_iterate_done(*curq, iter);
    }
  }

  return;
}



/**
 * Mark a position in the error queues for future reference.  The mark
 * is a constant pointer--the same value will be used for any other
 * mark usage--the pointer is used for comparison purposes, NOT the
 * data it is pointed to (e.g. save the pointer externally in a
 * variable instead of using "FOO" or strdup("FOO")).  This allows you
 * to only see "recent" errors.
 *
 * Note the mark is not bounded in size--you should clear.  This may have
 * to change in the threaded environment.
 *
 *	@param B BAKA thread/global state 
 *	@param beinfo The error state structure. 
 *	@param mark The constant pointer which represents a location in the error queue.
 *	@param flags Future expansion
 */
void bk_error_imark(bk_s B, struct bk_error *beinfo, const char *mark, bk_flags flags)
{
  struct bk_error_node *node;

  bk_error_iclear(B, beinfo, mark, flags);

  if (!(node = malloc(sizeof(*node))))
  {
    // <KLUDGE>perror("malloc")</KLUDGE>
    return;
  }
  node->ben_time = time(NULL);
  node->ben_seq = beinfo->be_seqnum++;
  node->ben_level = 0;
  node->ben_msg = (char *)mark;

  if (errq_insert(beinfo->be_markqueue, node) != DICT_OK)
  {
    // <KLUDGE>perror("insert")</KLUDGE>
    free(node);
    return;
  }

  return;
}



/**
 * Search for mark which has been placed in the mark queue.
 *
 *	@param B BAKA Thread/global state
 *	@param beinfo Error handle
 *	@param mark Constant pointer/key to search for in mark queue
 *	@param flags Fun for the future
 *	@return <i>NULL</i> if mark could not be found
 *	@return <br><i>node</i> giving mark "location" information representing the first occurance of the mark
 */
static struct bk_error_node *bk_error_marksearch(bk_s B, struct bk_error *beinfo, const char *mark, bk_flags flags)
{
  struct bk_error_node *node;

  // Search for the mark the hard way (using manual pointer comparison)
  for (node = errq_minimum(beinfo->be_markqueue);node;node = errq_successor(beinfo->be_markqueue, node))
    {
      if (node->ben_msg == mark)
      {
	return(node);
      }
    }

  return(NULL);
}



/**
 * Clear an existing mark in the error queues.
 *
 *	@param B BAKA thread/global state 
 *	@param beinfo The error state structure. 
 *	@param mark The constant pointer/key which represents a location in the error queue.
 *	@param flags Future expansion
 */
void bk_error_iclear(bk_s B, struct bk_error *beinfo, const char *mark, bk_flags flags)
{
  struct bk_error_node *node;

  if (node = bk_error_marksearch(B, beinfo, mark, flags))
  {
    errq_delete(beinfo->be_markqueue,node);
    free(node);
  }

  return;
}



/**
 * Dump (print) error queues to a file or syslog.  You may filter for
 * recent log message or log messages of a certain level of
 * importance.
 *
 *	@param B BAKA thread/global state 
 *	@param beinfo The error state structure. 
 *	@param fh The stdio file handle to print the messages to
 *	@param mark The constant pointer/key which represents a location in the error queue: only newer messages will be printed.
 *	@param minimumlevel The minimum BK_ERR level for which to output error messages (BK_ERR_NONE will output all levels)
 *	@param sysloglevel If not BK_ERR_NONE, the system log level at which to log these messages.
 *	@param flags Future expansion
 */
void bk_error_idump(bk_s B, struct bk_error *beinfo, FILE *fh, char *mark, int minimumlevel, int sysloglevel, bk_flags flags)
{
  struct bk_error_node *hi, *lo, *cur, *marknode;

  if (!beinfo)
    return;

  marknode = NULL;

  if (mark)
  {
    if (!(marknode = bk_error_marksearch(B, beinfo, mark, flags)))
    {
      // <KLUDGE>We cannot do anything, but cannot tell anyone about it</KLUDGE>
      return;
    }
  }

  hi = errq_maximum(beinfo->be_hiqueue);
  lo = errq_maximum(beinfo->be_lowqueue);

  // Print the queues in FIFO order, interlacing the low and hi queue messages as appropriate
  while (hi || lo)
  {
    int wanthi;					// Do I want to output the message from the hi queue or the low queue next

    if (!hi || !lo)
      wanthi = !lo;
    else if ((wanthi = lo->ben_time - hi->ben_time) == 0)
      wanthi = ((int)(lo->ben_seq - hi->ben_seq));

    if (wanthi > 0)
    {
      cur = hi;
      hi = errq_predecessor(beinfo->be_hiqueue, hi);
    }
    else
    {
      cur = lo;
      lo = errq_predecessor(beinfo->be_lowqueue, lo);
    }

    if (marknode)
    {
      if (cur->ben_time < marknode->ben_time ||
	  (cur->ben_time == marknode->ben_time && ((int)(cur->ben_seq - marknode->ben_seq)) < 0))
	continue;					// We yet to reach the mark
    }

    // <WARNING>Assumes syslog levels are higher priority->lower numbers</WARNING>
    if (minimumlevel >= 0 && minimumlevel > cur->ben_level)
      continue;

    be_error_output(B, fh, sysloglevel, cur, 0);
  }
}



/**
 * Syslog level to printable character conversions.
 *
 * Convert the BK_ERR levels into printable character for automated filtering priority.
 *
 *	@param sysloglevel The BK_ERR level we wish a character for
 *	@return <i>character</i> representing the error level--'?' if unknown
 */
static char bk_error_sysloglevel_char(int sysloglevel)
{
  switch(sysloglevel)
  {
  case BK_ERR_CRIT: return('1');
  case BK_ERR_ERR: return('2');
  case BK_ERR_WARN: return('3');
  case BK_ERR_NOTICE: return('4');
  case BK_ERR_DEBUG: return('5');
  default: return('?');
  }
}



/**
 * Output a error node to file handle or syslog.
 *
 *	@param B BAKA Thread/global state
 *	@param fh The file handle to output the error node to (NULL to disable)
 *	@param sysloglevel The system log level to log the error message at (BK_ERR_NONE to disable)
 *	@param node The error node--message, time, etc to log
 *	@param flags Control whether you want FULL or abbreviated (default) logging
 */
static void be_error_output(bk_s B, FILE *fh, int sysloglevel, struct bk_error_node *node, bk_flags flags)
{
  int tmp;
  char timeprefix[40];
  char fullprefix[40];
  struct tm *tm = localtime(&node->ben_time);

  if ((tmp = strftime(timeprefix, sizeof(timeprefix), "%Y-%m-%d %H:%M:%S", tm)) != 19)
  {
#if 0
    /*
     * XXX - this way lies madness (and infinite recursion).  A better
     * solution is to use assert.
     */
    bk_error_printf(B, BK_ERR_ERR, __FUNCTION__ ": Somehow strftime produced %d bytes instead of the expected 14\n",tmp);
#endif
    return;
  }

  if (BK_GENERAL_PROGRAM(B))
    snprintf(fullprefix, sizeof(fullprefix), "%s %s[%d]", timeprefix, (char *)BK_GENERAL_PROGRAM(B), getpid());
  else
    snprintf(fullprefix, sizeof(fullprefix), "%s", timeprefix);
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
