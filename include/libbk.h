/*
 * $Id: libbk.h,v 1.68 2001/11/26 18:12:27 jtt Exp $
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

/**
 * @file
 * Public APIs for libbk
 */

#ifndef _LIBBK_h_
#define _LIBBK_h_
#include "libbk_include.h"
#include "libbk_oscompat.h"



/* Forward references */
struct bk_ioh;
struct bk_addrgroup;
struct bk_server_info;



typedef u_int32_t bk_flags;			///< Normal bitfield type



/* Error levels & their syslog equivalents */
#define BK_ERR_NONE	-1			///< No error logging or level
#define BK_ERR_CRIT	LOG_CRIT		///< Critical error
#define BK_ERR_ERR	LOG_ERR			///< Error
#define BK_ERR_WARN	LOG_WARNING		///< Warning message
#define BK_ERR_NOTICE	LOG_NOTICE		///< Notice about something
#define BK_ERR_DEBUG	LOG_DEBUG		///< Debugging



#define BK_APP_CONF	"/etc/bk.conf"		///< Default configuration file name
#define BK_ENV_GWD(e,d)	((char *)(getenv(e)?getenv(e):(d))) ///< Get an environmental variable with a default if it does not work
#define BK_GWD(B,k,d) (bk_config_getnext(B, NULL, (k), NULL)?:(d)) ///< Get a value from the config file, or return a default
#define BK_SYSLOG_MAXLEN 256			///< Length of maximum user message we will syslog
#define BK_FLAG_SET(var,bit) ((var) |= (bit))	///< Set a bit in a simple bitfield
#define BK_FLAG_ISSET(var,bit) ((var) & (bit))	///< Test if bit is set in a simple bitfield
#define BK_FLAG_CLEAR(var,bit) ((var) &= ~(bit)) ///< Clear a bit in a simple bitfield
#define BK_FLAG_ISCLEAR(var,bit) (!((var) & (bit))) ///< Test of bit is clear in a simple bitfield
#define BK_STREQ(a,b) ((a) && (b) && !strcmp((a),(b))) ///< Are two strings equivalent
#define BK_STREQN(a,b,n) ((a) && (b) && ((int)n>=0) && !strncmp(a,b,n)) ///< Are two strings different?

#define BK_CALLOC(p) BK_CALLOC_LEN(p,sizeof(*(p))) ///< Structure allocation calloc with assignment and type cast
#define BK_CALLOC_LEN(p,l) ((p) = (typeof(p))calloc(1,l))	///< Calloc with assignment and type cast
#define BK_MALLOC(p) BK_MALLOC_LEN(p,sizeof(*(p))) ///< Structure allocation malloc with assignment and type cast
#define BK_MALLOC_LEN(p,l) ((p) = (typeof(p))malloc(l))	///< Malloc with assignment and type cast
#define BK_ZERO(p) memset((p),0,sizeof(*p))	///< Memset for the common case
#define BK_BITS_SIZE(b)		(((b)+7)/8)	///< Size of complex bitfield in bytes
#define BK_BITS_BYTENUM(b)	((b)/8)		///< Which byte a particular bit is in
#define BK_BITS_BITNUM(b)	((b)%8)		///< Which bit in a byte has a particular bit
#define BK_BITS_VALUE(B,b)	(((B)[BK_BITS_BYTENUM(b)] & (1 << BK_BITS_BITNUM(b))) >> BK_BITS_BITNUM(b)) ///< Discover truth (0 or 1) value of a particular bit in a complex bitmap
#define BK_BITS_SET(B,b,v)	(B)[BK_BITS_BYTENUM(b)] = (((B)[BK_BITS_BYTENUM(b)] & ~(1 << BK_BITS_BITNUM(b))) | ((v) & 1) << BK_BITS_BITNUM(b)); ///< Set a particular bit in a complex bitmap



/**
 * @name Debugging function common interface
 * Virtualize the general bk_debug routines which work on a specific debug queues
 * to use the bk_general debug queue.
 */
// @{
#define BK_DEBUG_KEY "debug"			///< The default key to look up debugging levels in the configuration file
#define BK_DEBUG_DEFAULTLEVEL "*DEFAULT*"	///< The name of the default "function" which will set the debug level of all functions
#define bk_debug_and(B,l) (BK_GENERAL_FLAG_ISDEBUGON(B) && BK_BT_CURFUN(B) && (BK_BT_CURFUN(B)->bf_debuglevel & (l))) ///< Test to see if this function has a particular per-function debugging bit set
#define bk_debug_vprintf_and(B,l,f,ap) (bk_debug_and(B,l)?bk_debug_vprintf(B,f,ap):1) ///< Perform a debugging vprintf if a particular per-function debugging bit is set
#define bk_debug_printf_and(B,l,f,args...) (bk_debug_and(B,l)?bk_debug_printf(B,f,##args):1) ///< Perform a debugging printf if a particular per-function debugging bit is set
#define bk_debug_printbuf_and(B,l,i,p,v) (bk_debug_and(B,l)?bk_debug_printbuf(B,i,p,v):1) ///< Perform a debugging printbuf if a particular per-function debugging bit is set
#define bk_debug_print(B,s) ((BK_GENERAL_FLAG_ISDEBUGON(B) && BK_BT_CURFUN(B))?bk_debug_iprint(B,BK_GENERAL_DEBUG(B),s):1) ///< Perform a debugging print if debugging is enabled
#define bk_debug_printf(B,f,args...) ((BK_GENERAL_FLAG_ISDEBUGON(B) && BK_BT_CURFUN(B))?bk_debug_iprintf(B,BK_GENERAL_DEBUG(B),f,##args):1) ///< Perform a debugging printf if debugging is enabled
#define bk_debug_vprintf(B,f,ap) ((BK_GENERAL_FLAG_ISDEBUGON(B) && BK_BT_CURFUN(B))?bk_debug_ivprintf(B,BK_GENERAL_DEBUG(B),f,ap):1) ///< Perform a debugging vprintf if debugging is enabled
#define bk_debug_printbuf(B,i,p,v) ((BK_GENERAL_FLAG_ISDEBUGON(B) && BK_BT_CURFUN(B))?bk_debug_iprintbuf(B,BK_GENERAL_DEBUG(B),i,p,v):1) ///< Perform a debugging printbuf if debugging is enabled
// @}



/**
 * @name Error function common interface
 * Virtualize the general bk_error routines which work on a specific error queues
 * to use the bk_general error queue.
 */
