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



#endif /* _baka_h_ */
