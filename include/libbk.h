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

#ifndef _baka_h_
#define _baka_h_
#include "baka_include.h"
#include "baka_oscompat.h"



typedef u_int32_t bakaflags;

/* General vectored pointer */
struct bakavptr
{
  void *ptr;					/* Data */
  u_int32_t len;				/* Length */
};

/* General vectored pointer */
struct baka_alloc_ptr
{
  void *ptr;					/* Data */
  u_int32_t cur;				/* Current length */
  u_int32_t max;				/* Maximum length */
};


/* General baka global information */
struct __bakageneral
{
  struct baka_debug bg_debug;			/* Debug info */
  struct baka_error bg_error;			/* Error info */
  dict_h	bg_funstack;			/* Function stack */
  char		*bg_program;			/* Program name */
  bakaflags	bg_flags;			/* Flags */
#define BAKA_BGFLAGS_FUNON	1		/* Is function tracing on? */
#define BAKA_BGFLAGS_DEBUGON	1		/* Is debugging on? */
} __baka =
{
  { NULL, 0, 0 },				/* bd_debug */
  { NULL, 0, NULL, 0, 0, 0 },			/* bd_error */
  NULL,						/* bd_funstack */
  NULL,						/* bd_program */
  1,						/* bg_flags */
};



/* debug.c */
#define sos_debug_and(__



/* fun.c */
#define BAKA_ENTRY(fun, pkg) baka_funinfo __baka_funinfo = (__baka.bg_flags & BAKA_BGFLAGS_FUNON) ? NULL : baka_fun_entry(fun, pkg)
#define BAKA_RETURN(retval) \
	do { \
	  typeof(retval) myretval = (retval); \
	  int save_errno = errno; \
	  if (!(__baka.bg_flags & BAKA_BGFLAGS_FUNON)) \
	    baka_fun_exit(__baka_funinfo); \
	  errno = save_errno; \
	  return myretval; \
	  /* NOTREACHED */ \
	} while (0)
#define BAKA_VRETURN() \
	do { \
	  int save_errno = errno; \
	  if (!(__baka.bg_flags & BAKA_BGFLAGS_FUNON)) \
	    baka_fun_exit(__baka_funinfo); \
	  errno = save_errno; \
	  return; \
	  /* NOTREACHED */ \
	} while (0)



/* debug.c */
struct baka_debug
{
  FILE		*bd_fh;				/* Debugging info file handle */
  u_char	bd_sysloglevel;			/* Debugging syslog level */
  bakaflags	bd_flags;			/* Flags */
};



/* error.c */
struct baka_error
{
  FILE		*be_fh;				/* Debugging info file handle */
  u_char	be_sysloglevel;			/* Debugging syslog level */
  dict_h	be_queue;			/* Queue of error messages */
  u_short	be_cursize;			/* Current queue size */
  u_short	be_maxsize;			/* Maximum queue size */
  bakaflags	be_flags;			/* Flags */
};



/* fun.c */
typedef struct _baka_funinfo
{
  const char *bf_funname;
  const char *bf_pkgname;
  u_int32_t bf_debuglevel;
};



/* debug.c */
extern u_int32_t baka_debug_query(struct baka_debug *bdinfo, const char *funname, const char *pkgname, const char *program);
extern int baka_debug_set(struct baka_debug *bdinfo, const char *name, u_int32_t level);
extern int baka_debug_setconfig(struct baka_debug *bdinfo, baka_config_t config, const char *program);
extern void baka_debug_config(struct baka_debug *bdinfo, u_int16_t queuelen, FILE *fh, u_int8_t sysloglevel, bakaflags flags);
extern void baka_debug_iprint(struct baka_debug *bdinfo, char *buf);
extern void baka_debug_iprintf(struct baka_debug *bdinfo, char *format, ...);
extern void baka_debug_iprintbuf(struct baka_debug *bdinfo, struct bakavptr *buf);
extern void baka_debug_ivprintf(struct baka_debug *bdinfo, char *format, va_list ap);



/* error.c */
extern u_int32_t baka_error_query(struct baka_error *beinfo, const char *funname, const char *pkgname, const char *program);
extern int baka_error_set(struct baka_error *beinfo, const char *name, u_int32_t level);
extern int baka_error_setconfig(struct baka_error *beinfo, baka_config_t config, const char *program);
extern void baka_error_config(struct baka_error *beinfo, u_int16_t queuelen, FILE *fh, u_int8_t sysloglevel, bakaflags flags);
extern void baka_error_iprint(struct baka_error *beinfo, char *buf);
extern void baka_error_iprintf(struct baka_error *beinfo, char *format, ...);
extern void baka_error_iprintbuf(struct baka_error *beinfo, struct bakavptr *buf);
extern void baka_error_ivprintf(struct baka_error *beinfo, char *format, va_list ap);



/* fun.c */
extern baka_funinfo baka_fun_entry(const char *func, const char *package);
extern void baka_fun_exit(baka_funinfo fh);
extern void baka_fun_reentry(void);
extern void baka_fun_trace(FILE *out, u_int8_t sysloglevel, bakaflags flags);
extern void baka_fun_set(bakaflags flags);
#define BAKA_FUN_OFF	0			/* Turn off function tracing */
#define BAKA_FUN_ON	1			/* Turn on function tracing */


#endif /* _baka_h_ */
