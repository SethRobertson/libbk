#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_relay.c,v 1.13 2002/01/21 03:11:08 seth Exp $";
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
 * Provide a generic IOH relay
 */

#include <libbk.h>
#include "libbk_internal.h"



/**
 * All information about an IOH relay.
 */
struct bk_relay
{
  struct bk_ioh        *br_ioh1;		///< One of the IOHs
  bk_flags		br_ioh1_state;		///< State of one IOH
#define BR_IOH_READCLOSE	0x1		///< Read side is no longer available
#define BR_IOH_CLOSED		0x2		///< Entire IOH is no longer available
#define BR_IOH_THROTTLED	0x4		///< Read is throttled because of output queue size
  struct bk_ioh        *br_ioh2;		///< Another of the IOHs
  bk_flags		br_ioh2_state;		///< State of one IOH
  void 		      (*br_donecb)(bk_s, void *, u_int); ///< Completion callback
  void		       *br_opaque;		///< Opaque data for callback
  bk_flags		br_flags;		///< State
};



static void bk_relay_iohhandler(bk_s B, bk_vptr data[], void *opaque, struct bk_ioh *ioh, u_int state_flags);




/**
 * Create and start a relay, which will terminate when EOF or error
 * has been received on both sides.  Callback will be called when all
 * is done, but IOHs and fds will be closed at that point.  No
 * interface or modification to the relay will be allowed after this
 * point.  Perhaps an FD interface would be useful which would create
 * the IOHs.  Whatever.
 *
 *	@param B BAKA Thread/global state
 *	@param ioh1 One side of the IOH relay
 *	@param ioh2 Other side of the IOH relay
 *	@param donecb Function to call when relay has termianted
 *	@param opaque Data for function
 *	@param flags Fun for the future
 *
 *	@return <i>-1</i> Call failure, allocation failure, other failure
 *	@return <br><i>0</i> on success
 */
int bk_relay_ioh(bk_s B, struct bk_ioh *ioh1, struct bk_ioh *ioh2, void (*donecb)(bk_s B, void *opaque, u_int state), void *opaque, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"libbk");
  struct bk_relay *relay;

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
  relay->br_donecb = donecb;
  relay->br_opaque = opaque;

  // Ensure that reading is allowed
  bk_ioh_readallowed(B, ioh1, 1, 0);
  bk_ioh_readallowed(B, ioh2, 1, 0);

  //<TODO> Alter interface to permit caller to specify hints </TODO>
  //<TODO> Allow the caller to specify a callback which gets called in each read/write 

  if (bk_ioh_update(B, ioh1, NULL, NULL, NULL, bk_relay_iohhandler, relay, 0, 0, 0, 0, BK_IOH_UPDATE_HANDLER|BK_IOH_UPDATE_OPAQUE) < 0)
    goto error;
  if (bk_ioh_update(B, ioh2, NULL, NULL, NULL, bk_relay_iohhandler, relay, 0, 0, 0, 0, BK_IOH_UPDATE_HANDLER|BK_IOH_UPDATE_OPAQUE) < 0)
    goto error;

  BK_RETURN(B, 0);

 error:
  bk_error_printf(B, BK_ERR_ERR, "Error during ioh get/updates\n");
  free(relay);
  BK_RETURN(B, -1);
}



/**
 * IOH handler which actually performs the relay
 *
 *	@param B BAKA Thread/global state
 *	@param data List of data to be relayed
 *	@param opaque Callback data
 *	@param ioh IOH data/activity came in on
 *	@param state_flags Type of activity
 */