// @{
#define bk_error_print(B,l,s) bk_error_iprint(B,l,BK_GENERAL_ERROR(B),s) ///< Print an error message
#define bk_error_printf(B,l,f,args...) bk_error_iprintf(B,l,BK_GENERAL_ERROR(B),f,##args) ///< Print an error message in printf-style format
#define bk_error_vprintf(B,l,f,ap) bk_error_ivprintf(B,l,BK_GENERAL_ERROR(B),f,ap) ///< Print an error message in printf-style format
#define bk_error_printbuf(B,l,i,p,v) bk_error_iprintbuf(B,l,BK_GENERAL_ERROR(B),i,p,v) ///< Print an error message along with a binary buffer
#define bk_error_dump(B,f,m,M,s,F) bk_error_idump(B,BK_GENERAL_ERROR(B),f,m,M,s,F) ///< Dump the error queues to a certain file handle, syslog level, with various filtering options
#define bk_error_mark(B,m,F) bk_error_idump(B,BK_GENERAL_ERROR(B),m,F) ///< Mark a particular location in the error queue for future reference
#define bk_error_clear(B,m,F) bk_error_iclear(B,BK_GENERAL_ERROR(B),m,F) ///< Delete a previously inserted mark
#define bk_error_flush(B,m,F) bk_error_iflush(B,BK_GENERAL_ERROR(B),m,F) ///< Flush the error queue, from a mark if desired.
// @}



/**
 * @name bk_general structure, accessors, and flags
 */
// @{
/**
 * General libbk global information shared by all threads.  Do NOT access this
 * structure directly.  Instead use BK_GENERAL macros for OOishness
 */
struct bk_general
{
  struct bk_error	*bg_error;		///< Error info
  struct bk_debug	*bg_debug;		///< Debug info
  struct bk_funlist	*bg_reinit;		///< Reinitialization list
  struct bk_funlist	*bg_destroy;		///< Destruction list
  struct bk_proctitle	*bg_proctitle;		///< Process title info
  struct bk_config	*bg_config;		///< Configuration info
  char			*bg_program;		///< Name of program
  bk_flags		bg_flags;		///< Flags
#define BK_BGFLAGS_FUNON	1		///< Is function tracing on?
#define BK_BGFLAGS_DEBUGON	2		///< Is debugging on?
#define BK_BGFLAGS_SYSLOGON	4		///< Is syslog on?
};
#define BK_GENERAL_ERROR(B)	((B)?(B)->bt_general->bg_error:(struct bk_error *)bk_nullptr) ///< Access the bk_general error queue
#define BK_GENERAL_DEBUG(B)	((B)?(B)->bt_general->bg_debug:(struct bk_debug *)bk_nullptr) ///< Access the bk_general debug queue
#define BK_GENERAL_REINIT(B)	((B)?(B)->bt_general->bg_reinit:(struct bk_funlist *)bk_nullptr) ///< Access the bk_general reinit list
#define BK_GENERAL_DESTROY(B)	((B)?(B)->bt_general->bg_destroy:(struct bk_funlist *)bk_nullptr) ///< Access the bk_general destruction list
#define BK_GENERAL_PROCTITLE(B) ((B)?(B)->bt_general->bg_proctitle:(struct bk_proctitle *)bk_nullptr) ///< Access the bk_general process title state
#define BK_GENERAL_CONFIG(B)	((B)?(B)->bt_general->bg_config:(struct bk_config *)bk_nullptr) ///< Access the bk_general config info
#define BK_GENERAL_PROGRAM(B)	((B)?(B)->bt_general->bg_program:(char *)bk_nullptr) ///< Access the bk_general program name
#define BK_GENERAL_FLAGS(B)	((B)?(B)->bt_general->bg_flags:bk_zerouint) ///< Access the bk_general flags
#define BK_GENERAL_FLAG_ISFUNON(B)   BK_FLAG_ISSET(BK_GENERAL_FLAGS(B), BK_BGFLAGS_FUNON) ///< Is function tracing on?
#define BK_GENERAL_FLAG_ISDEBUGON(B)  BK_FLAG_ISSET(BK_GENERAL_FLAGS(B), BK_BGFLAGS_DEBUGON) ///< Is debugging on?
#define BK_GENERAL_FLAG_ISSYSLOGON(B) BK_FLAG_ISSET(BK_GENERAL_FLAGS(B), BK_BGFLAGS_SYSLOGON) ///< Is system logging on?
// @}



/**
 * @name bk_thread structure, accessors, and flags
 */
// @{
/**
 * Baka general per-thread state information.  Do NOT access this
 * structure directly.  Instead use BK_BT macros for OOishness.
 */
typedef struct __bk_thread
{
  dict_h		bt_funstack;		///< Function stack
  struct bk_funinfo	*bt_curfun;		///< Current function
  const char		*bt_threadname;		///< Thread name
  struct bk_general	*bt_general;		///< Common program state
  bk_flags		bt_flags;		///< Flags for the future
} *bk_s;
#define BK_BT_FUNSTACK(B)	((B)->bt_funstack)   ///< Access the function stack
#define BK_BT_CURFUN(B)		((B)->bt_curfun)     ///< Access the current function
#define BK_BT_THREADNAME(B)	((B)->bt_threadname) ///< Access the thread name
#define BK_BT_GENERAL(B)	((B)->bt_general)    ///< Access the global shared info
#define BK_BT_FLAGS(B)		((B)->bt_flags)      ///< Access thread-specific flags
// @}



/**
 * The possible "sides" of a socket
 */
typedef enum
{
  BK_SOCKET_SIDE_LOCAL=1,			///< The local side.
  BK_SOCKET_SIDE_REMOTE,			///< The remote side.
} bk_socket_side_t;



/**
 * Actions permitted in modifying fd flags
 */
typedef enum 
{
  BK_FILEUTILS_MODIFY_FD_FLAGS_ACTION_ADD=1,
  BK_FILEUTILS_MODIFY_FD_FLAGS_ACTION_DELETE,
} bk_fileutils_modify_fd_flags_action_t;



/**
 * Enum list of known network address types.
 */
typedef enum 
{ 
  BK_NETINFO_TYPE_UNKNOWN=0,			///< Special "unset" marker
  BK_NETINFO_TYPE_INET,				///< IPv4 address
  BK_NETINFO_TYPE_INET6,			///< IPv6 address
  BK_NETINFO_TYPE_LOCAL,			///< AF_LOCAL/AF_UNIX address
  BK_NETINFO_TYPE_ETHER,			///< Ethernet address
} bk_netaddr_type_t;


