/*
 * $Id: libbk.h,v 1.25 2001/09/05 02:56:34 seth Exp $
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

/* XXX Seth want this moved, but for the moment */
/* Define NULL the *proper* way. */
#if 0
#ifdef NULL
#undef NULL
#define NULL ((void *)0)
#endif
#endif

/* Error levels & their syslog equivalents */
#define BK_ERR_NONE	-1
#define BK_ERR_CRIT	LOG_CRIT
#define BK_ERR_ERR	LOG_ERR
#define BK_ERR_WARN	LOG_WARNING
#define BK_ERR_NOTICE	LOG_NOTICE
#define BK_ERR_DEBUG	LOG_DEBUG


/* Very generic (should not be used) application configuration file */
#define BK_APP_CONF	"/etc/bk.conf"

#define BK_ENV_GWD(e,d)		((char *)(getenv(e)?getenv(e):(d)))

/* General constants */
#define BK_SYSLOG_MAXLEN 256			/* Length of maximum user message we will syslog */

/* General purpose macros */
#define BK_FLAG_SET(var,bit) ((var) |= (bit))
#define BK_FLAG_ISSET(var,bit) ((var) & (bit))
#define BK_FLAG_CLEAR(var,bit) ((var) &= ~(bit))
#define BK_FLAG_ISCLEAR(var,bit) (!((var) & (bit)))

/* String equality functions */
#define BK_STREQ(a,b) ((a) && (b) && !strcmp((a),(b)))
#define BK_STREQN(a,b,n) ((a) && (b) && ((int)n>=0) && !strncmp(a,b,n))

/* Malloc functions (g++ sucks -- it *requires* casts from (void *) */
#define BK_MALLOC_LEN(o,l) do { if ((o)=(typeof(o))malloc(l)) memset((o),0,l); } while(0)
#define BK_MALLOC(o) BK_MALLOC_LEN((o),sizeof(*(o)))

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
  char			*bg_program;		/* Name of program */
  bk_flags		bg_flags;		/* Flags */
#define BK_BGFLAGS_FUNON	1		/* Is function tracing on? */
#define BK_BGFLAGS_DEBUGON	2		/* Is debugging on? */
#define BK_BGFLAGS_SYSLOGON	4		/* Is syslog on? */
};
#define BK_GENERAL_ERROR(B)	((B)?(B)->bt_general->bg_error:(struct bk_error *)bk_nullptr)
#define BK_GENERAL_DEBUG(B)	((B)?(B)->bt_general->bg_debug:(struct bk_debug *)bk_nullptr)
#define BK_GENERAL_REINIT(B)	((B)?(B)->bt_general->bg_reinit:(struct bk_funlist *)bk_nullptr)
#define BK_GENERAL_DESTROY(B)	((B)?(B)->bt_general->bg_destroy:(struct bk_funlist *)bk_nullptr)
#define BK_GENERAL_PROCTITLE(B) ((B)?(B)->bt_general->bg_proctitle:(struct bk_proctitle *)bk_nullptr)
#define BK_GENERAL_CONFIG(B)	((B)?(B)->bt_general->bg_config:(struct bk_config *)bk_nullptr)
#define BK_GENERAL_PROGRAM(B)	((B)?(B)->bt_general->bg_program:(char *)bk_nullptr)
#define BK_GENERAL_FLAGS(B)	((B)?(B)->bt_general->bg_flags:bk_zeroint)
#define BK_GENERAL_FLAG_ISFUNON(B)   BK_FLAG_ISSET(BK_GENERAL_FLAGS(B), BK_BGFLAGS_FUNON)
#define BK_GENERAL_FLAG_ISDEBUGON(B)  BK_FLAG_ISSET(BK_GENERAL_FLAGS(B), BK_BGFLAGS_DEBUGON)
#define BK_GENERAL_FLAG_ISSYSLOGON(B) BK_FLAG_ISSET(BK_GENERAL_FLAGS(B), BK_BGFLAGS_SYSLOGON)