static void bk_relay_iohhandler(bk_s B, bk_vptr data[], void *opaque, struct bk_ioh *ioh, bk_ioh_status_e state_flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"libbk");
  struct bk_ioh *ioh_other;
  bk_flags *state_me, *state_him;
  struct bk_relay *relay = opaque;
  u_int cnt, size = 0;
  bk_vptr *newcopy = NULL;
  int ret;

  if (!ioh || !relay)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  if (relay->br_ioh1 == ioh)
  {
    ioh_other = relay->br_ioh2;
    state_me = &relay->br_ioh1_state;
    state_him = &relay->br_ioh2_state;
  }
  else
  {						// We are just assuming here the other matches
    ioh_other = relay->br_ioh1;
    state_me = &relay->br_ioh2_state;
    state_him = &relay->br_ioh1_state;
  }

  switch (state_flags)
  {
  case BkIohStatusIncompleteRead:
  case BkIohStatusReadComplete:
    // Coalesce into one buffer for output
    if (!(newcopy = bk_ioh_coalesce(B, data, NUL, 0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not coalesce relay data\n");
      goto error;
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
    bk_ioh_shutdown(B, ioh_other, SHUT_WR, 0);
    bk_debug_printf_and(B, 1, "Received read error or EOF.  My state %x, his state %x\n",*state_me,*state_him);
    break;

  case BkIohStatusIohWriteError:
    // Propagate shutdown to read side of peer
    bk_debug_printf_and(B, 1, "Received write error msg.\n");
    BK_FLAG_SET(*state_him, BR_IOH_READCLOSE);
    bk_ioh_shutdown(B, ioh_other, SHUT_RD, 0);
    bk_debug_printf_and(B, 1, "Write error msg.  My state %x, his state %x\n",*state_me,*state_him);
    break;

  case BkIohStatusWriteComplete:
  case BkIohStatusWriteAborted:
    // Guarenteed just one buffer
    free(data[0].ptr);
    free(data);

    // Now that some data has drained...
    if (BK_FLAG_ISSET(*state_him, BR_IOH_THROTTLED))
    {
      bk_debug_printf_and(B, 1, "Clearing throttle.  My state %x, his state %x\n",*state_me,*state_him);
      BK_FLAG_CLEAR(*state_him, BR_IOH_THROTTLED);
      bk_ioh_readallowed(B, ioh_other, 1, 0);
    }
    break;

  case BkIohStatusIohClosing:
    BK_FLAG_SET(*state_me, BR_IOH_CLOSED);
    if (BK_FLAG_ISCLEAR(*state_me, BR_IOH_READCLOSE) ||
	BK_FLAG_ISCLEAR(*state_him, BR_IOH_READCLOSE))
    {						// Other party may not be aware of severity yet. Close him anyway.
      BK_FLAG_SET(*state_me, BR_IOH_READCLOSE);
      BK_FLAG_SET(*state_him, BR_IOH_READCLOSE);
      if (BK_FLAG_ISCLEAR(*state_him, BR_IOH_CLOSED))
	bk_ioh_close(B, ioh_other, 0);
    }
    bk_debug_printf_and(B, 1, "Received ioh close notification.  My state %x, his state %x\n",*state_me,*state_him);
    break;

  case BkIohStatusIohSeekSuccess:
  case BkIohStatusIohSeekFailed:
    bk_error_printf(B, BK_ERR_ERR, "I got seek notification. How could this happen\n");
    BK_VRETURN(B);

    // No default here so that compiler can catch missed state
  }

  if (0)					// Stupid method of getting error case to continue execution
  {
  error:
    BK_FLAG_SET(*state_me, BR_IOH_READCLOSE);
    bk_ioh_shutdown(B, ioh, SHUT_RD, 0);
    bk_ioh_shutdown(B, ioh_other, SHUT_WR, 0);
  }

  if (BK_FLAG_ISSET(*state_me, BR_IOH_CLOSED) &&
      BK_FLAG_ISSET(*state_him, BR_IOH_CLOSED))
  {
    // Both sides closed, dry up and go away
    bk_debug_printf_and(B, 1, "Both sides seem to have closed--drying up\n");
    (*relay->br_donecb)(B, relay->br_opaque, 0);
    free(relay);
    BK_VRETURN(B);
  }

  // Check if both sides have read shut down, but neither has closed (e.g. we are not already in close-wait)
  if (BK_FLAG_ISSET(*state_me, BR_IOH_READCLOSE) &&
      BK_FLAG_ISSET(*state_him, BR_IOH_READCLOSE) &&
      BK_FLAG_ISCLEAR(*state_me, BR_IOH_CLOSED) &&
      BK_FLAG_ISCLEAR(*state_him, BR_IOH_CLOSED))
  {
    // Both sides have read gone--start cleanup process
    bk_debug_printf_and(B, 1, "Both sides seem to have read issues--closing\n");
    bk_ioh_close(B, ioh, 0);
    if (BK_FLAG_ISCLEAR(*state_him, BR_IOH_CLOSED))
      bk_ioh_close(B, ioh_other, 0);
  }

  BK_VRETURN(B);
}

