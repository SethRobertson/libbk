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


/* baka.c -- General baka global information */
struct bakageneral
{
  struct baka_debug	*bg_debug;		/* Debug info */
  struct baka_error	*bg_error;		/* Error info */
  struct baka_config	*bg_config;		/* Configuration info */
  struct baka_funlist	*bg_reinit;		/* Reinitialization list */
  char			*bg_program;		/* Program name */
  bakaflags		bg_flags;		/* Flags */
#define BAKA_BGFLAGS_FUNON	1		/* Is function tracing on? */
#define BAKA_BGFLAGS_DEBUGON	2		/* Is debugging on? */
#define BAKA_BGFLAGS_SYSLOGON	4		/* Is syslog on? */
};

/* Per-thread state information */
typedef struct __bakathread
{
  dict_h		bt_funstack;		/* Function stack */
  struct baka_funinfo	*bt_curfun;		/* Current function */
  char			*bt_threadname;		/* Function name */
  struct bakageneral	bt_general;		/* Common program state */
  bakaflags		bt_flags;
} *baka;



/* b_debug.c */
#define baka_debug_and(B,l) (((B)->bt_flags & BAKA_BGFLAGS_DEBUGON) && (B)->bt_curfun && ((B)->bt_curfun->bf_debuglevel & (l)))
#define baka_debug_vprintf(B,l,f,ap) (baka_debug_and(B,l) && sos_debug_vprintf(B,f,ap))
#define baka_debug_printf_and(B,l,f,args...) (baka_debug_and(B,l) && sos_debug_printf(B,f,##args))
#define baka_debug_printbuf_and(B,l,v) (baka_debug_and(B,l) && sos_debug_printf(B,v))
#define baka_debug_print(B,s) baka_debug_iprint(B,(B)->bt_general->bg_debug,s)
#define baka_debug_printf(B,f,args...) baka_debug_iprintf(B,(B)->bt_general->bg_debug,f,##args)
#define baka_debug_vprintf(B,f,ap) baka_debug_ivprintf(B,(B)->bt_general->bg_debug,f,ap)
#define baka_debug_printbuf(B,v) baka_debug_iprintbuf(B,(B)->bt_general->bg_debug,v)



/* b_error.c */
#define baka_error_print(B,s) baka_error_iprint(B,(B)->bt_general->bg_error,s)
#define baka_error_printf(B,f,args...) baka_error_iprintf(B,(B)->bt_general->bg_error,f,##args)
#define baka_error_vprintf(B,f,ap) baka_error_ivprintf(B,(B)->bt_general->bg_error,f,ap)
#define baka_error_printbuf(B,v) baka_error_iprintbuf(B,(B)->bt_general->bg_error,v)



/* b_fun.c */
#define BAKA_ENTRY(B, fun, pkg, grp) baka_funinfo __baka_funinfo = (__baka.bg_flags & BAKA_BGFLAGS_FUNON) ? NULL : baka_fun_entry(B, fun, pkg, grp)
#define BAKA_RETURN(B, retval) \
	do { \
	  typeof(retval) myretval = (retval); \
	  int save_errno = errno; \
	  if (!(__baka.bg_flags & BAKA_BGFLAGS_FUNON)) \
	    baka_fun_exit(B, __baka_funinfo); \
	  errno = save_errno; \
	  return myretval; \
	  /* NOTREACHED */ \
	} while (0)
#define BAKA_VRETURN(B) \
	do { \
	  int save_errno = errno; \
	  if (!(__baka.bg_flags & BAKA_BGFLAGS_FUNON)) \
	    baka_fun_exit(B, __baka_funinfo); \
	  errno = save_errno; \
	  return; \
	  /* NOTREACHED */ \
	} while (0)



/* b_fun.c */
struct baka_funinfo
{
  const char *bf_funname;
  const char *bf_pkgname;
  u_int32_t bf_debuglevel;
};



/* baka.c */
extern baka baka_baka_init(argc, &argv, &envp, char *configfile, int error_queue_length, int log_facility);
extern void baka_baka_destroy(baka);
extern baka baka_baka_thread_init(baka, char *name);
extern void baka_baka_thread_destroy(baka);



