#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_ioh.c,v 1.1 2001/11/02 23:13:03 seth Exp $";
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
 * All of the baka ioh public and private functions.
 */

#include <libbk.h>
#include "libbk_internal.h"



/**
 * Information about a specific ioh.
 */
struct bk_ioh
{
  int			ioh_fdin;		/**< Input file descriptor */
  int			ioh_fdout;		/**< Output file descriptor */
  bk_iofunc 		ioh_readfun;		/**< Function to read data */
  bk_iofunc		ioh_writefun;		/**< Function to write data */
  bk_iohhandler		ioh_handler;		/**< Callback function for event handling */
  void		       *ioh_opaque;		/**< Opaque data for handler */
  u_int32_t		ioh_inbuf_hint;		/**< Hint for input buffer sizes */
  u_int32_t		ioh_inbuf_max;		/**< Maxmum size of input buffer */
  u_int32_t		ioh_outbuf_max;		/**< Maxmum amount of data buffered for output */
  struct bk_run	       *ioh_run;		/**< BK_RUN environment */
  bk_flags		ioh_extflags;		/**< Flags--see libbk.h */
  bk_flags		ioh_intflags;		/**< Flags */
#define IOH_FLAGS_SHUTDOWN_INPUT	0x01	/**< Input shut down */
#define IOH_FLAGS_SHUTDOWN_OUTPUT	0x02	/**< Output shut down */
#define IOH_FLAGS_SHUTDOWN_OUTPUT_PEND	0x04	/**< Output shut down */
#define IOH_FLAGS_SHUTDOWN_CLOSING	0x08	/**< Attempting to close */
#define IOH_FLAGS_SHUTDOWN_DESTROYING	0x10	/**< Attempting to destroy */
};



/*
 * Static functions
 */
static void ioh_runhandler(bk_s B, struct bk_run *run, u_int fd, u_int gottypes, void *opaque, struct timeval starttime);



/**
 * Create and initialize the ioh environment.
 *	@param B BAKA thread/global state 
 *	@param fdin The file descriptor to read from.  -1 if no input is desired.
 *	@param fdout The file descriptor to write to.  -1 if no output is desired.
 *	@param readfun The function to use to read data.
 *	@param writefun The function to use to write data.
 *	@param handler The user callback to notify on complete I/O or other events
 *	@param opaque The opaque data for the user callback.
 *	@param inbufhint A hint for the input routines (0 for 128 bytes)
 *	@param inbufmax The maximum buffer size of incomplete data (0 for unlimited)
 *	@param outbufmax The maximum amount of data queued for transmission (0 for unlimited)
 *	@param run The bk run environment to use with the fd.
 *	@param flags The type of data on the file descriptors.
 *	@return NULL on call failure, allocation failure, or other fatal error.
 *	@return The initialized ioh structure if successful.
 */