/* Per-thread state information */
typedef struct __bk_thread
{
  dict_h		bt_funstack;		/* Function stack */
  struct bk_funinfo	*bt_curfun;		/* Current function */
  const char		*bt_threadname;		/* Function name */
  struct bk_general	*bt_general;		/* Common program state */
  bk_flags		bt_flags;
} *bk_s;
#define BK_BT_FUNSTACK(B)	((B)->bt_funstack)
#define BK_BT_CURFUN(B)		((B)->bt_curfun)
#define BK_BT_THREADNAME(B)	((B)->bt_threadname)
#define BK_BT_GENERAL(B)	((B)->bt_general)
#define BK_BT_FLAGS(B)		((B)->bt_flags)



/* b_debug.c */
#define BK_DEBUG_KEY "debug"
#define BK_DEBUG_DEFAULTLEVEL "*DEFAULT*"
#define bk_debug_and(B,l) (BK_GENERAL_FLAG_ISDEBUGON(B) && BK_BT_CURFUN(B) && (BK_BT_CURFUN(B)->bf_debuglevel & (l)))
#define bk_debug_vprintf_and(B,l,f,ap) (bk_debug_and(B,l)?bk_debug_vprintf(B,f,ap):1)
#define bk_debug_printf_and(B,l,f,args...) (bk_debug_and(B,l)?bk_debug_printf(B,f,##args):1)
#define bk_debug_printbuf_and(B,l,i,p,v) (bk_debug_and(B,l)?bk_debug_printbuf(B,i,p,v):1)
#define bk_debug_print(B,s) ((BK_GENERAL_FLAG_ISDEBUGON(B) && BK_BT_CURFUN(B))?bk_debug_iprint(B,BK_GENERAL_DEBUG(B),s):1)
#define bk_debug_printf(B,f,args...) ((BK_GENERAL_FLAG_ISDEBUGON(B) && BK_BT_CURFUN(B))?bk_debug_iprintf(B,BK_GENERAL_DEBUG(B),f,##args):1)
#define bk_debug_vprintf(B,f,ap) ((BK_GENERAL_FLAG_ISDEBUGON(B) && BK_BT_CURFUN(B))?bk_debug_ivprintf(B,BK_GENERAL_DEBUG(B),f,ap):1)
#define bk_debug_printbuf(B,i,p,v) ((BK_GENERAL_FLAG_ISDEBUGON(B) && BK_BT_CURFUN(B))?bk_debug_iprintbuf(B,BK_GENERAL_DEBUG(B),i,pv):1)



/* b_error.c */
#define bk_error_print(B,l,s) bk_error_iprint(B,l,BK_GENERAL_ERROR(B),s)
#define bk_error_printf(B,l,f,args...) bk_error_iprintf(B,l,BK_GENERAL_ERROR(B),f,##args)
#define bk_error_vprintf(B,l,f,ap) bk_error_ivprintf(B,l,BK_GENERAL_ERROR(B),f,ap)
#define bk_error_printbuf(B,l,i,p,v) bk_error_iprintbuf(B,l,BK_GENERAL_ERROR(B),i,p,v)
#define bk_error_dump(B,f,s,F) bk_error_idump(B,BK_GENERAL_ERROR(B),f,s,F)



/* b_fun.c */
#define bk_fun_reentry(B) bk_fun_reentry_i(B, __bk_funinfo)
#define BK_ENTRY(B, fun, pkg, grp) struct bk_funinfo *__bk_funinfo = (B && !BK_GENERAL_FLAG_ISFUNON(B)?NULL:bk_fun_entry(B, fun, pkg, grp))

#define BK_RETURN(B, retval)			\
do {						\
  int save_errno = errno;			\
  if (!(B) || BK_GENERAL_FLAG_ISFUNON(B))	\
    bk_fun_exit((B), __bk_funinfo);		\
  errno = save_errno;				\
  return retval;				\
  /* NOTREACHED */				\
} while (0)


