#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_error.c,v 1.22 2002/06/19 21:39:23 dupuy Exp $";
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
  const char    *ben_origmsg;			///< Message without fun name
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
static void be_error_append(bk_s B, bk_alloc_ptr *str, struct bk_error_node *node, bk_flags flags);



/**
 * Initialize the error queue and structure.
 *	@param B BAKA thread/global state 
 *	@param queuelen The number of messages that will be kept in the each (high and low priority message) queue
 *	@param fh The stdio file handle to print error messages to when errors occur (typically for debugging)
 *	@param syslogthreshold The syslog level which high priority error messages will be logged at (BK_ERR_NONE to disable logging)
 *	@param flags Error flags: BK_ERROR_FLAG_SYSLOG_FULL to duplicate
 *	timestamp and program information in syslogged errors,
 *	BK_ERROR_FLAG_BRIEF to omit timestamp and program information in
 *	non-syslogged errors, BK_ERROR_FLAG_NO_FUN to only include original
 *	error message (no fun info).
 *	@return <i>NULL</i> on call failure, allocation failure, or other fatal error.
 *	@return <br><i>Error structure</i> if successful, which has been initialized.
 */
struct bk_error *bk_error_init(bk_s B, u_int16_t queuelen, FILE *fh, int syslogthreshold, bk_flags flags)
{
  struct bk_error *beinfo;

  if (!(beinfo = malloc(sizeof(*beinfo))))
  {
    if (fh)
      fprintf(fh, __FUNCTION__ ": Could not allocate storage for error queue structure: %s\n", strerror(errno));
    return(NULL);
  }

  beinfo->be_fh = fh;
  beinfo->be_seqnum = 0;
  beinfo->be_hilo_pivot = BK_ERROR_PIVOT;
  beinfo->be_sysloglevel = syslogthreshold;
  beinfo->be_curHiSize = 0;
  beinfo->be_curLowSize = 0;
  beinfo->be_maxsize = queuelen;
  beinfo->be_flags = flags;

  if (!(beinfo->be_markqueue = errq_create(NULL, NULL, DICT_UNORDERED, NULL)))
  {
    if (fh)
      fprintf(fh, __FUNCTION__ ": Could not create mark error queue: %s\n", errq_error_reason(NULL, NULL));
    goto error;
  }

  if (!(beinfo->be_hiqueue = errq_create(NULL, NULL, DICT_UNORDERED, NULL)))
  {
    if (fh)
      fprintf(fh, __FUNCTION__ ": Could not create hi error queue: %s\n", errq_error_reason(NULL, NULL));
    goto error;
  }

  if (!(beinfo->be_lowqueue = errq_create(NULL, NULL, DICT_UNORDERED, NULL)))
  {
    if (fh)
      fprintf(fh, __FUNCTION__ ": Could not create low error queue: %s\n", errq_error_reason(NULL, NULL));
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
}



/**
 * Allow modification of configuration parameters.
 *	@param B BAKA thread/global state 
 *	@param beinfo The error state structure. 
 *	@param queuelen The number of messages that will be kept in the each (high and low priority message) queue
 *	@param fh The stdio file handle to print error messages to when errors occur (typically for debugging)
 *	@param syslogthreshold The syslog level which high priority error messages will be logged at (NONE to disable logging)
 *	@param hilo_pivot The BK_ERR level which seperates messages into the high and low error queues (default BK_ERR_ERR which means BK_ERR_WARN messages and less important go into the low priority queue).
 *	@param flags Controls which of the previous configuration values are
 *	set (BK_ERROR_(FH, HILO_PIVOT, SYSLOGTHRESHOLD, QUEUELEN, FLAGS)).
 *	Combined with error flags: BK_ERROR_FLAG_SYSLOG_FULL to duplicate
 *	timestamp and program information in syslogged errors,
 *	BK_ERROR_FLAG_BRIEF to omit timestamp and program information in
 *	non-syslogged errors, BK_ERROR_FLAG_NO_FUN to only include original
 *	error message (no fun info).
*/
void bk_error_config(bk_s B, struct bk_error *beinfo, u_int16_t queuelen, FILE *fh, int syslogthreshold, int hilo_pivot, bk_flags flags)
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
  if (BK_FLAG_ISSET(flags, BK_ERROR_CONFIG_SYSLOGTHRESHOLD))
    beinfo->be_sysloglevel = syslogthreshold;
  if (BK_FLAG_ISSET(flags, BK_ERROR_CONFIG_QUEUELEN))
    beinfo->be_maxsize = queuelen;
  if (BK_FLAG_ISSET(flags, BK_ERROR_CONFIG_FLAGS))
    beinfo->be_flags = flags &
      (BK_ERROR_FLAG_SYSLOG_FULL|BK_ERROR_FLAG_BRIEF|BK_ERROR_FLAG_NO_FUN);
}