#define BK_NETINFO_TYPE_NONE BK_NETINFO_TYPE_UNKNOWN ///< Alias for BK_NETINFO_TYPE_UNKNOWN

/**
 * Results from an asynchronous connection (these are mostly geared towars
 * the client side.
 */
typedef enum
{
  BK_ADDRGROUP_STATE_NULL=0,			///< Doesn't match any other state.
  BK_ADDRGROUP_STATE_TIMEOUT,			///< Connection timedout.
  BK_ADDRGROUP_STATE_WIRE_ERROR,		///< Connection got an error off the wire.
  BK_ADDRGROUP_STATE_BAD_ADDRESS,		///< Something's wrong with the addresses in use.
  BK_ADDRGROUP_STATE_ABORT,			///< We've aborted for some reason.
  BK_ADDRGROUP_STATE_NEWCONNECTION,		///< Here's a new connection.
  BK_ADDRGROUP_STATE_CONNECTING,		///< We are connecting.
  BK_ADDRGROUP_STATE_ACCEPTING,			///< We are accepting.
  BK_ADDRGROUP_STATE_READY,			///< Server ready.
  BK_ADDRGROUP_STATE_CLOSING,			///< We're closing.
} bk_addrgroup_state_t;



/**
 * File descriptor handler type.
 */
struct bk_run;
typedef void (*bk_fd_handler_t)(bk_s B, struct bk_run *run, int fd, u_int gottypes, void *opaque, struct timeval starttime);

/**
 * @name bk_addrgroup structure.
 */
// @{
/**
 * Callback when connections complete/abort and server listens
 * complete/abort. NB: Choosing to store the @a server_handle means you
 * accept the responsibility of noting if and when it dies. Should
 * something go wrong with the server to which it refers, you will receive
 * a BK_ADDRGROUP_STATE_CLOSING for that socket (ie the server socket, not
 * any connected sockets you might be currently handling). The
 * server_handle is still valid at this point, but from the moment you
 * return it is <em>no longer usuable</em> (its underlying memory will be
 * freed). Do not reuse. Indeed about the only thin you probably want to do
 * when you get this noticification is to NULL out your stored version of
 * the handle (and any local cleanup associated with this). Please be aware
 * too that if you call bk_addrgroup_server_close(), you will still get the
 * closing notification.
 *	@param B BAKA thread/global state.
 *	@param args User args
 *	@param sock The new socket.
 *	@param bag The address group pair (if you requested it).
 *	@param server_handle The handle for referencing the server (accepting connections only). 
 *	@param state State as described by @a bk_addrgroup_state_t.
 *
 */
typedef void (*bk_bag_callback_t)(bk_s B, void *args, int sock, struct bk_addrgroup *bag, void *server_handle, bk_addrgroup_state_t state);

/**
 * Structure which describes a network "association". This name is slightly
 * bogus owing to the fact that server listens (which aren't tehcnically
 * associations) and unconnected udp use this structure too. 
 */
struct bk_addrgroup
{
  bk_flags		bag_flags;		///< Everyone needs flags */
  /* XXX timeout probably belongs in local address_state structure */
  struct bk_netinfo *	bag_local;		///< Local side information */
  struct bk_netinfo *	bag_remote;		///< Remote side information */
  int			bag_proto;		///< Cached proto */
  bk_netaddr_type_t	bag_type;		///< Cached address family */
};



#define BK_ADDRGROUP_FLAGS(bag) ((bag)->bag_flags)
#define BK_ADDRGROUP_TIMEOUT(bag) ((bag)->bag_timeout)
#define BK_ADDRGROUP_LOCAL_NETINFO(bag) ((bag)->bag_local)
#define BK_ADDRGROUP_REMOTE_NETINFO(bag) ((bag)->bag_remote)
#define BK_ADDRGROUP_CALLBACK(bag) ((bag)->bag_callback)
#define BK_ADDRGROUP_ARGS(bag) ((bag)->bag_args)


#define BK_ADDRGROUP_FLAG_DIVIDE_TIMEOUT	0x1 ///< Timeout should be divided among all addresses
#define BK_ADDRGROUP_FLAG_WANT_ADDRGROUP	0x2 ///< I want a filled out addrgroup on my callback. If you take this you want it on both newconnection and server ready. You must destroy with bk_addrgroup_destroy()


// @}

/**
 * @name Function tracing common interfacce
 * Virtualize the bk_fun routines which work on a specific function
 * stack to use the per-thread function stack available through (B)
 */
// @{
/**
 * @brief Re-enter the current function after the function stack has been
 * initialized
 */
#define bk_fun_reentry(B) bk_fun_reentry_i(B, __bk_funinfo)

/**
 * @brief Normal method to let function tracing know you have entered
 * function--if function tracing is enabled
 */
#define BK_ENTRY(B, fun, pkg, grp) struct bk_funinfo *__bk_funinfo = (B && !BK_GENERAL_FLAG_ISFUNON(B)?NULL:bk_fun_entry(B, fun, pkg, grp))

/**
 * @brief Return a value, letting function tracing know you are exiting the
 * function, while preseving errno
 */
#define BK_RETURN(B, retval)			\
do {						\
  int save_errno = errno;			\
  if (!(B) || BK_GENERAL_FLAG_ISFUNON(B))	\
    bk_fun_exit((B), __bk_funinfo);		\
  errno = save_errno;				\
  return retval;				\
  /* NOTREACHED */				\
} while (0)					


/**
 * @brief Return a value, letting function tracing know you are exiting the
 * function, while preseving errno, and preseving the order of return
 * value function stack evaluation (e.g. if the return value is
 * actually a function which must be evaluated--the normal version
 * will have the return value evaluation be called by the parent
 * instead of this function, from function tracing's perspective
 */
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


/**
 * @brief Let function tracing know you are returning from this function, and
 * return void
 */
#define BK_VRETURN(B)				\
do {						\
  int save_errno = errno;			\
  if (!(B) || BK_GENERAL_FLAG_ISFUNON(B))	\
    bk_fun_exit((B), __bk_funinfo);		\
  errno = save_errno;				\
  return;					\
  /* NOTREACHED */				\
} while (0)					
// @}



/**
 * @name Basic Timeval Operations
 * Perform basic timeval operations (add, subtract, compare, etc).
 */