#define BK_ORETURN(B, retval)			\
do {						\
  typeof(retval) myretval = (retval);		\
  int save_errno = errno;			\
  if (!(B) || BK_GENERAL_FLAG_ISFUNON(B))	\
    bk_fun_exit((B), __bk_funinfo);		\
  errno = save_errno;				\
  return myretval;				\
  /* NOTREACHED */				\
} while (0)


#define BK_VRETURN(B)				\
do {						\
  int save_errno = errno;			\
  if (!(B) || BK_GENERAL_FLAG_ISFUNON(B))		\
    bk_fun_exit((B), __bk_funinfo);		\
  errno = save_errno;				\
  return;					\
  /* NOTREACHED */				\
} while (0)



     /* b_fun.c */
struct bk_funinfo
{
  const char *bf_funname;
  const char *bf_pkgname;
  const char *bf_grpname;
  u_int32_t bf_debuglevel;
};



/* b_general.c */
extern bk_s bk_general_init(int argc, char ***argv, char ***envp, const char *configfile, int error_queue_length, int log_facility, bk_flags flags);
#define BK_GENERAL_NOPROCTITLE 1
extern void bk_general_proctitle_set(bk_s B, char *);
extern void bk_general_reinit(bk_s B);
extern void bk_general_destroy(bk_s B);
extern bk_s bk_general_thread_init(bk_s B, char *name);
extern void bk_general_thread_destroy(bk_s B);
extern int bk_general_reinit_insert(bk_s B, void (*bf_fun)(bk_s, void *, u_int), void *args);
extern int bk_general_reinit_delete(bk_s B, void (*bf_fun)(bk_s, void *, u_int), void *args);
extern int bk_general_destroy_insert(bk_s B, void (*bf_fun)(bk_s, void *, u_int), void *args);
extern int bk_general_destroy_delete(bk_s B, void (*bf_fun)(bk_s, void *, u_int), void *args);
extern void bk_general_syslog(bk_s B, int level, bk_flags flags, char *format, ...) __attribute__ ((format (printf, 4, 5)));
extern void bk_general_vsyslog(bk_s B, int level, bk_flags flags, char *format, va_list args);
#define BK_SYSLOG_FLAG_NOFUN 1			/* Don't want function name included */
#define BK_SYSLOG_FLAG_NOLEVEL 2		/* Don't want error level included */
extern const char *bk_general_errorstr(bk_s B, int level);
extern int bk_general_debug_config(bk_s B, FILE *fh, int sysloglevel, bk_flags flags);
extern void *bk_nullptr;			/* NULL pointer junk */
extern int bk_zeroint;				/* Zero integer junk */

struct bk_config
{
  bk_flags			bc_flags;	/* Everyone needs flags */
  struct bk_config_fileinfo *	bc_bcf;		/* Files of conf data */
  dict_h			bc_kv;		/* Hash of value dlls */
  int				bc_kv_error;	/* clc errno for bc_kv */
};


/* b_cksum.c */
extern int b_in_cksum(register struct bk_vptr **m, register int len);


/* b_config.c */
extern struct bk_config *bk_config_init(bk_s B, const char *filename, bk_flags flags);
extern int bk_config_reinit(bk_s B, struct bk_config *config);
extern void bk_config_destroy(bk_s B, struct bk_config *config);
extern void bk_config_write(bk_s B, struct bk_config *config, char *outfile);
extern int bk_config_insert(bk_s B, struct bk_config *config, char *key, char *value);
extern char *bk_config_getnext(bk_s B, struct bk_config *ibc, const char *key, const char *ovalue);
extern int bk_config_delete_key(bk_s B, struct bk_config *ibc, const char *key);
extern int bk_config_delete_value(bk_s B, struct bk_config *ibc, const char *key, const char *value);



