#if !defined(lint) && !defined(__INSIGHT__)
UNUSED static const char libbk__rcsid[] = "$Id: b_skid.c,v 1.7 2004/07/08 04:40:17 lindauer Exp $";
UNUSED static const char libbk__copyright[] = "Copyright (c) 2003";
UNUSED static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright LIBBK++
 *
 * Copyright (c) 2003 The Authors. All rights reserved.
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
 *
 * WARNING WARNING WARNING:  This code has never been tested and is known
 * to have bugs in it, and needs thread conversion.
 *
 * SKID3 -- Secret Key IDentification Protocol as described in
 * B. Schneier, Applied Crptography Modified to turn it into a
 * symmetric algorithm without hopefully losing any security.
 *
 * <HR>
 * The original protocol as described in Schneier
 *
 * Step		ALICE		BOB
 *
 * A		Ra	-->
 *
 *		Alice generates 64 bit random
 *		number (Ra) and sends to Bob
 *
 * B			<--	Rb H(Ra,Rb,Bob,K)
 *
 *		Bob generates 64 bit random number
 *		(Rb), computes keyed hash. Bob is Bob's
 *		name (we use source IP address).
 *		K is shared secret key.
 *
 * C		Verify
 *
 *		Alice verifies that Bob knows the secret
 *		key (K) by computing the hash herself.
 *
 * D		H(Rb,Alice,K)	-->
 *
 *		Alice computes keyed hash.  Alice is
 *		Alice's name.
 *
 * E				Verify
 *
 *		Bob verifies that Alice knows the secret
 *		key (K) by computing the hash himself.
 *
 *
 * At step C, Alice knows that Bob knows K.
 * At step E, Bob knows that Alice knows K.
 *
 * <HR>
 * Our modified protocol--modified to make it symmetric:
 *
 * Step		ALICE		BOB
 *
 * A1		MIME(Alice) " " MIME(Ra)	-->
 *
 *		Alice generates 128 bit random
 *		number (Ra) and sends to Bob
 *
 * A2			<--	MIME(Bob) " " MIME(Rb)
 *
 *		Bob generates 128 bit random number (Rb) and sends to
 *		Alice without waiting for Alice's A1 message
 *
 * Aa		Alice and Bob verify that Bob-name, Alice-name,
 *		Ra, and Rb are all different.  If any match, then
 *		Alice and/or Bob go into anti-spoof mode.
 *
 * B1		MIME(H(Ra,Rb,Alice,K)) -->
 *
 *		Alice computes MD5 keyed hash. Bob is Bob's
 *		name.  K is shared secret key.
 *
 * B2		        <--    MIME(H(Rb,Ra,Bob,K))
 *
 *		Bob computes MD5 keyed hash in slight different
 *		order. Bob is Bob's name.  K is shared secret key.
 *		without waiting for Alice's B1 message
 *
 * C		Verify
 *
 *		Each side computes the other's hash and verifies that
 *		what the other sent was correct.
 *
 * At step C, Alice and Bob knows that either other knows K.  SKID (ours and original)
 * are subject to man-in-the-middle attacks.
 *
 * Note our on-wire message format is US-ASCII printable clean and
 * is line oriented.  The on-wire message format will look like the
 * following:
 *
 *  "SKID A message\n"
 *  "SKID B message\n"
 *
 * Each side will send one "SKID A" message and (absent I/O issues)
 * one "SKID B" message.  MIME(something) is the MIME base 64 encoding
 * of the information described above (random number, hash, or name)
 * and will be of fixed size (24 bytes--what 128 bits expands to) for
 * the random number and hash.
 *
 * The SKID routine will discard non-conforming message (e.g. ones that
 * do not start with "SKID ").
 *
 * If a caller detects a protocol violation in the remote party's
 * "SKID A" message--essentially the wrong sized message, one with the
 * wrong protocol stage, the remote name not appearing the in the key
 * lookup table, or the name/random mismatch described in step
 * Aa--then the detector will silently (to the remote party) send a
 * 128 bit random number in place of the keyed hash for step "SKID B"
 * and will return failure to the user no matter what the remote party
 * sends.
 *
 * If a caller does not know about the remote user's name--it does not
 * match the remote name passed in, or is not in the database of
 * remote name pairs, then the detector will silently (to the remote
 * party) send a random number in place of the keyed hash for step
 * "SKID B"
 *
 * The skid protocol will not provide any confirmation of the success
 * of the protocol--that is left to the caller.
 */