// @{
/** @brief Add two timevals together--answer may be an argument */
#define BK_TV_ADD(sum,a,b)				\
    do							\
    {							\
      (sum)->tv_sec = (a)->tv_sec + (b)->tv_sec;	\
      (sum)->tv_usec = (a)->tv_usec + (b)->tv_usec;	\
      BK_TV_RECTIFY(sum);				\
    } while (0)
/** @brief Subtract timeval a from b--anwser may be an argument */
#define BK_TV_SUB(sum,a,b)				\
    do							\
    {							\
      (sum)->tv_sec = (a)->tv_sec - (b)->tv_sec;	\
      (sum)->tv_usec = (a)->tv_usec - (b)->tv_usec;	\
      BK_TV_RECTIFY(sum);				\
    } while (0)
/** @brief Compare two timevals, return trinary value around zero */
#define BK_TV_CMP(a,b) (((a)->tv_sec==(b)->tv_sec)?((a)->tv_usec-(b)->tv_usec):((a)->tv_sec-(b)->tv_sec))
/** @brief Convert a timeval to a float. */
#define BK_TV2F(tv) ((double)(((double)((tv)->tv_sec)) + ((double)((tv)->tv_usec))/1000000.0))
/** @brief Rectify timeval so that usec value is within range of second, and that usec value has same sign as sec.  Performed automatically on all BK_TV operations. */
#define BK_TV_RECTIFY(sum)					\
    do								\
    {								\
      if ((sum)->tv_usec >= BK_TV_SECTOUSEC(1))			\
      {								\
	(sum)->tv_sec += (sum)->tv_usec / BK_TV_SECTOUSEC(1);	\
	(sum)->tv_usec %= BK_TV_SECTOUSEC(1);			\
      }								\
								\
      if ((sum)->tv_usec <= -BK_TV_SECTOUSEC(1))		\
      {								\
	(sum)->tv_sec += (sum)->tv_usec / BK_TV_SECTOUSEC(1);	\
	(sum)->tv_usec %= -BK_TV_SECTOUSEC(1);			\
      }								\
								\
      if ((sum)->tv_usec < 0 && (sum)->tv_sec > 0)		\
      {								\
	(sum)->tv_sec--;					\
	(sum)->tv_usec += BK_TV_SECTOUSEC(1);			\
      }								\
								\
      if ((sum)->tv_usec > 0 && (sum)->tv_sec < 0)		\
      {								\
	(sum)->tv_sec++;					\
	(sum)->tv_usec -= BK_TV_SECTOUSEC(1);			\
      }								\
								\
    } while (0)
/** @brief Number of microseconds in a particular number of seconds */
#define BK_TV_SECTOUSEC(s) ((s)*1000000)	
// @}


/** @brief Special symbol which means resolve to "any" address. */
#define BK_ADDR_ANY "bk_addr_any"

/**
 * General vectored pointer
 */
typedef struct bk_vptr
{
  void *ptr;					///< Data
  u_int32_t len;				///< Length
} bk_vptr;


/**
 * Pointer with current length and maximum length
 */
struct bk_alloc_ptr
{
  void *ptr;					///< Data
  u_int32_t cur;				///< Current length
  u_int32_t max;				///< Maximum length
};


/**
 * A structure for communicating configuration file configuration
 * parameters to the config subsystem during baka general
 * initialization.  Typically, this will be NULL and not used.
 */
struct bk_config_user_pref
{
  char *	bcup_include_tag;		///< Include file tag
  char *	bcup_separator;		 	///< key/value sep. char
  bk_flags	bcup_flags;			///< Everyone needs flags
};



/**
 * The information stored about every function on the function stack during
 * function tracing.
 */
struct bk_funinfo
{
  const char *bf_funname;			///< Function name
  const char *bf_pkgname;			///< Package name
  const char *bf_grpname;			///< Group name
  u_int32_t bf_debuglevel;			///< Per-function debug level
};



/**
 * @a bk_servinfo struct. Pretty much the same thing as a @a servent, but
 * wth a few @a BAKAisms. For instance since the the protocol is often
 * implied by the current conext, you may expect the @a bsi_protostr to be
 * NULL in many cases.
 */
struct bk_servinfo
{
  bk_flags		bsi_flags;		///< Everyone needs flags
  u_int			bsi_port;		///< Port (network order)
  char *		bsi_servstr;		///< Service string
  char *		bsi_protostr;		///< Proto str (NULL OK)
};



/**
 * @a servinfo struct. Pretty much the same thing as a @a servent, but wth
 * a fiew @a BAKAisms. For instance the protocol is stored as a pointer to
 * a @bk_protoent, but since proto is often implied by context, this value
 * may indeed be NULL in many cases
 */
struct bk_protoinfo
{
  bk_flags		bpi_flags;		///< Everyone needs flags
  int			bpi_proto;		///< Protocol number
  char *		bpi_protostr;		///< Protocol string
};



/** 
 * Everything you ever wanted to know about a network address. 
 */
struct bk_netaddr
{
  bk_flags		bna_flags;		///< Everyone needs flags
  bk_netaddr_type_t	bna_type;		///< Type of address
  u_int			bna_len;		///< Length of address
  union
  {
    struct in_addr	bnaa_inet;		///< IPv4 address
    struct in6_addr	bnaa_inet6;		///< IPv6 address
    char *		bnaa_path;		///< AF_LOCAL/AF_UNIX address
    struct ether_addr	bnaa_ether;		///< Ethernet address
  } bna_addr;
  char *		bna_pretty;		///< Printable form of addr
  dict_h		bna_netinfo_addrs;	///< Back pointer to prevent double free
};


#define bna_inet	bna_addr.bnaa_inet
#define bna_inet6	bna_addr.bnaa_inet6
#define bna_path	bna_addr.bnaa_path
#define bna_ether	bna_addr.bnaa_ether

/**
 * Everything you ever wanted to know about networks (well not routing info)
 */
struct bk_netinfo
{
  bk_flags		bni_flags;		///< Everyone needs flags
  struct bk_netaddr *	bni_addr;		///< Primary address
  struct bk_netaddr *	bni_addr2;		///< Secondary address
  dict_h		bni_addrs;		///< dll of addrs
  struct bk_servinfo *	bni_bsi;		///< Service info
  struct bk_protoinfo *	bni_bpi;		///< Protocol info
  char *		bni_pretty;		///< Printable forms
};

/**
 * @name Defines: netinfo_addrs_clc
 * list of addresses within a @a struct @bk_netinfo.
 * which hides CLC choice.
 */
