#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2001-2008";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2001-2008 The Authors. All rights reserved.
 *
 * This source code is licensed to you under the terms of the file
 * LICENSE.TXT in this release for further details.
 *
 * Send e-mail to <projectbaka@baka.org> for further information.
 *
 * - -Copyright BAKA- -
 */

/**
 * @file
 * Provide a generic IOH relay
 */

#include <libbk.h>
#include "libbk_internal.h"


/**
 * All information about an IOH relay.
 */
struct bk_relay
{
  struct bk_ioh*	br_ioh1;		///< One of the IOHs
  bk_flags		br_ioh1_state;		///< State of one IOH
#define BR_IOH_READCLOSE	0x1		///< Read side is no longer available
#define BR_IOH_CLOSED		0x2		///< Entire IOH is no longer available
#define BR_IOH_THROTTLED	0x4		///< Read is throttled because of output queue size
#define BR_IOH_SEIZEOK		0x8		///< Carpe Data
  struct bk_ioh *	br_ioh2;		///< Another of the IOHs
  bk_flags		br_ioh2_state;		///< State of one IOH
  u_int32_t		br_ioh1_max;		///< Maximum size of ioh1 buffer
  u_int32_t		br_ioh2_max;		///< Maximum size of ioh1 buffer
  bk_relay_cb_f		br_callback;		///< Callback on reads and shutdown
  void		       *br_opaque;		///< Opaque data for callback
  struct bk_relay_ioh_stats *br_stats;		///< Optional statistics about relay
  bk_flags		br_flags;		///< State
  struct bk_relay_cancel *br_brc;			///< Pointer to use cancel structure.
};



static void bk_relay_iohhandler(bk_s B, bk_vptr *data, void *opaque, struct bk_ioh *ioh, u_int state_flags);


#define BK_RELAY_CANCEL_FLAG_SHUTODWN	0x1	///< Relay is shutdown don't do anything.


/**
 * Create and start a relay, which will terminate when EOF or error
 * has been received on both sides.  Callback will be called when all
 * is done, but IOHs and fds will be closed at that point.  No
 * interface or modification to the relay will be allowed after this
 * point.  Perhaps an FD interface would be useful which would create
 * the IOHs.  Whatever.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state
 *	@param ioh1 One side of the IOH relay
 *	@param ioh2 Other side of the IOH relay
 *	@param callback Function to call on reads and shutdown
 *	@param opaque Data for function
 *	@param stats Optional statistics about I/O
 *	@param brcp Optional copyout structure for calling bk_relay_cancel.
 *	@param flags BK_RELAY_IOH_DONE_AFTER_ONE_CLOSE BK_RELAY_IOH_DONTCLOSEFDS
 *
 *	@return <i>-1</i> Call failure, allocation failure, other failure
 *	@return <br><i>0</i> on success
 */
