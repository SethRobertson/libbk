/*
 * $Id: libbk.h,v 1.8 2001/06/18 22:41:46 seth Exp $
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

#ifndef _LIBBK_h_
#define _LIBBK_h_
#include "libbk_include.h"
#include "libbk_oscompat.h"


/* Error levels & their syslog equivalents */
#define BK_ERR_NONE	-1
#define BK_ERR_CRIT	LOG_CRIT
#define BK_ERR_ERR	LOG_ERR
#define BK_ERR_WARNING	LOG_WARNING
#define BK_ERR_NOTICE	LOG_NOTICE
#define BK_ERR_DEBUG	LOG_DEBUG



typedef u_int32_t bk_flags;

/* General vectored pointer */
typedef struct bk_vptr
{
  void *ptr;					/* Data */
  u_int32_t len;				/* Length */
} bk_vptr;

/* General vectored pointer */
struct bk_alloc_ptr
{
  void *ptr;					/* Data */
  u_int32_t cur;				/* Current length */
  u_int32_t max;				/* Maximum length */
};


/* b_general.c -- General libbk global information */
struct bk_general
{
  struct bk_error	*bg_error;		/* Error info */
  struct bk_debug	*bg_debug;		/* Debug info */
  struct bk_funlist	*bg_reinit;		/* Reinitialization list */
  struct bk_funlist	*bg_destroy;		/* Destruction list */
  struct bk_proctitle	*bg_proctitle;		/* Process title info */
  struct bk_config	*bg_config;		/* Configuration info */
  bk_flags		bg_flags;		/* Flags */
#define BK_BGFLAGS_FUNON	1		/* Is function tracing on? */
#define BK_BGFLAGS_DEBUGON	2		/* Is debugging on? */
#define BK_BGFLAGS_SYSLOGON	4		/* Is syslog on? */
};

/* Per-thread state information */
typedef struct __bk_thread
{
  dict_h		bt_funstack;		/* Function stack */
  struct bk_funinfo	*bt_curfun;		/* Current function */
  const char		*bt_threadname;		/* Function name */
  struct bk_general	*bt_general;		/* Common program state */
  bk_flags		bt_flags;
} *bk_s;



/* b_debug.c */
#define bk_debug_and(B,l) (((B)->bt_flags & BK_BGFLAGS_DEBUGON) && (B)->bt_curfun && ((B)->bt_curfun->bf_debuglevel & (l)))
#define bk_debug_vprintf_and(B,l,f,ap) (bk_debug_and(B,l) && sos_debug_vprintf(B,f,ap))
#define bk_debug_printf_and(B,l,f,args...) (bk_debug_and(B,l) && sos_debug_printf(B,f,##args))
#define bk_debug_printbuf_and(B,l,v) (bk_debug_and(B,l) && sos_debug_printf(B,v))
#define bk_debug_print(B,s) bk_debug_iprint(B,(B)->bt_general->bg_debug,s)
#define bk_debug_printf(B,f,args...) bk_debug_iprintf(B,(B)->bt_general->bg_debug,f,##args)
#define bk_debug_vprintf(B,f,ap) bk_debug_ivprintf(B,(B)->bt_general->bg_debug,f,ap)
#define bk_debug_printbuf(B,v) bk_debug_iprintbuf(B,(B)->bt_general->bg_debug,v)



/* b_error.c */
#define bk_error_print(B,s) bk_error_iprint(B,(B)->bt_general->bg_error,s)
#define bk_error_printf(B,f,args...) bk_error_iprintf(B,(B)->bt_general->bg_error,f,##args)
#define bk_error_vprintf(B,f,ap) bk_error_ivprintf(B,(B)->bt_general->bg_error,f,ap)
#define bk_error_printbuf(B,v) bk_error_iprintbuf(B,(B)->bt_general->bg_error,v)



/* b_fun.c */
#define BK_ENTRY(B, fun, pkg, grp) bk_funinfo __bk_funinfo = (B->bg_flags & BK_BGFLAGS_FUNON) ? NULL : bk_fun_entry(B, fun, pkg, grp)
#define BK_RETURN(B, retval) \
	do { \
	  typeof(retval) myretval = (retval); \
	  int save_errno = errno; \
	  if (!(B->bg_flags & BK_BGFLAGS_FUNON)) \
	    bk_fun_exit(B, __bk_funinfo); \
	  errno = save_errno; \
	  return myretval; \
	  /* NOTREACHED */ \
	} while (0)
