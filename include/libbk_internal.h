/*
 * $Id: libbk_internal.h,v 1.24 2001/12/19 20:21:02 jtt Exp $
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
 * Internal information required between libbk files which should NOT
 * be used by those outside libbk.
 */

#ifndef _libbk_internal_h_
#define _libbk_internal_h_
#include <libbk_i18n.h>


#define SCRATCHLEN (MAX(MAXPATHLEN,1024))
#define SCRATCHLEN2 ((SCRATCHLEN)-100)

/**
 * Information about a queue of data pending for I/O
 */
struct bk_ioh_queue
{
  u_int32_t		biq_queuelen;		///< Amount of non-consumed data current in queue
  u_int32_t		biq_queuemax;		///< Maximum of non-consumed data storable queue
  dict_h		biq_queue;		///< Queued data
  union
  {
    struct
    {
      u_int32_t 	remaining;		///< Number of bytes in previous complete block remaining
      bk_flags		flags;			///< Private flags
    }			block;			///< Block message types
    /* Space for future private info of additional message types */
  }			biq;			///< Private data for message types
};



/**
 * Information about a specific ioh.
 */
struct bk_ioh
{
  int			ioh_fdin;		///< Input file descriptor
  u_int32_t		ioh_fdin_savestate;	///< Information about fdin which we changed
#define IOH_ADDED_NONBLOCK	0x01		///< O_NONBLOCK was added to the fd
#define IOH_ADDED_OOBINLINE	0x02		///< SO_OOBINLINE was added to the fd
#define IOH_NUKED_LINGER	0x04		///< SO_LINGER was deleted from the fd
  int			ioh_fdout;		///< Output file descriptor
  u_int32_t		ioh_fdout_savestate;	///< Information about fdout which we changed
  bk_iorfunc 		ioh_readfun;		///< Function to read data
  bk_iowfunc		ioh_writefun;		///< Function to write data
  void		       *ioh_iofunopaque;	///< Opaque data for iofuns
  bk_iohhandler_f	ioh_handler;		///< Callback function for event handling
  void		       *ioh_opaque;		///< Opaque data for handler
  u_int32_t		ioh_inbuf_hint;		///< Hint for input buffer sizes
  struct bk_run	       *ioh_run;		///< BK_RUN environment
  struct bk_ioh_queue	ioh_readq;		///< Data queued for reading
  struct bk_ioh_queue	ioh_writeq;		///< Data queued for writing
  char			ioh_eolchar;		///< End of line character
  bk_flags		ioh_deferredclosearg;	///< Flags argument to deferred close
  int			ioh_throttle_cnt;	///< How many people want to block reads.
  bk_flags		ioh_extflags;		///< Flags--see libbk.h
  bk_flags		ioh_intflags;		///< Flags
#define IOH_FLAGS_SHUTDOWN_INPUT	0x01	///< Input shut down
#define IOH_FLAGS_SHUTDOWN_OUTPUT	0x02	///< Output shut down
#define IOH_FLAGS_SHUTDOWN_OUTPUT_PEND	0x04	///< Output shut down is deferred
#define IOH_FLAGS_SHUTDOWN_CLOSING	0x08	///< Attempting to close
#define IOH_FLAGS_SHUTDOWN_DESTROYING	0x10	///< Attempting to destroy
#define IOH_FLAGS_DONTCLOSEFDS		0x40	///< Don't close FDs on death
#define IOH_FLAGS_ERROR_INPUT		0x80	///< Input had I/O error or EOF
#define IOH_FLAGS_ERROR_OUTPUT		0x100	///< Output had I/O error
#define IOH_FLAGS_CLOSE_PENDING		0x200	///< We want to close, but are in a user callback
#define IOH_FLAGS_IN_CALLBACK		0x400	///< We are in a user callback
};




/**
 * All the state which the on demand I/O  subsytem requires.
 */
struct bk_polling_io
{
  bk_flags		bpi_flags;		///< Everyone needs flags.
#define BPI_FLAG_CLOSING		0x1	///< We are closing down bpi.
#define BPI_FLAG_READ_DEAD		0x2	///< Our read side is in permement ruins.
#define BPI_FLAG_WRITE_DEAD		0x4	///< Our write side is in permement ruins.
#define BPI_FLAG_SAW_EOF		0x8	///< We have seen EOF.
#define BPI_FLAG_DONT_DESTROY		0x10	///< Tell io hander not destroy bpi.
#define BPI_FLAG_IOH_DEAD		0x20	///< Tell io hander not destroy bpi.
  dict_h		bpi_data;		///< Queue of data vptrs.
  struct bk_ioh *	bpi_ioh;		///< Ioh structure.
  u_int			bpi_size;		///< Amount of data I'm buffering.
  u_int			bpi_throttle_cnt;	///< Count the number of people who want to throttle me.
  quad_t		bpi_tell;		///< Where we are in the stream.
};





/* FRIENDLY FUNCTIONS */

extern bk_s bk_general_thread_init(bk_s B, char *name);
extern void bk_general_thread_destroy(bk_s B);

extern void bk_run_signal_ihandler(int signum);


#endif /* _libbk_internal_h_ */