#include <libbk.h>
#include "libbk_internal.h"


#ifdef SKIDHASBEENDEBUGGEDANDTESTED


#define BK_SKID_BITS		128		///< Number of bits of randomness and hash
#define BK_SKID_BYTES		16		///< Number of bytes of randomness and hash
#define BK_SKID_MAXMSG		256		///< Maximum message size
#define BK_SKIDA		"SKID A"	///< SKID A message prefix
#define BK_SKIDB		"SKID B"	///< SKID B message prefix
#define BK_SKIDKQ		"KEYquote-"	///< MIME encoded string
#define BK_SKIDKR		"KEY-"		///< Non-encoded string


/**
 * An enumeration defining the roles we can play (transmit, receive, etc)
 */
typedef enum
{
  SKID_START,					///< Starting state
  SKID_WAITING_SKIDA,				///< Sent SKIDA, waiting reciprocal
  SKID_WAITING_SKIDB,				///< Sent SKIDB, waiting reciprocal
  SKID_END,					///< End state
} skid_state;



/**
 * Internal SKID state
*/
struct bk_skid
{
  struct bk_ioh	       *bs_ioh;			///< Communication IOH
  bk_vptr	       *bs_key;			///< Shared secret key
  bk_vptr	       *bs_myname;		///< My name
  bk_vptr	       *bs_hisname;		///< His name
  struct bk_config     *bs_hisnamekeylist;	///< Remote name/key pairs
  bk_vptr	        bs_Rme;			///< My random number
  bk_vptr	       *bs_Rhim;		///< His random number
  bk_skid_cb		bs_done;		///< Handler
  void		       *bs_opaque;		///< Opaque data for done handler
  bk_iohhandler_f       bs_oldhandler;		///< Once and future handler of IOH
  void		       *bs_oldopaque;		///< Once and future opaque of IOH
  bk_flags	        bs_oldflags;		///< Once and future flags of IOH
  skid_state		bs_state;		///< The state of the protocol
  struct bk_randinfo   *bs_randinfo;		///< Random state
  const char	       *bs_failure;		///< The protocol has failed, for this reason
#define SFAIL_IO "IO error"			///< Failure reason: IOH error
#define SFAIL_PROTOCOL "Protocol failure"	///< Failure reason: Protocol failure
#define SFAIL_SYSTEM "System failure"		///< Failure reason: System failure
#define SFAIL_UNKNOWN "Unknown remote party"	///< Failure reason: Unknown remote party
#define SFAIL_BADAUTH "Authentication failed"	///< Failure reason: Authentication failed
#define SFAIL_BADUSER "Remote user not found"	///< Failure reason: Remote user bad
  bk_flags		bs_flags;		///< Flags are fun
#define BK_SKID_IOH_CLOSED	0x01		///< IOH has gone away
#define BK_SKID_ISBAD		0x02		///< Bad--tell user to fail
#define BK_SKID_FREEKEY		0x04		///< Key has been allocated
};



static void skid_iohhandler(bk_s B, bk_vptr data[], void *opaque, struct bk_ioh *ioh, bk_ioh_status_e state_flags);
static void skid_status(bk_s B, struct bk_skid *bs, const char *str);