struct bk_ioh *bk_ioh_init(bk_s B, int fdin, int fdout, bk_iofunc readfun, bk_iofunc writefun, bk_iohhandler handler, void *opaque, u_int32_t inbufhint, u_int32_t inbufmax, u_int32_t outbufmax, struct bk_run *run, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ioh *curioh = NULL;
  int tmp;
 
  if ((fdin < 0 && fdout < 0) || (fdin < 0 && !readfun) || (fdout < 0 && !writefun) || !handler || !run)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }


  // Check for invalid flags combinations
  tmp = 0;
  if (BK_FLAG_ISSET(flags, BK_IOH_RAW)) tmp++;
  if (BK_FLAG_ISSET(flags, BK_IOH_BLOCKED)) tmp++;
  if (BK_FLAG_ISSET(flags, BK_IOH_VECTORED)) tmp++;
  if (BK_FLAG_ISSET(flags, BK_IOH_LINE)) tmp++;

  if (tmp != 1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Must have one parse method in flags (RAW/BLOCKED/VECTORED/LINE)\n");
    BK_RETURN(B, NULL);
  }


  // Check for invalid flags combinations
  tmp = 0;
  if (BK_FLAG_ISSET(flags, BK_IOH_STREAM)) tmp++;

  if (tmp != 1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Must have one input type in flags (STREAM)\n");
    BK_RETURN(B, NULL);
  }

  if (!(curioh = malloc(sizeof(*curioh))))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate ioh structure: %s\n",strerror(errno));
    BK_RETURN(B, NULL);
  }
  memset(curioh, 0, sizeof(*curioh));

  curioh->ioh_fdin = fdin;
  curioh->ioh_fdout = fdout;
  curioh->ioh_readfun = readfun;
  curioh->ioh_writefun = writefun;
  curioh->ioh_handler = handler;
  curioh->ioh_opaque = opaque;
  curioh->ioh_inbuf_hint = inbufhint;
  curioh->ioh_inbuf_max = inbufmax;
  curioh->ioh_outbuf_max = outbufmax;
  curioh->ioh_run = run;
  curioh->ioh_extflags = flags;

  if (curioh->ioh_fdin >= 0)
  {
    if (bk_run_handle(B, curioh->ioh_run, curioh->ioh_fdin, ioh_runhandler, curioh, BK_RUN_WANTREAD, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not put this new read ioh into the bk_run environment\n");
      goto error;
    }
  }

  if (curioh->ioh_fdout >= 0 && curioh->ioh_fdin != curioh->ioh_fdout)
  {
    if (bk_run_handle(B, curioh->ioh_run, curioh->ioh_fdout, ioh_runhandler, curioh, 0, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not put this new write ioh into the bk_run environment\n");
      goto error;
    }
  }

  BK_RETURN(B, curioh);
  
 error:
  if (curioh)
  {
    if (curioh->ioh_fdin >= 0)
      bk_run_close(B, curioh->ioh_run, curioh->ioh_fdin, 0);
    if (curioh->ioh_fdout >= 0 && curioh->ioh_fdin != curioh->ioh_fdout)
      bk_run_close(B, curioh->ioh_run, curioh->ioh_fdout, 0);
    free(curioh);
  }
  BK_RETURN(B, NULL);
}



/**
 * Update various configuration parameters of a IOH instance
 *	@param B BAKA thread/global state 
 *	@param ioh The IOH environment to update
 *	@param readfun The function to use to read data.
 *	@param writefun The function to use to write data.
 *	@param handler The user callback to notify on complete I/O or other events
 *	@param opaque The opaque data for the user callback.
 *	@param inbufhint A hint for the input routines (0 for 128 bytes)
 *	@param inbufmax The maximum buffer size of incomplete data (0 for unlimited)
 *	@param outbufmax The maximum amount of data queued for transmission (0 for unlimited)
 *	       -- lowered maximum will not affect previously queued data
 *	@param flags The type of data on the file descriptors.
 *	       -- not all (any?) flags changes will take affect or will have a positive effect--handle with care
 *	@return -1 on call failure.
 *	@return 0 on success.
 */
int bk_ioh_update(bk_s B, struct bk_ioh *ioh, bk_iofunc readfun, bk_iofunc writefun, int (*handler)(bk_vptr *data, void *opaque, struct bk_ioh *ioh, u_int state_flags), void *opaque, u_int32_t inbufhint, u_int32_t inbufmax, u_int32_t outbufmax, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ioh *curioh = NULL;
  int tmp;
 
  if (!ioh || (ioh->ioh_readfun && !readfun) || (ioh->ioh_writefun && !writefun) || !handler)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  curioh->ioh_readfun = readfun;
  curioh->ioh_writefun = writefun;
  curioh->ioh_handler = handler;
  curioh->ioh_opaque = opaque;
  curioh->ioh_inbuf_hint = inbufhint;
  curioh->ioh_inbuf_max = inbufmax;
  curioh->ioh_outbuf_max = outbufmax;
  curioh->ioh_extflags = flags;

  BK_RETURN(B, 0);
}



/**
 * Request various configuration parameters of a IOH instance
 *	@param B BAKA thread/global state 
 *	@param ioh The IOH environment to update
 *	@param fdin The file descriptor to read from.  -1 if no input is desired.
 *	@param fdout The file descriptor to write to.  -1 if no output is desired.
 *	@param readfun The function to use to read data.
 *	@param writefun The function to use to write data.
 *	@param handler The user callback to notify on complete I/O or other events
 *	@param opaque The opaque data for the user callback.
 *	@param inbufhint A hint for the input routines (0 for 128 bytes)
 *	@param inbufmax The maximum buffer size of incomplete data (0 for unlimited)
 *	@param outbufmax The maximum amount of data queued for transmission (0 for unlimited)
 *	@param run The bk run environment to use with the fd.
 *	@param flags The type of data on the file descriptors.
 *	@return -1 on call failure.
 *	@return 0 on success.
 */
int bk_ioh_get(bk_s B, struct bk_ioh *ioh, int *fdin, int *fdout, bk_iofunc *readfun, bk_iofunc *writefun, int (**handler)(bk_vptr *data, void *opaque, struct bk_ioh *ioh, u_int state_flags), void **opaque, u_int32_t *inbufhint, u_int32_t *inbufmax, u_int32_t *outbufmax, struct bk_run **run, bk_flags *flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ioh *curioh = NULL;
  int tmp;
 
  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (fdin) *fdin = ioh->ioh_fdin;
  if (fdout) *fdout = ioh->ioh_fdout;
  if (readfun) *readfun = ioh->ioh_readfun;
  if (writefun) *writefun = ioh->ioh_writefun;
  if (handler) *handler = ioh->ioh_handler;
  if (opaque) *opaque = ioh->ioh_opaque;
  if (inbufhint) *inbufhint = ioh->ioh_inbuf_hint;
  if (inbufmax) *inbufmax = ioh->ioh_inbuf_max;
  if (outbufmax) *outbufmax = ioh->ioh_outbuf_max;
  if (run) *run = ioh->ioh_run;
  if (flags) *flags = ioh->ioh_extflags;

  BK_RETURN(B, 0);
}



/**
 * Enqueue data for output, if allowed.
 *	@param B BAKA thread/global state 
 *	@param ioh The IOH environment to update
 *	@param data The data to be sent
 *	@param flags Future expansion
 *	@return -1 on call failure or subsystem refusal
 *	@return 0 on success
 */
int bk_ioh_write(bk_s B, struct bk_ioh *ioh, bk_vptr *data, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  u_int pref;
 
  if (!ioh || !data)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  /* XXX */

  BK_RETURN(B, 0);
}



/**
 * Tell the system that no futher system I/O will be permitted.
 * If data is currently queued for input, this data will sent to the user as incomplete.
 * If data is currented queued for output, this data will be drained before the shutdown takes effect.
 * See bk_ioh_flush for a way to avoid this.
 *	@param B BAKA thread/global state 
 *	@param ioh The IOH environment to update
 *	@param how -- SHUT_RD to shut down reads, SHUT_WR to shut down writes, SHUT_RDWR for both.
 *	@param flags Future expansion
 *	@return -1 on call failure or subsystem refusal
 *	@return 0 on success
 */
void bk_ioh_shutdown(bk_s B, struct bk_ioh *ioh, int how, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  u_int pref;
 
  if (!ioh || (how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR))
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  /* XXX */

  BK_VRETURN(B);
}



/**
 * Flush data queued in the ioh.
 * Data may be queued for input or output.
 *	@param B BAKA thread/global state 
 *	@param ioh The IOH environment to update
 *	@param how -- SHUT_RD to flush input, SHUT_WR to flush output, SHUT_RDWR for both.
 *	@param flags Future expansion
 *	@return -1 on call failure or subsystem refusal
 *	@return 0 on success
 */
void bk_ioh_flush(bk_s B, struct bk_ioh *ioh, int how, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  u_int pref;
 
  if (!ioh || (how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR))
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  /* XXX */

  BK_VRETURN(B);
}



/**
 * Control whether or not new input will be accepted from the system.
 * Note there may be incomplete data on the input queue which will remain pending.
 *	@param B BAKA thread/global state 
 *	@param ioh The IOH environment to update
 *	@param isallowed Zero if no reads desired, non-zero if reads allowed
 *	@param flags Future expansion
 *	@return -1 on call failure or subsystem refusal
 *	@return 0 on success
 */
void bk_ioh_readallowed(bk_s B, struct bk_ioh *ioh, int isallowed, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  u_int pref;
 
  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (ioh->ioh_fdin < 0 || BK_FLAG_ISSET(ioh->ioh_intflags, IOH_FLAGS_SHUTDOWN_INPUT))
  {
    bk_error_printf(B, BK_ERR_WARN, "You cannot manipulate the read desireability if input is technically impossible\n");
    BK_VRETURN(B);
  }

  // Get old preference to preserve write/xcpt
  if ((pref = bk_run_getpref(B, ioh->ioh_run, ioh->ioh_fdin, 0)) == (u_int)-1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Cannot obtain I/O preferences\n");
    BK_VRETURN(B);
  }

  pref &= ~BK_RUN_WANTREAD;
  pref |= isallowed?BK_RUN_WANTREAD:0;

  // Set new preference
  if (bk_run_setpref(B, ioh->ioh_run, ioh->ioh_fdin, pref, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Cannot set I/O preferences\n");
    BK_VRETURN(B);
  }

  BK_VRETURN(B);
}



/**
 * Indicate that no further activity is desired on this ioh.
 * The ioh may linger if data requires draining, unless abort.
 * Incomplete data pending on the input queue will be sent to user as incomplete unless abort.
 * Additional callbacks may occur--WRITECOMPLETEs or IOHWRITEERRORs
 *  (if no abort), WRITEABORTEDs (if abort), IOHCLOSING (if
 *  NOTIFYANYWAY)
 *	@param B BAKA thread/global state 
 *	@param ioh The IOH environment to update
 *	@param flags Future expansion
 */
void bk_ioh_close(bk_s B, struct bk_ioh *ioh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  u_int pref;
 
  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  /* XXX */

  BK_VRETURN(B);
}



/**
 * Indicate that no further activity is desired on this ioh.
 * All queued data will be flushed (use _close instead).
 * User data queued on system probably will not be freed (use _close instead).
 * No user notification (use _close instead).
 * Did we mention you should use _close instead of this interface?
 *	@param B BAKA thread/global state 
 *	@param ioh The IOH environment to update
 */
void bk_ioh_destroy(bk_s B, struct bk_ioh *ioh)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  u_int pref;
 
  if (!ioh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  /* XXX */

  BK_VRETURN(B);
}



/*
 * Run's interface into the IOH
 */
static void ioh_runhandler(bk_s B, struct bk_run *run, u_int fd, u_int gottypes, void *opaque, struct timeval starttime)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ioh *ioh = opaque;
 
  /* XXX */

  BK_VRETURN(B);
}