/* b_debug.c */
extern struct bk_debug *bk_debug_init(bk_s B, bk_flags flags);
extern void bk_debug_destroy(bk_s B, struct bk_debug *bd);
extern void bk_debug_reinit(bk_s B, struct bk_debug *bd);
extern u_int32_t bk_debug_query(bk_s B, struct bk_debug *bdinfo, const char *funname, const char *pkgname, const char *group, bk_flags flags);
extern int bk_debug_set(bk_s B, struct bk_debug *bdinfo, const char *name, u_int32_t level);
extern int bk_debug_setconfig(bk_s B, struct bk_debug *bdinfo, struct bk_config *config, const char *program);
extern void bk_debug_config(bk_s B, struct bk_debug *bdinfo, FILE *fh, int sysloglevel, bk_flags flags);
extern void bk_debug_iprint(bk_s B, struct bk_debug *bdinfo, char *buf);
extern void bk_debug_iprintf(bk_s B, struct bk_debug *bdinfo, char *format, ...) __attribute__ ((format (printf, 3, 4)));
extern void bk_debug_iprintbuf(bk_s B, struct bk_debug *bdinfo,  char *intro, char *prefix, bk_vptr *buf);
extern void bk_debug_ivprintf(bk_s B, struct bk_debug *bdinfo, char *format, va_list ap);



/* b_error.c */
extern struct bk_error *bk_error_init(bk_s B, u_int16_t queuelen, FILE *fh, int syslogthreshhold, bk_flags flags);
extern void bk_error_destroy(bk_s B, struct bk_error *beinfo);
extern void bk_error_config(bk_s B, struct bk_error *beinfo, u_int16_t queuelen, FILE *fh, int syslogthreshhold, int hilo_pivot, bk_flags flags);
extern void bk_error_iprint(bk_s B, int sysloglevel, struct bk_error *beinfo, char *buf);
extern void bk_error_iprintf(bk_s B, int sysloglevel, struct bk_error *beinfo, char *format, ...) __attribute__ ((format (printf, 4,5)));
extern void bk_error_iprintbuf(bk_s B, int sysloglevel, struct bk_error *beinfo, char *intro, char *prefix, bk_vptr *buf);
extern void bk_error_ivprintf(bk_s B, int sysloglevel, struct bk_error *beinfo, char *format, va_list ap);
extern void bk_error_idump(bk_s, struct bk_error *beinfo, FILE *fh, int sysloglevel, bk_flags flags);



/* b_fun.c */
extern dict_h bk_fun_init(void);
extern void bk_fun_destroy(dict_h funstack);
extern struct bk_funinfo *bk_fun_entry(bk_s B, const char *func, const char *package, const char *group);
extern void bk_fun_exit(bk_s B, struct bk_funinfo *fh);
extern void bk_fun_reentry_i(bk_s B, struct bk_funinfo *fh);
extern void bk_fun_trace(bk_s B, FILE *out, int sysloglevel, bk_flags flags);
extern void bk_fun_set(bk_s B, int state, bk_flags flags);
#define BK_FUN_OFF	0			/* Turn off function tracing */
#define BK_FUN_ON	1			/* Turn on function tracing */
extern const char *bk_fun_funname(bk_s B, int ancestordepth, bk_flags flags);
extern int bk_fun_reset_debug(bk_s B, bk_flags flags);


/* b_funlist.c */
extern struct bk_funlist *bk_funlist_init(bk_s B, bk_flags flags);
extern void bk_funlist_destroy(bk_s B, struct bk_funlist *funlist);
extern void bk_funlist_call(bk_s B, struct bk_funlist *funlist, u_int aux, bk_flags flags);
extern int bk_funlist_insert(bk_s B, struct bk_funlist *funlist, void (*bf_fun)(bk_s, void *, u_int), void *args, bk_flags flags);
extern int bk_funlist_delete(bk_s B, struct bk_funlist *funlist, void (*bf_fun)(bk_s, void *, u_int), void *args, bk_flags flags);