/**
 * Add an error string to the error queue -- timestamp, buffer marked up with
 * function name and error level, and if necessary, output.
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
  const char *level = bk_general_errorstr(B, sysloglevel);
  dict_h be_queue;
  u_short *be_cursize;
  const char fmt[]="%s/%n%s: %s";
  int len = -8;					// -(total %. chars in fmt)
  int origoffset;
  
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

  len += strlen(funname) + strlen(buf) + strlen(level) + sizeof(fmt);
  if (!(node->ben_msg = malloc(len)))
  {
    /* <KLUDGE>cannot allocate storage for error message</KLUDGE> */
    goto error;
  }
  snprintf(node->ben_msg, len, fmt, funname, &origoffset, level, buf);
  node->ben_origmsg = &node->ben_msg[origoffset];

  // <TRICKY>Presumes smaller syslog levels are higher priority</TRICKY>
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

  // <TRICKY>Presumes smaller syslog levels are higher priority</TRICKY>
  if (sysloglevel <= beinfo->be_hilo_pivot && (sysloglevel != BK_ERR_NONE || beinfo->be_fh))
    be_error_output(B, beinfo->be_fh, beinfo->be_sysloglevel, node, beinfo->be_flags);

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
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);

  bk_error_iprint(B, sysloglevel, beinfo, buf);
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
    
  vsnprintf(buf, sizeof(buf), format, ap);
  bk_error_iprint(B, sysloglevel, beinfo, buf);
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
    if ((iter = errq_iterate(*curq, DICT_FROM_START)))
    {
      while ((node = errq_nextobj(beinfo->be_markqueue, iter)))
      {

	// Tode is set if we have a mark we need to filter for and have not see it before
	if (tode)
	{
	  if (tode->ben_time < node->ben_time ||
	      (tode->ben_time == node->ben_time && (int)(tode->ben_seq - node->ben_seq) <= 0))
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
	errq_delete(*curq, node);
	free(node);

	// decrement counter
	if (*curq == beinfo->be_hiqueue)
	{
	  beinfo->be_curHiSize--;
	}
	else if (*curq == beinfo->be_lowqueue)
	{
	  beinfo->be_curLowSize--;
	}
      }
      errq_iterate_done(*curq, iter);
    }
  }
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

  if ((node = bk_error_marksearch(B, beinfo, mark, flags)))
  {
    errq_delete(beinfo->be_markqueue, node);
    free(node);
  }
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
 *	@param flags Error flags: BK_ERROR_FLAG_SYSLOG_FULL to duplicate
 *	timestamp and program information in syslogged errors,
 *	BK_ERROR_FLAG_BRIEF to omit timestamp and program information in
 *	non-syslogged errors, BK_ERROR_FLAG_NO_FUN to only include original
 *	error message (no fun info).
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

    // <TRICKY>Presumes smaller syslog levels are higher priority</TRICKY>
    if (minimumlevel >= 0 && cur->ben_level > minimumlevel)
      continue;

    be_error_output(B, fh, sysloglevel, cur, flags);
  }
}