// @{
#define netinfo_addrs_create(o,k,f)		dll_create((o),(k),(f))
#define netinfo_addrs_destroy(h)		dll_destroy(h)
#define netinfo_addrs_insert(h,o)		dll_insert((h),(o))
#define netinfo_addrs_insert_uniq(h,n,o)	dll_insert_uniq((h),(n),(o))
#define netinfo_addrs_append(h,o)		dll_append((h),(o))
#define netinfo_addrs_append_uniq(h,n,o)	dll_append_uniq((h),(n),(o))
#define netinfo_addrs_search(h,k)		dll_search((h),(k))
#define netinfo_addrs_delete(h,o)		dll_delete((h),(o))
#define netinfo_addrs_minimum(h)		dll_minimum(h)
#define netinfo_addrs_maximum(h)		dll_maximum(h)
#define netinfo_addrs_successor(h,o)		dll_successor((h),(o))
#define netinfo_addrs_predecessor(h,o)		dll_predecessor((h),(o))
#define netinfo_addrs_iterate(h,d)		dll_iterate((h),(d))
#define netinfo_addrs_nextobj(h,i)		dll_nextobj((h),(i))
#define netinfo_addrs_iterate_done(h,i)		dll_iterate_done((h),(i))
#define netinfo_addrs_error_reason(h,i)		dll_error_reason((h),(i))
static int bna_oo_cmp(void *bck1, void *bck2);
static int bna_ko_cmp(void *a, void *bck2);
// @}



/* b_general.c */
extern bk_s bk_general_init(int argc, char ***argv, char ***envp, const char *configfile, struct bk_config_user_pref *bcup, int error_queue_length, int log_facility, bk_flags flags);
#define BK_GENERAL_NOPROCTITLE 1		///< Specify that proctitle is not desired during general baka initialization
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
#define BK_SYSLOG_FLAG_NOFUN 1			///< Don't want function name included during bk_general_*syslog
#define BK_SYSLOG_FLAG_NOLEVEL 2		///< Don't want error level included during bk_general_*syslog
extern const char *bk_general_errorstr(bk_s B, int level);
extern int bk_general_debug_config(bk_s B, FILE *fh, int sysloglevel, bk_flags flags);
extern void *bk_nullptr;			/* NULL pointer junk */
extern int bk_zeroint;				/* Zero integer junk */
extern unsigned bk_zerouint;			/* Zero unsigned junk */


/* b_bits.c */
extern char *bk_bits_create(bk_s B, size_t size, bk_flags flags);
extern void bk_bits_destroy(bk_s B, char *base);
extern char *bk_bits_save(bk_s B, char *base, size_t size, bk_flags flags);
extern char *bk_bits_restore(bk_s B, char *saved, size_t *size, bk_flags flags);



/* b_cksum.c */
extern int bk_in_cksum(register struct bk_vptr **m, register int len);



/* b_config.c */
extern struct bk_config *bk_config_init(bk_s B, const char *filename, struct bk_config_user_pref *bcup, bk_flags flags);
extern int bk_config_get(bk_s B, struct bk_config *config, const char **filename, struct bk_config_user_pref **bcup, bk_flags *getflags);
extern int bk_config_reinit(bk_s B, struct bk_config *config);
extern void bk_config_destroy(bk_s B, struct bk_config *config);
extern void bk_config_write(bk_s B, struct bk_config *config, char *outfile);
extern int bk_config_insert(bk_s B, struct bk_config *config, char *key, char *value);
extern char *bk_config_getnext(bk_s B, struct bk_config *ibc, const char *key, const char *ovalue);
extern int bk_config_delete_key(bk_s B, struct bk_config *ibc, const char *key);
extern int bk_config_delete_value(bk_s B, struct bk_config *ibc, const char *key, const char *value);
extern void bk_config_print(bk_s B, struct bk_config *ibc, FILE *fp);



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
#define BK_ERROR_CONFIG_FH			0x1 ///< File handle is being set
#define BK_ERROR_CONFIG_HILO_PIVOT		0x2 ///< Hilo_pivot is being set
#define BK_ERROR_CONFIG_SYSLOGTHRESHHOLD	0x4 ///< Syslog threshhold is being set
#define BK_ERROR_CONFIG_QUEUELEN		0x8 ///< Queue length is being set
extern void bk_error_iclear(bk_s B, struct bk_error *beinfo, const char *mark, bk_flags flags);
extern void bk_error_imark(bk_s B, struct bk_error *beinfo, const char *mark, bk_flags flags);
extern void bk_error_iprint(bk_s B, int sysloglevel, struct bk_error *beinfo, char *buf);
extern void bk_error_iprintf(bk_s B, int sysloglevel, struct bk_error *beinfo, char *format, ...) __attribute__ ((format (printf, 4,5)));
extern void bk_error_iprintbuf(bk_s B, int sysloglevel, struct bk_error *beinfo, char *intro, char *prefix, bk_vptr *buf);
extern void bk_error_ivprintf(bk_s B, int sysloglevel, struct bk_error *beinfo, char *format, va_list ap);
extern void bk_error_idump(bk_s B, struct bk_error *beinfo, FILE *fh, char *mark, int minimumlevel, int sysloglevel, bk_flags flags);



/* b_fun.c */
extern dict_h bk_fun_init(void);
extern void bk_fun_destroy(dict_h funstack);
extern struct bk_funinfo *bk_fun_entry(bk_s B, const char *func, const char *package, const char *group);
extern void bk_fun_exit(bk_s B, struct bk_funinfo *fh);
extern void bk_fun_reentry_i(bk_s B, struct bk_funinfo *fh);
extern void bk_fun_trace(bk_s B, FILE *out, int sysloglevel, bk_flags flags);
extern void bk_fun_set(bk_s B, int state, bk_flags flags);
#define BK_FUN_OFF	0			///< Turn off function tracing in @a bk_fun_set
#define BK_FUN_ON	1			///< Turn on function tracing in @a bk_fun_set
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
#define BK_MEMX_PRESERVE_ARRAY    1		///< Don't destroy created array in @a bk_memx_destroy
extern void *bk_memx_get(bk_s B, struct bk_memx *bm, u_int count, u_int *curused, bk_flags flags);
#define BK_MEMX_GETNEW		  1		///< Get new allocation in @a bk_memx_get
extern int bk_memx_trunc(bk_s B, struct bk_memx *bm, u_int count, bk_flags flags);