/* b_memx.c */
extern struct bk_memx *bk_memx_create(bk_s B, size_t objsize, u_int start_hint, u_int incr_hint, bk_flags flags);
extern void bk_memx_destroy(bk_s B, struct bk_memx *bm, bk_flags flags);
#define BK_MEMX_PRESERVE_ARRAY    1		/* Don't destroy created array */
extern void *bk_memx_get(bk_s B, struct bk_memx *bm, u_int count, u_int *curused, bk_flags flags);
#define BK_MEMX_GETNEW		  1		/* Get new allocation */
extern int bk_memx_trunc(bk_s B, struct bk_memx *bm, u_int count, bk_flags flags);


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


/* b_stdfun.c */
extern void bk_die(bk_s B, u_char retcode, FILE *output, char *reason, bk_flags flags);
extern void bk_warn(bk_s B, FILE *output, char *reason, bk_flags flags);
#define BK_WARNDIE_WANTDETAILS		1	/* Verbose output of error logs */
extern void bk_exit(bk_s B, u_char retcode);



/* b_string.c */
extern u_int bk_strhash(char *a, bk_flags flags);
#define BK_STRHASH_NOMODULUS	0x01
extern char **bk_string_tokenize_split(bk_s B, char *src, u_int limit, char *spliton, void *variabledb, bk_flags flags);
#define BK_WHITESPACE					" \t\r\n"
#define BK_VWHITESPACE					"\r\n"
#define BK_HWHITESPACE					" \t"
#define BK_STRING_TOKENIZE_MULTISPLIT			0x001	/* Allow multiple split characters to seperate items (foo::bar are two tokens, not three)  */
#define BK_STRING_TOKENIZE_SINGLEQUOTE			0x002	/* Handle single quotes */
#define BK_STRING_TOKENIZE_DOUBLEQUOTE			0x004	/* Handle double quotes */
#ifdef NOTYET
#define BK_STRING_TOKENIZE_SIMPLEVARIABLE		0x008	/* Convert $VAR */
#endif /* NOTYET */
#define BK_STRING_TOKENIZE_BACKSLASH			0x010	/* Backslash quote next char */
#define BK_STRING_TOKENIZE_BACKSLASH_INTERPOLATE_CHAR	0x020	/* Convert \n et al */
#define BK_STRING_TOKENIZE_BACKSLASH_INTERPOLATE_OCT	0x040	/* Convert \010 et al */
#define BK_STRING_TOKENIZE_SKIPLEADING			0x080   /* Bypass leading split chars */
#define BK_STRING_TOKENIZE_SIMPLE	(BK_STRING_TOKENIZE_MULTISPLIT)
#define BK_STRING_TOKENIZE_NORMAL	(BK_STRING_TOKENIZE_MULTISPLIT|BK_STRING_TOKENIZE_DOUBLEQUOTE)
#define BK_STRING_TOKENIZE_CONFIG	(BK_STRING_TOKENIZE_DOUBLEQUOTE)
extern void bk_string_tokenize_destroy(bk_s B, char **tokenized);
extern char *bk_string_printbuf(bk_s B, char *intro, char *prefix, bk_vptr *buf, bk_flags flags);
extern int bk_string_atou(bk_s B, char *string, u_int32_t *value, bk_flags flags);
extern int bk_string_atoi(bk_s B, char *string, int32_t *value, bk_flags flags);
extern int bk_string_atoull(bk_s B, char *string, u_int64_t *value, bk_flags flags);
extern int bk_string_atoill(bk_s B, char *string, int64_t *value, bk_flags flags);
extern char *bk_string_rip(bk_s B, char *string, char *terminators, bk_flags flags);
extern char *bk_string_quote(bk_s B, char *src, char *needquote, bk_flags flags);
#define BK_STRING_QUOTE_NONPRINT	0x01	/* Quote non-printable */
char *bk_string_flagtoa(bk_s B, bk_flags src, bk_flags flags);
extern int bk_string_atoflag(bk_s B, char *src, bk_flags *dst, bk_flags flags);

#endif /* _BK_h_ */