/**
 * Dump (print) error queues to string.  You may filter for
 * recent log message or log messages of a certain level of
 * importance.  Caller must free returned string.
 *
 *	@param B BAKA thread/global state 
 *	@param beinfo The error state structure. 
 *	@param mark The constant pointer/key which represents a location in the error queue: only newer messages will be printed.
 *	@param minimumlevel The minimum BK_ERR level for which to output error messages (BK_ERR_NONE will output all levels)
 *	@param flags Error flags: BK_ERROR_FLAG_SYSLOG_FULL to duplicate
 *	timestamp and program information in syslogged errors,
 *	BK_ERROR_FLAG_BRIEF to omit timestamp and program information in
 *	non-syslogged errors, BK_ERROR_FLAG_NO_FUN to only include original
 *	error message (no fun info).
 *	@return <i>malloc'd string</i> on success (caller must free)<br><i>NULL</i> on error
 */
char *bk_error_istrdump(bk_s B, struct bk_error *beinfo, char *mark, int minimumlevel, bk_flags flags)
{
  struct bk_error_node *hi, *lo, *cur, *marknode;
  struct bk_alloc_ptr *str = NULL;
  char *out;

  if (!beinfo)
    goto error;

  marknode = NULL;

  if (mark)
  {
    if (!(marknode = bk_error_marksearch(B, beinfo, mark, flags)))
    {
      // <KLUDGE>We cannot do anything, but cannot tell anyone about it</KLUDGE>
      goto error;
    }
  }

  hi = errq_maximum(beinfo->be_hiqueue);
  lo = errq_maximum(beinfo->be_lowqueue);

  if (!BK_MALLOC(str))
  {
    goto error;
  }

  if (!BK_MALLOC_LEN(str->ptr, 1024))
  {
    goto error;
  }
  ((char*) str->ptr)[0] = '\0';
  str->cur = 1;
  str->max = 1024;

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

    // <TRICKY>Presumes smaller syslog levels are higher priority</TRICKY>
    if (minimumlevel >= 0 && (cur->ben_level > minimumlevel))
      continue;

    be_error_append(B, str, cur, flags);
  }

  out = str->ptr;
  free(str);
  str = NULL;

  return out;

 error:
  if (str)
  {
    if (str->ptr)
    {
      free(str->ptr);
    }
    free(str);
  }

  return NULL;
}



/**
 * Translate error time into formatted string
 *
 *	@param node The error node
 *	@param timestr [out] char buffer (must be at least 20 bytes long)
 *	@param size of time
 */
static void be_error_time(struct bk_error_node *node, char *timestr, size_t max)
{
  int tmp;
  struct tm *tm;

  timestr[0] = '\0';				// don't leave trash in string

  if (node)
  {
    tm = localtime(&node->ben_time);

    if ((tmp = strftime(timestr, max, "%Y-%m-%d %H:%M:%S ", tm)) != 20)
      timestr[0] = '\0';			// timestamp hosed; nuke it
  }
}



/**
 * Output a error node to file handle or syslog.
 *
 *	@param B BAKA Thread/global state
 *	@param fh The file handle to output the error node to (NULL to disable)
 *	@param sysloglevel The system log level to log the error message at (BK_ERR_NONE to disable)
 *	@param node The error node--message, time, etc to log
 *	@param flags Error flags: BK_ERROR_FLAG_SYSLOG_FULL to duplicate
 *	timestamp and program information in syslogged errors,
 *	BK_ERROR_FLAG_BRIEF to omit timestamp and program information in
 *	non-syslogged errors, BK_ERROR_FLAG_NO_FUN to only include original
 *	error message (no fun info).
 */