/**
 * Authentication an IOH using SKID
 *
 * Note the IOH must be in line or vectored mode.  We will set and
 * restore the mode the handlers, and everything else.  However, the
 * IOH mode transition is not necessarily very clean (data loss or worse
 * may occur) so be aware of this potential problem.
 *
 *	@param B BAKA Global/thread state
 *	@param ioh The ioh to the remote party to authenticate
 *	@param myname The name we are representing ourselves as--eight bit acceptable
 *	@param hisname The name we require he have--NULL to use keylist--eight bit acceptable
 *	@param key The default secret key crypto material if not in keylist--eight bit OK
 *	@param hisnamekeylist The list of valid remote names as keys,
 *		crypto material as values.  Possible formats of key
 *		"SKID-plaintextremotename"
 *		Possible formats of value: "KEY-plaintextkey"
 *		"KEYquote-".quote(key). Where quote is defined as bk_string_quote()
 *		quoting BK_STRING_QUOTE_NONPRINT, space, and equals.
 *	@param done The callback we will run when we have authenticated or not
 *	@param opaque Opaque argument for callback
 *	@param flags Fun for the future
 *	@return -1 Call failure, allocation failure, or other startup failure
 *	@return 0 Authentication in progress
 */
int bk_skid_authenticate(bk_s B, struct bk_ioh *ioh, bk_vptr *myname, bk_vptr *hisname, bk_vptr *key, struct bk_config *hisnamekeylist, bk_skid_cb done, void *opaque, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_skid *bs;
  int tmp;
  bk_vptr *vbuf = NULL;
  char *encode1 = NULL;
  char *encode2 = NULL;
  bk_flags newflags = 0;
  int ret;

  if (!ioh || !myname || !done)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_RETURN(B, -1);
  }

  if (!BK_CALLOC(bs))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate skid structure: %s\n", strerror(errno));
    BK_RETURN(B, -1);
  }

  bs->bs_ioh = ioh;
  bs->bs_key = key;
  bs->bs_myname = myname;
  bs->bs_hisname = hisname;
  bs->bs_hisnamekeylist = hisnamekeylist;
  bs->bs_done = done;
  bs->bs_opaque = opaque;
  bs->bs_state = SKID_START;

  if (!BK_MALLOC_LEN(bs->bs_Rme.ptr, BK_SKID_BYTES))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate skid random buffer: %s\n", strerror(errno));
    goto error;
  }

  if (!(bs->bs_randinfo = bk_rand_init(B, 0, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate random state during SKID initialization\n");
    goto error;
  }

  if (bk_rand_getbuf(B, bs->bs_randinfo, bs->bs_Rme.ptr, BK_SKID_BYTES, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not obtain random information during SKID initialization\n");
    goto error;
  }

  if (!(encode1 = bk_encode_base64(B, bs->bs_myname, NULL)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Encoding base64 of name failed\n");
    goto error;
  }

  if (!(encode2 = bk_encode_base64(B, &bs->bs_Rme, NULL)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Encoding base64 of random failed\n");
    goto error;
  }

  if (!BK_MALLOC(vbuf))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate output buffer: %s\n", strerror(errno));
    BK_RETURN(B, -1);
  }

  vbuf->len = strlen(encode1) + strlen(encode2) + strlen(BK_SKIDA) + 4;

  if (!(BK_MALLOC_LEN(vbuf->ptr, vbuf->len)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate output string: %s\n", strerror(errno));
    BK_RETURN(B, -1);
  }

  snprintf(vbuf->ptr, BK_SKID_MAXMSG, "%s %s %s\n", BK_SKIDA, encode1, encode2);
  vbuf->len--;
  free(encode1);
  free(encode2);
  encode1 = NULL;
  encode2 = NULL;

  if (bk_ioh_get(B, ioh, NULL, NULL, NULL, NULL, NULL, &bs->bs_oldhandler, &bs->bs_oldopaque, NULL, NULL, NULL, NULL, &bs->bs_oldflags) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not get old ioh information to reset\n");
    goto error;
  }

  if (BK_FLAG_ISSET(bs->bs_oldflags, BK_IOH_VECTORED) &&
      BK_FLAG_ISSET(bs->bs_oldflags, BK_IOH_LINE))
  {
    newflags = bs->bs_oldflags;
    BK_FLAG_CLEAR(newflags, BK_IOH_RAW);
    BK_FLAG_CLEAR(newflags, BK_IOH_BLOCKED);
    BK_FLAG_SET(newflags, BK_IOH_LINE);
  }

  ret = bk_ioh_update(B, ioh, NULL, NULL, NULL, skid_iohhandler, bs, 0, 0, 0,
		      newflags, BK_IOH_UPDATE_HANDLER|BK_IOH_UPDATE_OPAQUE|BK_IOH_UPDATE_FLAGS);
  if (ret < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not update ioh with new data\n");
    goto error;
  }
  else if (ret == 2)				// ioh destroyed (by reset?)
  {
    bs->bs_ioh = NULL;
    goto error;					// handler doesn't nuke bs
  }

  if (bk_ioh_write(B, ioh, vbuf, BK_IOH_BYPASSQUEUEFULL) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not output SKID A message\n");
    bk_skid_destroy(B, bs, 0);
  }
  vbuf = NULL;

  BK_RETURN(B, 0);

 error:
  if (bs)
    bk_skid_destroy(B, bs, 0);

  if (vbuf)
  {
    if (vbuf->ptr)
      free(vbuf->ptr);
    free(vbuf);
  }

  BK_RETURN(B, -1);
}



/**
 * SKID state destruction
 *
 *	@param B BAKA Thread/global environment
 *	@param bs SKID state
 */
void bk_skid_destroy(bk_s B, struct bk_skid *bs, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bs)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  if (bs->bs_Rme.ptr)
    free(bs->bs_Rme.ptr);
  if (bs->bs_Rhim)
  {
    if (bs->bs_Rhim->ptr)
      free(bs->bs_Rhim->ptr);
    free(bs->bs_Rhim);
  }
  if (bs->bs_randinfo)
    bk_rand_destroy(B, bs->bs_randinfo, 0);

  if (BK_FLAG_ISCLEAR(flags, BK_SKID_NORESTORE) && bs->bs_oldhandler && bs->bs_ioh)
  {
    bk_ioh_update(B, bs->bs_ioh, NULL, NULL, NULL, bs->bs_oldhandler, bs->bs_oldopaque, 0, 0, 0, bs->bs_oldflags, BK_IOH_UPDATE_HANDLER|BK_IOH_UPDATE_OPAQUE|BK_IOH_UPDATE_FLAGS);
  }

  free(bs);

  BK_VRETURN(B);
}



/**
 * Handle all SKID I/O
 *
 *	@param B BAKA Thread/global state
 *	@param data List of data to be relayed
 *	@param opaque Callback data
 *	@param ioh IOH data/activity came in on
 *	@param state_flags Type of activity
 */
static void skid_iohhandler(bk_s B, bk_vptr data[], void *opaque, struct bk_ioh *ioh, bk_ioh_status_e state_flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"libbk");
  struct bk_skid *bs = opaque;
  bk_vptr *vbuf, *tmp, *newcopy = NULL;
  char *encode1;
  char **tokens;
  int ret;
  bk_MD5_CTX ctx;

  if (!ioh || !bs)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  switch (state_flags)
  {
  case BkIohStatusIncompleteRead:
  case BkIohStatusReadComplete:
    if (!(newcopy = bk_ioh_coalesce(B, data, NULL, BK_IO_COALESCE_FLAG_MUST_COPY)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not coalesce skid data\n");
      bk_ioh_close(B, ioh, 0);
      BK_VRETURN(B);
    }

    if (!(tokens = bk_string_tokenize_split(B, newcopy->ptr, newcopy->len, NULL, NULL, BK_STRING_TOKENIZE_SKIPLEADING|BK_STRING_TOKENIZE_MULTISPLIT)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not tokenize skid data\n");
      bk_ioh_close(B, ioh, 0);
      BK_VRETURN(B);
    }
    free(newcopy->ptr);
    free(newcopy);

    switch (bs->bs_state)
    {
    case SKID_START:
    case SKID_END:
      bk_error_printf(B, BK_ERR_ERR, "Invalid skid state %x for ioh handler\n",bs->bs_state);
      bk_string_tokenize_destroy(B, tokens);
      skid_status(B, bs, SFAIL_SYSTEM);
      BK_VRETURN(B);

    case SKID_WAITING_SKIDA:
      if (strcmp(tokens[0], "SKID") || strcmp(tokens[0], "A"))
      {
	bk_error_printf(B, BK_ERR_WARN, "Got unexpected data during skidA read\n");
	bk_string_tokenize_destroy(B, tokens);
	BK_VRETURN(B);
      }

      if (!tokens[1] || !tokens[2] || tokens[3])
      {
	bk_error_printf(B, BK_ERR_WARN, "Ill formatted skidA message\n");
	bk_string_tokenize_destroy(B, tokens);
	skid_status(B, bs, SFAIL_PROTOCOL);
	BK_VRETURN(B);
      }

      if (!(tmp = bk_decode_base64(B, tokens[1])) ||
	  !(bs->bs_Rhim = bk_decode_base64(B, tokens[2])) ||
	  bs->bs_Rhim->len != bs->bs_Rme.len || tmp->len < 1)
      {
	bk_error_printf(B, BK_ERR_WARN, "Could not decode SKIDA name/random\n");
	bk_string_tokenize_destroy(B, tokens);
	skid_status(B, bs, SFAIL_PROTOCOL);
	BK_VRETURN(B);
      }
      bk_string_tokenize_destroy(B, tokens);

      if (bs->bs_hisname && (bs->bs_hisname->len != tmp->len || strncmp(bs->bs_hisname->ptr, tmp->ptr, bs->bs_hisname->len)))
      {
	bk_error_printf(B, BK_ERR_WARN, "Users' name does not match my approved list\n");
	BK_FLAG_SET(bs->bs_flags, BK_SKID_ISBAD);
	bs->bs_failure = SFAIL_BADUSER;
      }

      if (bs->bs_hisname->len == bs->bs_myname->len && !strncmp(bs->bs_hisname->ptr, bs->bs_myname->ptr, bs->bs_myname->len))
      {
	bk_error_printf(B, BK_ERR_WARN, "My and his name match, go into anti-spoofing\n");
	BK_FLAG_SET(bs->bs_flags, BK_SKID_ISBAD);
	bs->bs_failure = SFAIL_BADUSER;
      }

      if (!strncmp(bs->bs_Rhim->ptr, bs->bs_Rme.ptr, bs->bs_Rme.len))
      {
	bk_error_printf(B, BK_ERR_WARN, "My and his random numbers match, go into anti-spoofing\n");
	BK_FLAG_SET(bs->bs_flags, BK_SKID_ISBAD);
	bs->bs_failure = SFAIL_BADUSER;
      }

      if (bs->bs_hisnamekeylist)
      {
	bk_vptr skidrmnt;
	char *encname, *source, *value;

	/*
	 * <BUG ID="1250">This keylist code is pretty broken.
	 * bs_hisname is specified to be NULL if keylist is in use.
	 * Make sure when fixing this to avoid integer overflow
	 * problems, if applicable.</BUG>
	 */

	if (!(BK_MALLOC_LEN(source,bs->bs_hisname->len + strlen("SKID-") + 1)))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not allocate memory for name lookup: %s\n", strerror(errno));
	  skid_status(B, bs, SFAIL_SYSTEM);
	  BK_VRETURN(B);
	}
	memcpy(source, "SKID-", strlen("SKID-"));
	memcpy(source + strlen("SKID-"), bs->bs_hisname->ptr, bs->bs_hisname->len);
	source[strlen("SKID-") + bs->bs_hisname->len] = 0;

	if (!(value = bk_config_getnext(B, bs->bs_hisnamekeylist, source, NULL)))
	{
	  if (!bs->bs_key || !bs->bs_key->ptr)
	  {
	    bk_error_printf(B, BK_ERR_ERR, "No default key, no specific key\n");
	    BK_FLAG_SET(bs->bs_flags, BK_SKID_ISBAD);
	    bs->bs_failure = SFAIL_BADUSER;
	  }
	}
	else
	{
	  if (strncmp(value, BK_SKIDKQ, strlen(BK_SKIDKQ)))
	  {
	    if (!(bs->bs_key = bk_decode_base64(B, value+strlen(BK_SKIDKQ))))
	    {
	      free(source); source=NULL;
	      bk_error_printf(B, BK_ERR_ERR, "Could not decode %s=%s\n",source,value);
	      skid_status(B, bs, SFAIL_SYSTEM);
	      BK_VRETURN(B);
	    }
	    BK_FLAG_SET(bs->bs_flags, BK_SKID_FREEKEY);
	  }
	  else if (strncmp(value, BK_SKIDKR, strlen(BK_SKIDKR)))
	  {
	    if (!BK_MALLOC(bs->bs_key) || !BK_MALLOC_LEN(bs->bs_key->ptr, strlen(value)-strlen(BK_SKIDKR)))
	    {
	      bk_error_printf(B, BK_ERR_ERR, "Could not allocate key structure: %s\n", strerror(errno));
	      free(source); source=NULL;
	      if (bs->bs_key)
		free(bs->bs_key);
	      bs->bs_key = NULL;

	      skid_status(B, bs, SFAIL_SYSTEM);
	      BK_VRETURN(B);
	    }
	    BK_FLAG_SET(bs->bs_flags, BK_SKID_FREEKEY);
	  }
	}
	free(source); source=NULL;
      }

      if (BK_FLAG_ISSET(bs->bs_flags, BK_SKID_ISBAD))
      {
	if (bk_rand_getbuf(B, bs->bs_randinfo, ctx.digest, sizeof(ctx.digest), 0) < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not generate random number\n");
	  skid_status(B, bs, SFAIL_SYSTEM);
	  BK_VRETURN(B);
	}
      }
      else
      {
	bk_MD5Init(B, &ctx);
	bk_MD5Update(B, &ctx, bs->bs_Rme.ptr, bs->bs_Rme.len);
	bk_MD5Update(B, &ctx, bs->bs_Rhim->ptr, bs->bs_Rhim->len);
	bk_MD5Update(B, &ctx, bs->bs_myname->ptr, bs->bs_myname->len);
	bk_MD5Update(B, &ctx, bs->bs_key->ptr, bs->bs_key->len);
	bk_MD5Final(B, &ctx);
      }

      if (!BK_MALLOC(vbuf))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not allocate skidb structure: %s\n", strerror(errno));
	skid_status(B, bs, SFAIL_SYSTEM);
	BK_VRETURN(B);
      }
      vbuf->ptr = ctx.digest;
      vbuf->len = sizeof(ctx.digest);
      if (!(encode1 = bk_encode_base64(B, bs->bs_myname, NULL)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Encoding base64 of name failed\n");
	skid_status(B, bs, SFAIL_SYSTEM);
	BK_VRETURN(B);
      }
      vbuf->len = strlen(BK_SKIDB) + strlen(encode1) + 3;
      if (!BK_MALLOC_LEN(vbuf->ptr,vbuf->len))
      {
	free(vbuf);
	bk_error_printf(B, BK_ERR_ERR, "Could not allocate skidb string: %s\n", strerror(errno));
	skid_status(B, bs, SFAIL_SYSTEM);
	BK_VRETURN(B);
      }
      sprintf(vbuf->ptr, "%s %s\n", BK_SKIDB, encode1);
      vbuf->len--;

      if (bk_ioh_write(B, ioh, vbuf, BK_IOH_BYPASSQUEUEFULL) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not output SKID A message\n");
	skid_status(B, bs, SFAIL_SYSTEM);
	BK_VRETURN(B);
      }
      vbuf = NULL;
      break;

    case SKID_WAITING_SKIDB:
      // XXX
    }

  case BkIohStatusIohReadError:
  case BkIohStatusIohReadEOF:
  case BkIohStatusIohWriteError:
    skid_status(B, bs, SFAIL_IO);
    break;

  case BkIohStatusWriteComplete:
  case BkIohStatusWriteAborted:
    // Guarenteed just one buffer
    free(data[0].ptr);
    free(data);
    break;

  case BkIohStatusIohClosing:
    skid_status(B, bs, SFAIL_IO);
    break;

  case BkIohStatusIohSeekSuccess:
  case BkIohStatusIohSeekFailed:
    break;

    // No default here so that compiler can catch missed state
  }

  BK_VRETURN(B);
}



/**
 * Perform callback to user telling status
 */
static void skid_status(bk_s B, struct bk_skid *bs, const char *str)
{
  // XXX
}

#endif /*SKIDHASBEENDEBUGGEDANDTESTED*/