int bk_relay_ioh(bk_s B, struct bk_ioh *ioh1, struct bk_ioh *ioh2, bk_relay_cb_f callback, void *opaque, struct bk_relay_ioh_stats *stats, struct bk_relay_cancel *brc, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"libbk");
  struct bk_relay *relay;
  bk_flags flags1, flags2;

  if (!ioh1 || !ioh2)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, -1);
  }

  if (!BK_CALLOC(relay))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate storage for relay data: %s\n",strerror(errno));
    BK_RETURN(B, -1);
  }

  // Configure the IOHs
  relay->br_ioh1 = ioh1;
  relay->br_ioh2 = ioh2;
  relay->br_callback = callback;
  relay->br_opaque = opaque;
  relay->br_stats = stats;
  relay->br_flags = flags;


  if (relay->br_stats)
    memset(relay->br_stats, 0, sizeof(*relay->br_stats));

  // Ensure that reading is allowed
  bk_ioh_readallowed(B, ioh1, 1, 0);
  bk_ioh_readallowed(B, ioh2, 1, 0);

  //<TODO> Alter interface to permit caller to specify hints (though they can specify them in the IOH up front) </TODO>

  if (bk_ioh_get(B, ioh1, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &relay->br_ioh1_max, NULL, &flags1) < 0)
    goto error;
  if (bk_ioh_get(B, ioh2, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &relay->br_ioh2_max, NULL, &flags2) < 0)
    goto error;

  if (BK_FLAG_ISCLEAR(flags1, BK_IOH_LINE) && BK_FLAG_ISCLEAR(flags2, BK_IOH_LINE))
    BK_FLAG_SET(relay->br_flags, BR_IOH_SEIZEOK);

  if (bk_ioh_update(B, ioh1, NULL, NULL, NULL, NULL, bk_relay_iohhandler, relay, 0, 0, 0, 0, BK_IOH_UPDATE_HANDLER|BK_IOH_UPDATE_OPAQUE) < 0)
    goto error;
  if (bk_ioh_update(B, ioh2, NULL, NULL, NULL, NULL, bk_relay_iohhandler, relay, 0, 0, 0, 0, BK_IOH_UPDATE_HANDLER|BK_IOH_UPDATE_OPAQUE) < 0)
    goto error;

  if (brc)
  {
    brc->brc_flags = 0;
    brc->brc_ioh1 = ioh1;
    brc->brc_ioh2 = ioh2;
    brc->brc_opaque = relay;
    relay->br_brc = brc;
  }

  // ioh_update might have destroyed ioh's, but handler will have dealt with it

  BK_RETURN(B, 0);

 error:
  bk_error_printf(B, BK_ERR_ERR, "Error during ioh get/updates\n");
  free(relay);
  BK_RETURN(B, -1);
}



/**
 * IOH handler which actually performs the relay
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state
 *	@param data List of data to be relayed
 *	@param opaque Callback data
 *	@param ioh IOH data/activity came in on
 *	@param state_flags Type of activity
 */