#define BK_VRETURN(B) \
	do { \
	  int save_errno = errno; \
	  if (!(B->bg_flags & BK_BGFLAGS_FUNON)) \
	    bk_fun_exit(B, __bk_funinfo); \
	  errno = save_errno; \
	  return; \
	  /* NOTREACHED */ \
	} while (0)



/* b_fun.c */
struct bk_funinfo
{
  const char *bf_funname;
  const char *bf_pkgname;
  u_int32_t bf_debuglevel;
};



/* b_general.c */
extern bk_s bk_general_init(int argc, char ***argv, char ***envp, char *configfile, int error_queue_length, int log_facility);
extern void bk_general_setproctitle(bk_s B, char *);
extern void bk_general_reinit(bk_s B);
extern void bk_general_destroy(bk_s B);
extern bk_s bk_general_thread_init(bk_s B, char *name);
extern void bk_general_thread_destroy(bk_s B);
/* XXX - bk_general_reinit_insert/delete ? */


/* b_config.c */
extern struct bk_config *bk_config_init(bk_s B, char *filename, bk_flags flags);
extern int bk_config_reinit(bk_s B, struct bk_config *config);
extern void bk_config_destroy(bk_s B, struct bk_config *config);
extern void bk_config_write(bk_s B, struct bk_config *config, char *outfile);
extern int bk_config_insert(bk_s B, struct bk_config *config, char *key, char *value);
extern char *bk_config_first(bk_s B, struct bk_config *config, char *key);
extern char *bk_config_getnext(bk_s B, struct bk_config *config, char *key);
extern char *bk_config_delete(bk_s B, struct bk_config *config, char *key);



/* b_debug.c */
extern struct bk_debug *bk_debug_init(bk_s B);
extern void bk_debug_destroy(bk_s B, struct bk_debug *bd);
extern void bk_debug_reinit(bk_s B, struct bk_debug *bd);
extern u_int32_t bk_debug_query(bk_s B, struct bk_debug *bdinfo, const char *funname, const char *pkgname, const char *program);
extern int bk_debug_set(bk_s B, struct bk_debug *bdinfo, const char *name, u_int32_t level);
extern int bk_debug_setconfig(bk_s B, struct bk_debug *bdinfo, struct bk_config *config, const char *program);
extern void bk_debug_config(bk_s B, struct bk_debug *bdinfo, FILE *fh, u_int8_t sysloglevel, bk_flags flags);
extern void bk_debug_iprint(bk_s B, struct bk_debug *bdinfo, char *buf);
extern void bk_debug_iprintf(bk_s B, struct bk_debug *bdinfo, char *format, ...);
extern void bk_debug_iprintbuf(bk_s B, struct bk_debug *bdinfo, bk_vptr *buf);
extern void bk_debug_ivprintf(bk_s B, struct bk_debug *bdinfo, char *format, va_list ap);



/* b_error.c */
extern struct bk_error *bk_error_init(bk_s B, u_int16_t queuelen, FILE *fh, u_int8_t sysloglevel, bk_flags flags);
extern void bk_error_destroy(bk_s B, struct bk_error *beinfo);
extern void bk_error_config(bk_s B, struct bk_error *beinfo, u_int16_t queuelen, FILE *fh, u_int8_t sysloglevel, bk_flags flags);
extern u_int32_t bk_error_query(bk_s B, struct bk_error *beinfo, const char *funname, const char *pkgname, const char *program);
extern void bk_error_config(bk_s B, struct bk_error *beinfo, u_int16_t queuelen, FILE *fh, u_int8_t sysloglevel, bk_flags flags);
extern void bk_error_iprint(bk_s B, struct bk_error *beinfo, char *buf);
extern void bk_error_iprintf(bk_s B, struct bk_error *beinfo, char *format, ...);
extern void bk_error_iprintbuf(bk_s B, struct bk_error *beinfo, bk_vptr *buf);
extern void bk_error_ivprintf(bk_s B, struct bk_error *beinfo, char *format, va_list ap);