static void be_error_output(bk_s B, FILE *fh, int sysloglevel, struct bk_error_node *node, bk_flags flags)
{
  char timeprefix[40];
  char fullprefix[40];
  const char *msg;
  int syslogflags = BK_SYSLOG_FLAG_NOFUN|BK_SYSLOG_FLAG_NOLEVEL;

  if (!node)
    return;

  if (BK_FLAG_ISSET(flags, BK_ERROR_FLAG_NO_FUN))
    msg = node->ben_origmsg;
  else
    msg = node->ben_msg;
  
  if (!msg)
    return;

  be_error_time(node, timeprefix, 40);

  if (BK_GENERAL_PROGRAM(B))
    snprintf(fullprefix, sizeof(fullprefix), "%s%s[%d]", timeprefix, (char *)BK_GENERAL_PROGRAM(B), getpid());
  else
    snprintf(fullprefix, sizeof(fullprefix), "%s[%d]", timeprefix, getpid());

  fullprefix[sizeof(fullprefix)-1] = 0;		// Ensure terminating NUL

 if (sysloglevel != BK_ERR_NONE)
  {
    if (BK_FLAG_ISSET(flags, BK_ERROR_FLAG_SYSLOG_FULL))
      bk_general_syslog(B, sysloglevel, syslogflags, "%s: %s",fullprefix, msg);
    else
      bk_general_syslog(B, sysloglevel, syslogflags, "%s", msg);
  }

  if (fh)
  {
    if (BK_FLAG_ISSET(flags, BK_ERROR_FLAG_BRIEF))
    {
      fprintf(fh, "%s", msg);
    }
    else
    {
      fprintf(fh, "%s: %s", fullprefix, msg);
    }
  }

  return;
}



/**
 * Append error to a bk_alloc_ptr
 *	@param B BAKA Thread/global state
 *	@param str extensible string to which to append
 *	@param node The error node--message, time, etc to log
 *	@param flags Error flags: BK_ERROR_FLAG_BRIEF to omit timestamp and
 *	program information in errors, BK_ERROR_FLAG_NO_FUN to only include
 *	original error message (no fun info).
 */
static void be_error_append(bk_s B, bk_alloc_ptr *str, struct bk_error_node *node, bk_flags flags)
{
  char timeprefix[40];
  char fullprefix[40];
  const char *msg;
  u_int32_t addedlength;

  if (!str || !node)
    return;

  if (BK_FLAG_ISSET(flags, BK_ERROR_FLAG_NO_FUN))
    msg = node->ben_origmsg;
  else
    msg = node->ben_msg;
  
  if (!msg)
    return;

  be_error_time(node, timeprefix, 40);

  if (BK_GENERAL_PROGRAM(B))
    snprintf(fullprefix, sizeof(fullprefix), "%s%s[%d]", timeprefix, (char *)BK_GENERAL_PROGRAM(B), getpid());
  else
    snprintf(fullprefix, sizeof(fullprefix), "%s", timeprefix);

  fullprefix[sizeof(fullprefix)-1] = 0;		// Ensure terminating NUL

  addedlength = strlen(msg);

  if (!BK_FLAG_ISSET(flags, BK_ERROR_FLAG_BRIEF))
    addedlength += strlen(fullprefix) + 1;

  while ((str->max - str->cur) < addedlength + 1)
  {
    char *tmp;
    const int blocksize = 2048;

    tmp = realloc(str->ptr, str->max + blocksize);
    if (!tmp)
    {
      return;
    }
    str->ptr = tmp;
    str->max += blocksize;
  }

  if (str->cur > 1)
  {
    strcpy((char*) str->ptr + str->cur - 1, "\n");
    str->cur++;
  }

  if (BK_FLAG_ISSET(flags, BK_ERROR_FLAG_BRIEF))
    sprintf((char*) str->ptr + str->cur - 1, "%s", msg);
  else
    sprintf((char*) str->ptr + str->cur - 1, "%s %s", fullprefix, msg);

  str->cur += addedlength;
}