static void bk_relay_iohhandler(bk_s B, bk_vptr *data, void *opaque, struct bk_ioh *ioh, bk_ioh_status_e state_flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"libbk");
  struct bk_ioh *ioh_other, **ioh_mep;
  bk_flags *state_me, *state_him;
  struct bk_relay *relay = opaque;
  bk_vptr *newcopy = NULL;
  int ret;
  int side;

  if (!ioh || !relay)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  if (relay->br_ioh1 == ioh)
  {
    ioh_mep = &relay->br_ioh1;
    ioh_other = relay->br_ioh2;
    state_me = &relay->br_ioh1_state;
    state_him = &relay->br_ioh2_state;
    side = 0;
  }
  else
  {						// We are just assuming here the other matches
    ioh_mep = &relay->br_ioh2;
    ioh_other = relay->br_ioh1;
    state_me = &relay->br_ioh2_state;
    state_him = &relay->br_ioh1_state;
    side = 1;
  }

  switch (state_flags)
  {
  case BkIohStatusIncompleteRead:
  case BkIohStatusReadComplete:
    // Coalesce into one buffer for output
    if (!data[1].ptr && BK_FLAG_ISSET(relay->br_flags, BR_IOH_SEIZEOK))
    {
      // Only one buffer, let's seize it!!
      if (!BK_MALLOC(newcopy))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not allocate for seize: %s\n", strerror(errno));
	goto error;
      }
      *newcopy = data[0];
      data[0].ptr = NULL;
    }
    else
    {
      if (!(newcopy = bk_ioh_coalesce(B, data, NULL, BK_IOH_COALESCE_FLAG_MUST_COPY, NULL)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not coalesce relay data\n");
	goto error;
      }
    }

    if (relay->br_stats)
    {
      relay->br_stats->side[side].birs_readbytes += newcopy->len;
      relay->br_stats->side[side].birs_ioh_ops++;
    }

    bk_debug_printf_and(B,128,"Reading data on descriptor pair (%d:%d)\n", ioh->ioh_fdin, ioh->ioh_fdout);

    if (relay->br_callback)
    {
      bk_debug_printf_and(B,64,"Making relay callback\n");
      (*relay->br_callback)(B, relay->br_opaque, ioh, ioh_other, newcopy, 0);
    }

    if ((ret = bk_ioh_write(B, ioh_other, newcopy, 0)) != 0)
    {
      if (ret > 0)
      {						// IOH output queue has filled
	// Turn off reads, write into queue anyway
	BK_FLAG_SET(*state_me, BR_IOH_THROTTLED);
	bk_ioh_readallowed(B, ioh, 0, 0);
	bk_debug_printf_and(B, 1, "Throttling input due to return %d.  My state %x, his state %x\n",ret,*state_me,*state_him);
	ret = bk_ioh_write(B, ioh_other, newcopy, BK_IOH_BYPASSQUEUEFULL);
	if (relay->br_stats)
	  relay->br_stats->side[side].birs_stalls++;
      }
      if (ret < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not write data to output ioh\n");
	goto error;
      }
    }
    break;

  case BkIohStatusIohReadError:
  case BkIohStatusIohReadEOF:
    // Propagate shutdown to write side of peer
    BK_FLAG_SET(*state_me, BR_IOH_READCLOSE);
    if (BK_FLAG_ISCLEAR(relay->br_flags, BK_RELAY_IOH_NOSHUTDOWN))
      bk_ioh_shutdown(B, ioh_other, SHUT_WR, 0);
    bk_debug_printf_and(B, 1, "Received read error or EOF.  My state %x, his state %x\n",*state_me,*state_him);
    break;

  case BkIohStatusIohWriteError:
    // Propagate shutdown to read side of peer
    bk_debug_printf_and(B, 1, "Received write error msg.\n");
    BK_FLAG_SET(*state_him, BR_IOH_READCLOSE);
    if (BK_FLAG_ISCLEAR(relay->br_flags, BK_RELAY_IOH_NOSHUTDOWN))
      bk_ioh_shutdown(B, ioh_other, SHUT_RD, 0);
    bk_debug_printf_and(B, 1, "Write error msg.  My state %x, his state %x\n",*state_me,*state_him);
    break;

  case BkIohStatusWriteComplete:
  case BkIohStatusWriteAborted:
    if (relay->br_stats)
    {
      relay->br_stats->side[side].birs_writebytes += data[0].len;
      relay->br_stats->side[side].birs_ioh_ops++;
    }

    // Guarenteed just one buffer
    free(data[0].ptr);
    free(data);

    // Now that some data has drained...
    if (BK_FLAG_ISSET(*state_him, BR_IOH_THROTTLED))
    {
      u_int32_t outqueue = 0;
      u_int32_t bufmax = 0;

      if (ioh == relay->br_ioh1)
	bufmax = relay->br_ioh1_max;
      else
	bufmax = relay->br_ioh2_max;

      bk_ioh_getqlen(B, ioh, NULL, &outqueue, 0);

      if (!bufmax || outqueue < bufmax)
      {
	bk_debug_printf_and(B, 1, "Clearing throttle.  My state %x, his state %x\n",*state_me,*state_him);
	BK_FLAG_CLEAR(*state_him, BR_IOH_THROTTLED);
	bk_ioh_readallowed(B, ioh_other, 1, 0);
      }
    }
    break;

  case BkIohStatusIohClosing:
    BK_FLAG_SET(*state_me, BR_IOH_CLOSED);
    *ioh_mep = NULL;
    if (BK_FLAG_ISCLEAR(*state_me, BR_IOH_READCLOSE) ||
	BK_FLAG_ISCLEAR(*state_him, BR_IOH_READCLOSE))
    {						// Other party may not be aware of severity yet. Close him anyway.
      BK_FLAG_SET(*state_me, BR_IOH_READCLOSE);
      BK_FLAG_SET(*state_him, BR_IOH_READCLOSE);
      if (BK_FLAG_ISCLEAR(*state_him, BR_IOH_CLOSED))
	bk_ioh_close(B, ioh_other, BK_FLAG_ISSET(relay->br_flags, BK_RELAY_IOH_DONTCLOSEFDS)?BK_IOH_DONTCLOSEFDS:0);
    }
    bk_debug_printf_and(B, 1, "Received ioh close notification.  My state %x, his state %x\n",*state_me,*state_him);
    break;

  case BkIohStatusIohSeekSuccess:
  case BkIohStatusIohSeekFailed:
    bk_error_printf(B, BK_ERR_ERR, "I got seek notification. How could this happen\n");
    BK_VRETURN(B);

  case BkIohStatusNoStatus:
    bk_error_printf(B, BK_ERR_ERR, "Uninitialized status\n");
    BK_VRETURN(B);

    // No default here so that compiler can catch missed state
  }

  if (0)					// Stupid method of getting error case to continue execution
  {
  error:
    BK_FLAG_SET(*state_me, BR_IOH_READCLOSE);
    if (BK_FLAG_ISCLEAR(relay->br_flags, BK_RELAY_IOH_NOSHUTDOWN))
    {
      bk_ioh_shutdown(B, ioh, SHUT_RD, 0);
      bk_ioh_shutdown(B, ioh_other, SHUT_WR, 0);
    }
  }

  if (BK_FLAG_ISSET(*state_me, BR_IOH_CLOSED) &&
      BK_FLAG_ISSET(*state_him, BR_IOH_CLOSED))
  {
    // Both sides closed, dry up and go away
    if (relay->br_brc)
      BK_FLAG_SET(relay->br_brc->brc_flags, BK_RELAY_CANCEL_FLAG_SHUTODWN);
    bk_debug_printf_and(B, 1, "Both sides seem to have closed--drying up\n");
    if (relay->br_callback)
      (*relay->br_callback)(B, relay->br_opaque, ioh, ioh_other, NULL, 0);
    free(relay);
    BK_VRETURN(B);
  }

  // Check if both sides have read shut down, but neither has closed (e.g. we are not already in close-wait)
  if ((BK_FLAG_ISSET(*state_me, BR_IOH_READCLOSE) &&
       BK_FLAG_ISSET(*state_him, BR_IOH_READCLOSE) &&
       BK_FLAG_ISCLEAR(*state_me, BR_IOH_CLOSED) &&
       BK_FLAG_ISCLEAR(*state_him, BR_IOH_CLOSED)) ||
      (BK_FLAG_ISSET(relay->br_flags, BK_RELAY_IOH_DONE_AFTER_ONE_CLOSE) &&
       ((BK_FLAG_ISSET(*state_me, BR_IOH_READCLOSE) &&
	 BK_FLAG_ISCLEAR(*state_me, BR_IOH_CLOSED)) ||
       ((BK_FLAG_ISSET(*state_him, BR_IOH_READCLOSE) &&
	 BK_FLAG_ISCLEAR(*state_him, BR_IOH_CLOSED))))))
  {
    // Both sides have read gone--start cleanup process
    bk_debug_printf_and(B, 1, "Both sides seem to have read issues--closing\n");
    bk_ioh_close(B, ioh, BK_FLAG_ISSET(relay->br_flags, BK_RELAY_IOH_DONTCLOSEFDS)?BK_IOH_DONTCLOSEFDS:0);
    if (BK_FLAG_ISCLEAR(*state_him, BR_IOH_CLOSED))
      bk_ioh_close(B, ioh_other, BK_FLAG_ISSET(relay->br_flags, BK_RELAY_IOH_DONTCLOSEFDS)?BK_IOH_DONTCLOSEFDS:0);
  }

  BK_VRETURN(B);
}



/**
 * User cancel a relay. Simulates a normal shutdown on both ioh's.
 *
 *	@param B BAKA thread/global state.
 *	@param brc The @a bk_relay_cancel structuure
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_relay_cancel(bk_s B, struct bk_relay_cancel *brc, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"libbk");

  if (!brc)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (BK_FLAG_ISSET(brc->brc_flags, BK_RELAY_CANCEL_FLAG_SHUTODWN))
    BK_RETURN(B, 0);

  bk_relay_iohhandler(B, NULL, brc->brc_opaque, brc->brc_ioh1, BkIohStatusIohReadEOF);
  bk_relay_iohhandler(B, NULL, brc->brc_opaque, brc->brc_ioh2, BkIohStatusIohReadEOF);

  BK_RETURN(B, 0);
}