/* b_run.c */
extern struct bk_run *bk_run_init(bk_s B, bk_flags flags);
extern void bk_run_destroy(bk_s B, struct bk_run *run);
extern int bk_run_signal(bk_s B, struct bk_run *run, int signum, void (*handler)(bk_s B, struct bk_run *run, int signum, void *opaque, struct timeval starttime), void *opaque, bk_flags flags);
#define BK_RUN_SIGNAL_CLEARPENDING		0x01	///< Clear pending signal count for this signum for @a bk_run_signal
#define BK_RUN_SIGNAL_INTR			0x02	///< Interrupt system calls for @a bk_run_signal
#define BK_RUN_SIGNAL_RESTART			0x04	///< Restart system calls for @a bk_run_signal
extern int bk_run_enqueue(bk_s B, struct bk_run *run, struct timeval when, void (*event)(bk_s B, struct bk_run *run, void *opaque, struct timeval starttime, bk_flags flags), void *opaque, void **handle, bk_flags flags);
extern int bk_run_enqueue_delta(bk_s B, struct bk_run *run, time_t usec, void (*event)(bk_s B, struct bk_run *run, void *opaque, struct timeval starttime, bk_flags flags), void *opaque, void **handle, bk_flags flags);
extern int bk_run_enqueue_cron(bk_s B, struct bk_run *run, time_t usec, void (*event)(bk_s B, struct bk_run *run, void *opaque, struct timeval starttime, bk_flags flags), void *opaque, void **handle, bk_flags flags);
extern int bk_run_dequeue(bk_s B, struct bk_run *run, void *handle, bk_flags flags);
#define BK_RUN_DEQUEUE_EVENT			0x01	///< Normal event to dequeue for @a bk_run_dequeue
#define BK_RUN_DEQUEUE_CRON			0x02	///< Cron event to dequeue for @a bk_run_dequeue
extern int bk_run_run(bk_s B, struct bk_run *run, bk_flags flags);
extern int bk_run_once(bk_s B, struct bk_run *run, bk_flags flags);
extern int bk_run_handle(bk_s B, struct bk_run *run, int fd, bk_fd_handler_t handler, void *opaque, u_int wanttypes, bk_flags flags);
#define BK_RUN_READREADY			0x01	///< user handler is notified by bk_run that data is ready for reading
#define BK_RUN_WRITEREADY			0x02	///< user handler is notified by bk_run that data is ready for writing
#define BK_RUN_XCPTREADY			0x04	///< user handler is notified by bk_run that exception data is ready
#define BK_RUN_CLOSE				0x08	///< user handler is notified by bk_run that bk_run is in process of closing this fd
#define BK_RUN_DESTROY				0x10	///< user handler is notified by bk_run that bk_run is in process of destroying
extern int bk_run_close(bk_s B, struct bk_run *run, int fd, bk_flags flags);
#define BK_RUN_NOTIFYANYWAY			1	///< Notify user callback that this fd is going away from bk_run, even though user is telling us
extern u_int bk_run_getpref(bk_s B, struct bk_run *run, int fd, bk_flags flags);
extern int bk_run_setpref(bk_s B, struct bk_run *run, int fd, u_int wanttypes, u_int wantmask, bk_flags flags);
#define BK_RUN_WANTREAD				0x01	///< Specify to bk_run_setpref that we want read notification for this fd
#define BK_RUN_WANTWRITE			0x02	///< Specify to bk_run_setpref that we want write notification for this fd
#define BK_RUN_WANTXCPT				0x04	///< Specify to bk_run_setpref that we want exceptional notification for this fd
#define BK_RUN_WANTALL				(BK_RUN_WANTREAD|BK_RUN_WANTWRITE|BK_RUN_WANTXCPT|) ///< Specify to bk_run_setpref that we want *all* notifcations.
extern int bk_run_poll_add(bk_s B, struct bk_run *run, int (*fun)(bk_s B, struct bk_run *run, void *opaque, struct timeval starttime, struct timeval *delta, bk_flags flags), void *opaque, void **handle);
extern int bk_run_poll_remove(bk_s B, struct bk_run *run, void *handle);
extern int bk_run_idle_add(bk_s B, struct bk_run *run, int (*fun)(bk_s B, struct bk_run *run, void *opaque, struct timeval starttime, struct timeval *delta, bk_flags flags), void *opaque, void **handle);
extern int bk_run_idle_remove(bk_s B, struct bk_run *run, void *handle);
extern int bk_run_on_demand_add(bk_s B, struct bk_run *run, int (*fun)(bk_s B, struct bk_run *run, void *opaque, volatile int *demand, struct timeval starttime, bk_flags flags), void *opaque, volatile int *demand, void **handle);
extern int bk_run_on_demand_remove(bk_s B, struct bk_run *run, void *handle);
extern int bk_run_set_run_over(bk_s B, struct bk_run *run);


