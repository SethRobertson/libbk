/*
 * ++Copyright BAKA++
 *
 * Copyright (c) 2001 The Authors.  All rights reserved.
 *
 * This source code is licensed to you under the terms of the file
 * LICENSE.TXT in this release for further details.
 *
 * Mail <projectbaka@baka.org> for further information
 *
 * --Copyright BAKA--
 */

#ifndef _baka_internal_h_
#define _baka_internal_h_



/* b_config.h */
struct baka_config
{
  char 			*bc_filename;		/* Filename for re-read */
  dict_h		bc_keylist;		/* List of keys (baka_config_key) */
};

struct baka_config_key
{
  char			*bc_key;		/* Key for values */
  char			*bc_getnext;		/* State for getnext */
  dict_h		bc_vallist;		/* List of values */
};



/* b_debug.c */
struct baka_debug
{
  FILE		*bd_fh;				/* Debugging info file handle */
  u_char	bd_sysloglevel;			/* Debugging syslog level */
  bakaflags	bd_flags;			/* Flags */
};



/* b_error.c */
struct baka_error
{
  FILE		*be_fh;				/* Debugging info file handle */
  u_char	be_sysloglevel;			/* Debugging syslog level */
  dict_h	be_queue;			/* Queue of error messages */
  u_short	be_cursize;			/* Current queue size */
  u_short	be_maxsize;			/* Maximum queue size */
  bakaflags	be_flags;			/* Flags */
};



/* b_funlist.c */
struct baka_funlist
{
  dict_h	bf_list;			/* Function list */
  bakaflags	bf_flags;			/* Flags */
};

/* Function info */
struct baka_fun
{
  void		(*bf_fun)(baka B, void *args, u_int aux);
  void		*bf_args;
};



/* b_run.c */
struct baka_run
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
  baka_flags		br_flags;		/* General flags */
};

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

#endif /* _baka_h_ */
