/*
 * $Id: libbk_internal.h,v 1.11 2001/08/27 03:10:22 seth Exp $
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


#if 0
/* b_config.h */
struct bk_config
{
  char 			*bc_filename;		/* Filename for re-read */
  dict_h		bc_keylist;		/* List of keys (bk_config_key) */
};

struct bk_config_key
{
  char			*bc_key;		/* Key for values */
  char			*bc_getnext;		/* State for getnext */
  dict_h		bc_vallist;		/* List of values */
};
#endif


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
  void			(*bre_event)(void *opaque, time_t starttime); /* Event to run */
  void			*bre_opaque;		/* Data for opaque */
};

/* Cron structure */
struct br_equeuecron
{
  u_int			brec_allones;		/* All ones for structure discrimination */
  time_t		brec_interval;		/* Interval timer */
  void			(*brec_event)(void *opaque, time_t starttime); /* Event to run */
  void			*brec_opaque;		/* Data for opaque */
  struct br_equeue	*brec_equeue;		/* Current event */
};

/* Signal handler to run */
struct br_sighandler
{
  void			(*brs_handler)(int signum, void *opaque, time_t starttime); /* Handler */
  void			*brs_opaque;		/* Opaque data */
};

/* Fundamental run information */
struct bk_run
{
  fd_set		br_readset;		/* FDs interested in this operation */
  fd_set		br_writeset;		/* FDs interested in this operation */
  fd_set		br_xcptset;		/* FDs interested in this operation */
  dict_h		br_fdassoc;		/* FD to callback association */
  volatile int		*br_ondemandtest;	/* Should on-demand function be called */
  int			(*br_ondemand)(void *opaque, volatile int *demand, time_t starttime); /* On-demand function */
  void			*br_ondemandopaque;	/* On-demand opaque */
  int			(*br_pollfun)(void *opaque, time_t starttime); /* Polling function */
  void			*br_pollopaque;		/* Polling opaque */
  struct br_equeue	*br_equeue;		/* Event queue ARRAY */
  u_int16_t		br_eq_cursize;		/* Equeue current size */
  u_int16_t		br_eq_maxsize;		/* Equeue current size */
  u_int8_t		br_signums[NSIG];	/* Number of signal events we have received */
  struct br_sighandler	br_handlerlist[NSIG];	/* Handlers for signals */
  bk_flags		br_flags;		/* General flags */
};

#endif /* _libbk_h_ */