/* b_ioh.c */
typedef int (*bk_iorfunc)(bk_s, int, caddr_t, __SIZE_TYPE__, bk_flags); ///< read style I/O function for bk_ioh (flags for specialized datagram handling, like peek)
typedef int (*bk_iowfunc)(bk_s, int, struct iovec *, __SIZE_TYPE__, bk_flags); ///< writev style I/O function for bk_ioh
typedef void (*bk_iohhandler)(bk_s B, bk_vptr data[], void *opaque, struct bk_ioh *ioh, u_int state_flags);  ///< User callback for bk_ioh w/zero terminated array of data ptrs free'd after handler returns
extern struct bk_ioh *bk_ioh_init(bk_s B, int fdin, int fdout, bk_iorfunc readfun, bk_iowfunc writefun, bk_iohhandler handler, void *opaque, u_int32_t inbufhint, u_int32_t inbufmax, u_int32_t outbufmax, struct bk_run *run, bk_flags flags);
#define BK_IOH_STREAM		0x01		///< Stream (instead of datagram) oriented protocol, for bk_ioh
#define BK_IOH_RAW		0x02		///< Any data is suitable, no special message blocking, for bk_ioh
#define BK_IOH_BLOCKED		0x04		///< Must I/O in hint blocks: block size is required, for bk_ioh
#define BK_IOH_VECTORED		0x08		///< Size of data sent before data: datagramish semantics, for bk_ioh
#define BK_IOH_LINE		0x10		///< Line oriented reads, for bk_ioh
#define BK_IOH_STATUS_INCOMPLETEREAD	1	///< bk_ioh notifying user handler of incomplete read, data provided
#define BK_IOH_STATUS_IOHCLOSING	2	///< bk_ioh notifying user handler of IOH in process of close() -- all data flushed/drained at this point
#define BK_IOH_STATUS_IOHREADERROR	3	///< bk_ioh notifying user handler of a received IOH error on read
#define BK_IOH_STATUS_IOHWRITEERROR	4	///< bk_ioh notifying user handler of a received IOH error on write--and here is the buffer
#define BK_IOH_STATUS_IOHABORT		5	///< bk_ioh notifying user handler of IOH going away due to system constraints
#define BK_IOH_STATUS_WRITECOMPLETE	6	///< bk_ioh notifying user handler of a previous write of a buffer has completed, and here is the buffer
#define BK_IOH_STATUS_WRITEABORTED	7	///< bk_ioh notifying user handler of a previous write of a buffer was not fully completed before abort, and here is the buffer
#define BK_IOH_STATUS_READCOMPLETE	8	///< bk_ioh notifying user handler of a previous write of a buffer has completed, and here is the zero terminated array of buffers, which you must copy if necessary
#define BK_IOH_STATUS_USERERROR		9	///< bk_ioh notifying user handler of some kind of user error being propagated in some unknown and unknowable fashion
#define BK_IOH_STATUS_IOHREADEOF	10	///< bk_ioh notifying user handler of a received EOF
extern int bk_ioh_update(bk_s B, struct bk_ioh *ioh, bk_iorfunc readfun, bk_iowfunc writefun, bk_iohhandler handler, void *opaque, u_int32_t inbufhint, u_int32_t inbufmax, u_int32_t outbufmax, bk_flags flags);
extern int bk_ioh_get(bk_s B, struct bk_ioh *ioh, int *fdin, int *fdout, bk_iorfunc *readfun, bk_iowfunc *writefun, bk_iohhandler *handler, void **opaque, u_int32_t *inbufhint, u_int32_t *inbufmax, u_int32_t *outbufmax, struct bk_run **run, bk_flags *flags);
extern int bk_ioh_write(bk_s B, struct bk_ioh *ioh, bk_vptr *data, bk_flags flags);
#define BK_IOH_BYPASSQUEUEFULL	0x01		///< Bypass bk_ioh_write normal check for output queue full
extern void bk_ioh_shutdown(bk_s B, struct bk_ioh *ioh, int how, bk_flags flags);
extern void bk_ioh_readallowed(bk_s B, struct bk_ioh *ioh, int isallowed, bk_flags flags);
extern void bk_ioh_flush(bk_s B, struct bk_ioh *ioh, int how, bk_flags flags);
extern void bk_ioh_close(bk_s B, struct bk_ioh *ioh, bk_flags flags);
#define BK_IOH_ABORT		0x01		///< During bk_ioh_close: Abort stream immediately -- don't wait to drain */
#define BK_IOH_DONTCLOSEFDS	0x04		///< During bk_ioh_close: Don't close the file descriptors during close */
extern void bk_ioh_destroy(bk_s B, struct bk_ioh *ioh);
extern int bk_ioh_stdrdfun(bk_s B, int fd, caddr_t buf, __SIZE_TYPE__ size, bk_flags flags);		///< read() when implemented in ioh style
extern int bk_ioh_stdwrfun(bk_s B, int fd, struct iovec *buf, __SIZE_TYPE__ size, bk_flags flags);	///< write() when implemented in ioh style
extern int bk_ioh_getqlen(bk_s B, struct bk_ioh *ioh, u_int32_t *inqueue, u_int32_t *outqueue, bk_flags flags);

/* b_stdfun.c */
extern void bk_die(bk_s B, u_char retcode, FILE *output, char *reason, bk_flags flags);
extern void bk_warn(bk_s B, FILE *output, char *reason, bk_flags flags);
#define BK_WARNDIE_WANTDETAILS		1	///< Verbose output of error logs during bk_die/bk_warn
extern void bk_exit(bk_s B, u_char retcode);


/* b_string.c */
extern u_int bk_strhash(char *a, bk_flags flags);
#define BK_STRHASH_NOMODULUS	0x01		///< Do not perform modulus of hash by a large prime
extern char **bk_string_tokenize_split(bk_s B, char *src, u_int limit, char *spliton, void *variabledb, bk_flags flags);
#define BK_WHITESPACE					" \t\r\n" ///< General definition of horizonal and vertical whitespace
#define BK_VWHITESPACE					"\r\n" ///< General definition of vertical whitespace
#define BK_HWHITESPACE					" \t" ///< General definition of horizontal whitespace
#define BK_STRING_TOKENIZE_MULTISPLIT			0x001	///< During bk_string_tokenize_split: Allow multiple split characters to seperate items (foo::bar are two tokens, not three)
#define BK_STRING_TOKENIZE_SINGLEQUOTE			0x002	///< During bk_string_tokenize_split: Handle single quotes
#define BK_STRING_TOKENIZE_DOUBLEQUOTE			0x004	///< During bk_string_tokenize_split: Handle double quotes
#ifdef NOTYET
#define BK_STRING_TOKENIZE_SIMPLEVARIABLE		0x008	///< During bk_string_tokenize_split: Convert $VAR (not currently available)
#endif /* NOTYET */
#define BK_STRING_TOKENIZE_BACKSLASH			0x010	///< During bk_string_tokenize_split: Backslash quote next char
#define BK_STRING_TOKENIZE_BACKSLASH_INTERPOLATE_CHAR	0x020	///< During bk_string_tokenize_split: Convert \n et al
#define BK_STRING_TOKENIZE_BACKSLASH_INTERPOLATE_OCT	0x040	///< During bk_string_tokenize_split: Convert \010 et al
#define BK_STRING_TOKENIZE_SKIPLEADING			0x080   ///< During bk_string_tokenize_split: Bypass leading split chars
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
#define BK_STRING_QUOTE_NONPRINT	0x01	///< During bk_string_quote: Quote non-printable
#define BK_STRING_QUOTE_NULLOK		0x02	///< During bk_string_quote: Output NULL
#define BK_NULLSTR			"NULL"  ///< During bk_string_quote: String rep of NULL
char *bk_string_flagtoa(bk_s B, bk_flags src, bk_flags flags);
extern int bk_string_atoflag(bk_s B, char *src, bk_flags *dst, bk_flags flags);
extern ssize_t bk_strnlen(bk_s B, char *s, ssize_t max);
extern char *bk_encode_base64(bk_s B, bk_vptr *str, char *eolseq);
extern bk_vptr *bk_decode_base64(bk_s B, const char *str);


