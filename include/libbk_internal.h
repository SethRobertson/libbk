/*
 * $Id: libbk_internal.h,v 1.15 2001/11/05 20:53:06 seth Exp $
 *
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

#ifndef _libbk_internal_h_
#define _libbk_internal_h_


/* b_general.c */
struct bk_proctitle
{
  int		bp_argc;			/* Number of program arguments */
  char		**bp_argv;			/* Program and arguments */
  char		**bp_envp;			/* Environment */
  bk_vptr	bp_title;			/* Original vector for overwriting */
  bk_flags	bp_flags;			/* Flags */
#define BK_PROCTITLE_OFF	1
};


/* b_debug.c */
struct bk_debug
{
  dict_h	bd_leveldb;			/* Debugging levels by fun/grp/pkg/prg */
  u_int32_t	bd_defaultlevel;		/* Default debugging level */
  FILE		*bd_fh;				/* Debugging info file handle */
  int		bd_sysloglevel;			/* Debugging syslog level */
  bk_flags	bd_flags;			/* Flags */
};

struct bk_debugnode
{
  char		*bd_name;			/* Name to debug */
  u_int32_t	bd_level;			/* Level to debug at */
};



/* b_error.c */
struct bk_error
{
  FILE		*be_fh;				/* Error info file handle */
  u_int		be_seqnum;			/* Sequence number */
  dict_h	be_markqueue;			/* Queue of high priority error messages */
  dict_h	be_hiqueue;			/* Queue of high priority error messages */
  dict_h	be_lowqueue;			/* Queue of low priority error messages */
  char		be_hilo_pivot;			/* Pivot value */
#define BK_ERROR_PIVOT	BK_ERR_ERR
  char		be_sysloglevel;			/* Error syslog level */
  u_short	be_curHiSize;			/* Current queue size */
  u_short	be_curLowSize;			/* Current queue size */
  u_short	be_maxsize;			/* Maximum queue size */
  bk_flags	be_flags;			/* Flags */
};

struct bk_error_node
{
  time_t	ben_time;			/* Timestamp */
  u_int		ben_seq;			/* Sequence number */
  int		ben_level;			/* Level of message */
  char		*ben_msg;			/* Actual message */
};



/* b_funlist.c */
struct bk_funlist
{
  dict_h	bf_list;			/* Function list */
  bk_flags	bf_flags;			/* Flags */
};

/* Function info */
struct bk_fun
{
  void		(*bf_fun)(bk_s B, void *args, u_int aux);
  void		*bf_args;
};



/* b_memx.c */
struct bk_memx
{
  void		*bm_array;			/* Extensible memory */
  size_t	bm_unitsize;			/* Size of units */
  size_t	bm_curalloc;			/* Allocated memory */
  size_t	bm_curused;			/* Current used */
  u_int		bm_incr;			/* Increment amount */
  bk_flags	bm_flags;			/* Other info */
};


/* b_run.c */

/* Event queue structure */
struct br_equeue
{
  struct timeval	bre_when;		/* Time to run event */
  void			(*bre_event)(bk_s B, struct bk_run *run, void *opaque, struct timeval starttime, bk_flags flags); /* Event to run */
  void			*bre_opaque;		/* Data for opaque */
};

/* Cron structure */
struct br_equeuecron
{
  time_t		brec_interval;		/* usec Interval timer */
  void			(*brec_event)(bk_s B, struct bk_run *run, void *opaque, struct timeval starttime, bk_flags flags); /* Event to run */
  void			*brec_opaque;		/* Data for opaque */
  struct br_equeue	*brec_equeue;		/* Current event */
};

/* Signal handler to run */
struct br_sighandler
{
  void			(*brs_handler)(bk_s B, struct bk_run *run, int signum, void *opaque, struct timeval starttime); /* Handler */
  void			*brs_opaque;		/* Opaque data */
};

/* fd association structure */
struct bk_run_fdassoc
{
  int			brf_fd;			/* Fd we are handling */
  void		      (*brf_handler)(bk_s B, struct bk_run *run, u_int fd, u_int gottype, void *opaque, struct timeval starttime);		/* Function to handle */
  void		       *brf_opaque;		/* Opaque information */
};

/* Fundamental run information */
struct bk_run
{
  fd_set		br_readset;		/* FDs interested in this operation */
  fd_set		br_writeset;		/* FDs interested in this operation */
  fd_set		br_xcptset;		/* FDs interested in this operation */
  int			br_selectn;		/* Highest FD (+1) in fdsets */
  dict_h		br_fdassoc;		/* FD to callback association */
  dict_h		br_poll_funcs;		/* Poll functions */
  dict_h		br_ondemand_funcs;	/* On demands functions */
  dict_h		br_idle_funcs;		/* Idle tasks */
  
  pq_h			*br_equeue;		/* Event queue */
  volatile u_int8_t	br_signums[NSIG];	/* Number of signal events we hx]ave received */
  struct br_sighandler	br_handlerlist[NSIG];	/* Handlers for signals */
  bk_flags		br_flags;		/* General flags */
#define BK_RUN_FLAG_RUN_OVER		0x1	/* bk_run_run should terminate */
#define BK_RUN_FLAG_NEED_POLL		0x2	/* Execute poll list */
#define BK_RUN_FLAG_CHECK_DEMAND	0x4	/* Check demand list */
#define BK_RUN_FLAG_HAVE_IDLE		0x8	/* Run idle task */
#define BK_RUN_FLAG_NOTIFYANYWAY	0x10	/* Notify caller if handed off*/
};


/* Used for polling and idle tasks */
struct bk_run_func
{
  void *	brfn_key;
  int		(*brfn_fun)(bk_s B, struct bk_run *run, void *opaque, struct timeval starttime, struct timeval *delta, bk_flags flags);
  void *	brfn_opaque;
  bk_flags	brfn_flags;
  dict_h	brfn_backptr;
};


/* Used for on demand function */
struct bk_run_ondemand_func
{
  void *		brof_key;
  int			(*brof_fun)(bk_s B, struct bk_run *run, void *opaque, volatile int *demand, struct timeval starttime, bk_flags flags);
  void *		brof_opaque;
  volatile int *	brof_demand;
  bk_flags		brof_flags;
  dict_h		brof_backptr;
};

#endif /* _libbk_h_ */