/* b_config.c */
extern struct baka_config *baka_config_init(baka B, char *filename, bakaflags flags);
extern int baka_config_reinit(baka B, struct baka_config *config);
extern void baka_config_destroy(baka B, struct baka_config *config);
extern void baka_config_write(baka B, struct baka_config *config, char *outfile);
extern int baka_config_insert(baka B, struct baka_config *config, char *key, char *value);
extern char *baka_config_first(baka B, struct baka_config *config, char *key);
extern char *baka_config_getnext(baka B, struct baka_config *config, char *key);
extern char *baka_config_delete(baka B, struct baka_config *config, char *key);



/* b_debug.c */
extern u_int32_t baka_debug_query(baka B, struct baka_debug *bdinfo, const char *funname, const char *pkgname, const char *program);
extern int baka_debug_set(baka B, struct baka_debug *bdinfo, const char *name, u_int32_t level);
extern int baka_debug_setconfig(baka B, struct baka_debug *bdinfo, baka_config_t config, const char *program);
extern void baka_debug_config(baka B, struct baka_debug *bdinfo, u_int16_t queuelen, FILE *fh, u_int8_t sysloglevel, bakaflags flags);
extern void baka_debug_iprint(baka B, struct baka_debug *bdinfo, char *buf);
extern void baka_debug_iprintf(baka B, struct baka_debug *bdinfo, char *format, ...);
extern void baka_debug_iprintbuf(baka B, struct baka_debug *bdinfo, struct bakavptr *buf);
extern void baka_debug_ivprintf(baka B, struct baka_debug *bdinfo, char *format, va_list ap);



/* b_error.c */
extern struct baka_error *baka_error_init(baka B, u_int16_t queuelen, FILE *fh, u_int8_t sysloglevel, bakaflags flags);
extern void baka_error_destroy(baka B, struct baka_error *beinfo);
extern void baka_error_config(baka B, struct baka_error *beinfo, u_int16_t queuelen, FILE *fh, u_int8_t sysloglevel, bakaflags flags);
extern u_int32_t baka_error_query(baka B, struct baka_error *beinfo, const char *funname, const char *pkgname, const char *program);
extern int baka_error_set(baka B, struct baka_error *beinfo, const char *name, u_int32_t level);
extern int baka_error_setconfig(baka B, struct baka_error *beinfo, baka_config_t config, const char *program);
extern void baka_error_config(baka B, struct baka_error *beinfo, u_int16_t queuelen, FILE *fh, u_int8_t sysloglevel, bakaflags flags);
extern void baka_error_iprint(baka B, struct baka_error *beinfo, char *buf);
extern void baka_error_iprintf(baka B, struct baka_error *beinfo, char *format, ...);
extern void baka_error_iprintbuf(baka B, struct baka_error *beinfo, struct bakavptr *buf);
extern void baka_error_ivprintf(baka B, struct baka_error *beinfo, char *format, va_list ap);



/* b_fun.c */
extern baka_funinfo baka_fun_entry(baka B, const char *func, const char *package);
extern void baka_fun_exit(baka B, baka_funinfo fh);
extern void baka_fun_reentry(baka B, void);
extern void baka_fun_trace(baka B, FILE *out, u_int8_t sysloglevel, bakaflags flags);
extern void baka_fun_set(baka B, bakaflags flags);
#define BAKA_FUN_OFF	0			/* Turn off function tracing */
#define BAKA_FUN_ON	1			/* Turn on function tracing */



/* b_funlist.c */
extern struct baka_funlist *baka_funlist_init(baka B);
extern void baka_funlist_destroy(baka B, struct baka_funlist *funlist);
extern void baka_funlist_call(baka B, struct baka_funlist *funlist, u_int aux);
extern void baka_funlist_insert(baka B, struct baka_funlist *funlist, void (*bf_fun)(baka, void *, u_int), void *args);
extern void baka_funlist_delete(baka B, struct baka_funlist *funlist, void (*bf_fun)(baka, void *, u_int), void *args);



#endif /* _baka_h_ */