/* getbyfoo.c */
extern int bk_getprotobyfoo(bk_s B, char *protostr, struct protoent **ip, struct bk_netinfo *bni, bk_flags flags);
#define BK_GETPROTOBYFOO_FORCE_LOOKUP	0x1	///< Do lookup even if argumnet suggests otherwise.
extern void bk_protoent_destroy(bk_s B, struct protoent *p);
extern int bk_getservbyfoo(bk_s B, char *servstr, char *iproto, struct servent **is, struct bk_netinfo *bni, bk_flags flags);
#define BK_GETSERVBYFOO_FORCE_LOOKUP	0x1	///< Do lookup even if argumnet suggests otherwise.
extern void bk_servent_destroy(bk_s B, struct servent *s);
extern void *bk_gethostbyfoo(bk_s B, char *name, int family, struct hostent **ih, struct bk_netinfo *bni, struct bk_run *br, void (*callback)(bk_s B, struct bk_run *run, struct hostent **h, struct bk_netinfo *bni, void *args), void *args, bk_flags user_flags);
#define BK_GETHOSTBYFOO_FLAG_FQDN	0x1	///< Get the FQDN
extern void bk_destroy_hostent(bk_s B, struct hostent *h);
extern void bk_gethostbyfoo_info_destroy(bk_s B, void *opaque);


/* b_netinfo.c */
extern struct bk_netinfo *bk_netinfo_create(bk_s B);
extern void bk_netinfo_destroy(bk_s B, struct bk_netinfo *bni);
extern int bk_netinfo_add_addr(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *bna, struct bk_netaddr **obna);
extern int bk_netinfo_delete_addr(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *ibna, struct bk_netaddr **obna);
extern int bk_netinfo_set_primary_address(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *bna);
extern int bk_netinfo_reset_primary_address(bk_s B, struct bk_netinfo *bni);
extern struct bk_netinfo *bk_netinfo_clone(bk_s B, struct bk_netinfo *obni);
extern int bk_netinfo_update_servent(bk_s B, struct bk_netinfo *bni, struct servent *s);
extern int bk_netinfo_update_protoent(bk_s B, struct bk_netinfo *bni, struct protoent *p);
extern int bk_netinfo_update_hostent(bk_s B, struct bk_netinfo *bni, struct hostent *h);
extern struct bk_netaddr *bk_netinfo_get_addr(bk_s B, struct bk_netinfo *bni);
extern int bk_netinfo_to_sockaddr(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *bna, bk_netaddr_type_t type, struct sockaddr *sa, bk_flags flags);
#define BK_NETINFO2SOCKADDR_FLAG_FUZZY_ANY	0x1 ///< Allow bad address information to indicate ANY addresss.
extern struct bk_netinfo *bk_netinfo_from_socket(bk_s B, int s, int proto, bk_socket_side_t side);
extern const char *bk_netinfo_info(bk_s B, struct bk_netinfo *bni);




/* b_netaddr.c */
extern struct bk_netaddr *bk_netaddr_create(bk_s B);
extern void bk_netaddr_destroy(bk_s B, struct bk_netaddr *bna);
extern struct bk_netaddr *bk_netaddr_user(bk_s B, bk_netaddr_type_t type, void *addr, int len, bk_flags flags);
extern struct bk_netaddr *bk_netaddr_addrdup (bk_s B, int type, void *addr, bk_flags flags);
extern struct bk_netaddr *bk_netaddr_clone (bk_s B, struct bk_netaddr *obna);
extern int bk_netaddr_af2nat(bk_s B, int af);
extern int bk_netaddr_nat2af(bk_s B, int type);


/* b_servinfo.c */
extern struct bk_servinfo *bk_servinfo_serventdup (bk_s B, struct servent *s, struct bk_protoinfo *bpi);
extern struct bk_servinfo *bk_servinfo_user(bk_s B, char *servstr, u_short port, char *protostr);
extern void bk_servinfo_destroy (bk_s B,struct bk_servinfo *bsi);
extern struct bk_servinfo *bk_servinfo_clone (bk_s B, struct bk_servinfo *obsi);
extern struct bk_protoinfo *bk_protoinfo_protoentdup (bk_s B, struct protoent *p);
extern struct bk_protoinfo *bk_protoinfo_user(bk_s B, char *protoname, int proto);
extern void bk_protoinfo_destroy (bk_s B,struct bk_protoinfo *bsi);
extern struct bk_protoinfo *bk_protoinfo_clone (bk_s B, struct bk_protoinfo *obsi);

/* b_netutils.c */
extern int bk_netutils_get_sa_len(bk_s B, struct sockaddr *sa);
extern int bk_parse_endpt_spec(bk_s B, char *urlstr, char **hoststr, char *defhoststr, char **servicestr,  char *defservicestr, char **protostr, char *defprotostr);
extern int bk_netutils_start_service(bk_s B, struct bk_run *run, char *url, char *defhoststr, char *defservstr, char *defprotostr, char *securenets, bk_bag_callback_t callback, void *args, int backlog, bk_flags flags);
extern int bk_netutils_make_conn(bk_s B, struct bk_run *run, char *rurl, char *defrhost, char *defrserv, char *lurl, char *deflhost, char *deflserv, char *defproto, char *sercurenets, u_long timeout, bk_bag_callback_t callback, void *args, bk_flags flags );



/* b_signal.c */
typedef void (*bk_sighandler)(int);
extern bk_sighandler bk_signal(bk_s B, int signo, bk_sighandler handler, bk_flags flags);


/* b_relay.c */
int bk_relay_ioh(bk_s B, struct bk_ioh *ioh1, struct bk_ioh *ioh2, void (*donecb)(bk_s B, void *opaque, u_int state), void *opaque, bk_flags flags);

/* b_fileutils.c */
extern int bk_fileutils_modify_fd_flags(bk_s B, int fd, long flags, bk_fileutils_modify_fd_flags_action_t action);

/* b_addrgroup.c */
extern int bk_net_init(bk_s B, struct bk_run *run, struct bk_netinfo *local, struct bk_netinfo *remote, u_long timeout, bk_flags flags, bk_bag_callback_t callback, void *args, int backlog);
void bk_addrgroup_destroy(bk_s B,struct bk_addrgroup *bag);
extern int bk_netutils_commandeer_service(bk_s B, struct bk_run *run, int s, char *securenets, bk_bag_callback_t callback, void *args, bk_flags flags);
int bk_addrgroup_get_server_socket(bk_s B, void *server_handle);
int bk_addrgroup_server_close(bk_s B, void *server_handle);

#endif /* _BK_h_ */