/* b_fun.c */
extern dict_h bk_fun_init();
extern void bk_fun_destroy(dict_h funstack);
extern struct bk_funinfo bk_fun_entry(bk_s B, const char *func, const char *package);
extern void bk_fun_exit(bk_s B, struct bk_funinfo fh);
/* extern void bk_fun_reentry(bk_s B, void *); huh? */
extern void bk_fun_trace(bk_s B, FILE *out, u_int8_t sysloglevel, bk_flags flags);
extern void bk_fun_set(bk_s B, bk_flags flags);
#define BK_FUN_OFF	0			/* Turn off function tracing */
#define BK_FUN_ON	1			/* Turn on function tracing */



/* b_funlist.c */
extern struct bk_funlist *bk_funlist_init(bk_s B);
extern void bk_funlist_destroy(bk_s B, struct bk_funlist *funlist);
extern void bk_funlist_call(bk_s B, struct bk_funlist *funlist, u_int aux);
extern int bk_funlist_insert(bk_s B, struct bk_funlist *funlist, void (*bf_fun)(bk_s, void *, u_int), void *args);
extern int bk_funlist_delete(bk_s B, struct bk_funlist *funlist, void (*bf_fun)(bk_s, void *, u_int), void *args);



/* b_run.c */
extern struct bk_run *bk_run_init(bk_s B, int (*pollfun)(void *opaque, time_t starttime), void *pollopaque, volatile int *demand, int (*ondemand)(void *opaque, volatile int *demand, time_t starttime), void *demandopaque, bk_flags flags);
extern void bk_run_destroy(bk_s B, struct bk_run *run);
extern int bk_run_signal(bk_s B, struct bk_run *run, int signum, void (*handler)(int signum, void *opaque, time_t starttime), void *opaque);
extern int bk_run_designal(bk_s B, int signum);
extern int bk_run_enqueue(bk_s B, struct bk_run *run, struct timeval *when, void (*event)(void *opaque, time_t starttime), void *opaque, void **handle);
extern int bk_run_enqueue_delta(bk_s B, struct bk_run *run, time_t usec, void (*event)(void *opaque, time_t starttime), void *opaque, void **handle);
extern int bk_run_enqueue_cron(bk_s B, struct bk_run *run, time_t usec, void (*event)(void *opaque, time_t starttime), void *opaque, void **handle);
extern int bk_run_dequeue(bk_s B, struct bk_run *run, void *handle);



/* b_ioh.c */
typedef int bk_iofunc(int, caddr_t, __SIZE_TYPE__);
extern struct bk_ioh *bk_ioh_init(bk_s B, int fdin, int fdout, bk_iofunc readfun, bk_iofunc writefun, int (*handler)(bk_vptr *data, void *opaque, struct bk_ioh *ioh, u_int state_flags), void *opaque, bk_flags flags, u_int32_t inbuflen, u_int32_t outbuflen, struct bk_run *run);
#define BK_IOH_STREAM		0x01
#define BK_IOH_RAW		0x02
extern int bk_ioh_update(bk_s B, bk_iofunc readfun, bk_iofunc writefun, int (*handler)(bk_vptr *data, void *opaque, struct bk_ioh *ioh, u_int state_flags), void *opaque, bk_flags flags, u_int32_t inbuflen, u_int32_t outbuflen);
extern int bk_ioh_get(bk_s B, int *fdin, int *fdout, bk_iofunc *readfun, bk_iofunc *writefun, int (**handler)(bk_vptr *data, void *opaque, struct bk_ioh *ioh, u_int state_flags), void **opaque, bk_flags *flags, u_int32_t *inbuflen, u_int32_t *outbuflen, struct bk_run *run);
extern void bk_run_close(bk_s B, struct bk_run *run, bk_flags flags);
#define BK_IOH_ABORT		0x01		/* Abort stream immediately -- don't wait to drain */
#define BK_IOH_NOTIFYANYWAY	0x02		/* Call handler notifying when close actually completes */
extern void bk_run_destroy(bk_s B, struct bk_run *run);



#endif /* _BK_h_ */
