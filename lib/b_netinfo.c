#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2001-2011";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2001-2011 The Authors. All rights reserved.
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
 * Stuff having to do with network information
 */

#include <libbk.h>
#include "libbk_internal.h"



static int bni2un(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *bna, struct sockaddr_un *sunix, bk_flags flags);
static int update_bni_pretty(bk_s B, struct bk_netinfo *bni);
static int bni2sin(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *bna, struct sockaddr_in *sin4, bk_flags flags);
#ifdef HAVE_INET6
static int bni2sin6(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *bna, struct sockaddr_in6 *sin6, bk_flags flags);
#endif

static int bna_oo_cmp(void *bck1, void *bck2);
static int bna_ko_cmp(void *a, void *bck2);


/**
 * Create a bk_netinfo struct
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@returns <i>NULL</i> on failure.<br>
 *	@returns the @a bk_netinfo on success.
 */
struct bk_netinfo *
bk_netinfo_create(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netinfo *bni = NULL;

  if (!BK_CALLOC(bni))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not calloc bni: %s\n", strerror(errno));
    goto error;
  }

  if (!(bni->bni_addrs = netinfo_addrs_create(bna_oo_cmp, bna_ko_cmp, DICT_UNIQUE_KEYS|bk_thread_safe_if_thread_ready)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create address list\n");
    goto error;
  }

  if (update_bni_pretty(B, bni) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not update bni pretty string\n");
    goto error;
  }

  BK_RETURN(B, bni);

 error:
  if (bni) bk_netinfo_destroy(B, bni);
  BK_RETURN(B, NULL);
}



/**
 * Destroy a baka netinfo struct.
 *
 * THREADS: MT-SAFE (assuming different bni)
 *
 *	@param B BAKA thread/global state.
 *	@param bni The @a struct @netinfo to destroy.
 */
void
bk_netinfo_destroy(bk_s B, struct bk_netinfo *bni)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netaddr *bna;

  if (!bni)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  /*
   * If we aborted during bni allocation this dict header may be NULL.
   * But that is the only case.
   */
  if (bni->bni_addrs)
  {
    while((bna = netinfo_addrs_minimum(bni->bni_addrs)))
    {
      netinfo_addrs_delete(bni->bni_addrs, bna);
      bk_netaddr_destroy(B, bna);
    }
    netinfo_addrs_destroy(bni->bni_addrs);
  }

  if (bni->bni_bsi) bk_servinfo_destroy(B, bni->bni_bsi);
  if (bni->bni_bpi) bk_protoinfo_destroy(B, bni->bni_bpi);
  if (bni->bni_pretty) free (bni->bni_pretty);
  free(bni);
  BK_VRETURN(B);
}



/**
 * Add an @a bk_netaddr structure to a @a netinfo. If the address already
 * appears to exist, there is a clash. If a clash occurs and @a obna has
 * ben supplied, it will be filled in the insertion will return success. If
 * @a obna has <em>not</em> been supplied, then the error is fatal. Do
 * yourself a favor and supply @a obna. If the adress type does not match
 * the current type, the insertion is aborted.
 *
 * THREADS: MT-SAFE (assuming different bni or not obna)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param bni The @a netinfo in which to insert into the @a netaddr.
 *	@param ibna The @a netaddr to insert.
 *	@param obna Will contain the @a netaddr which conflicts (if
 *	applicable). <em>Optional</em>
 *	@returns <i>-1</i> on failure.<br>
 *	@returns <i>1</i> on sanity failure.
 *	@returns <i>0</i> on success.
 */
int
bk_netinfo_add_addr(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *ibna, struct bk_netaddr **obna)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netaddr *bna;
  int first_entry = 0;

  if (!bni || !ibna)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (obna)
    *obna  = NULL;			/* Initialize copyout */

  if ((bna = netinfo_addrs_minimum(bni->bni_addrs)) &&
      bna->bna_type != ibna->bna_type)
  {
    bk_error_printf(B, BK_ERR_ERR, "Baka netinfo structures do not support heterogenious address types (%d != %d)\n", bna->bna_type, ibna->bna_type);
    BK_RETURN(B, 1);
  }

  if (!netinfo_addrs_minimum(bni->bni_addrs))
  {
    first_entry = 1;
  }

  if (netinfo_addrs_insert_uniq(bni->bni_addrs, ibna, (dict_obj *)obna) != DICT_OK)
  {
    if (obna && *obna)
    {
      bk_error_printf(B, BK_ERR_WARN, "Address %s already exists\n", (*obna)->bna_pretty);
      /* The fact that there the user has a copyout means this is OK */
      BK_RETURN(B, 0);
    }

    /*
     * NB: If there was an object clash and the user did not supply obna,
     * it's essentially a fatal error.
     */
    bk_error_printf(B, BK_ERR_ERR, "Could not insert address\n");
    goto error;
  }

  ibna->bna_netinfo_addrs = bni->bni_addrs;

  if (first_entry)
  {
    if (bk_netinfo_set_primary_address(B, bni, NULL))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not set primary address\n");
      goto error;
    }
  }

  update_bni_pretty(B, bni);

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}



/**
 * Delete a @a netaddr from a @a netinfo, returning the old structure in @a
 * obna (if supplied). This function does <em>not</em> clean up the @a
 * netaddr (since it can't be certain others don't have references to it),
 * so if you don't supply @a obna, then you'd better be certain that you
 * know what you are doing or face the possibility of a memory leak. Don't
 * tempt fate, supply @a obna.
 *
 * THREADS: MT-SAFE (assuming different bni)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param bni The @a netinfo from which to delete the @a netaddr.
 *	@param bna The @a netaddr to delete.
 *	@returns <i>-1</i> on failure.<br>
 *	@returns <i>0</i> on success.
 */
int
bk_netinfo_delete_addr(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *ibna, struct bk_netaddr **obna)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netaddr *bna;

  if (!bni || !ibna)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (obna)
    *obna = NULL;				/* Initialize copyout */

  if (!(bna = netinfo_addrs_search(bni->bni_addrs, ibna)))
  {
    bk_error_printf(B, BK_ERR_WARN, "Could not find address %s to delete\n", bna->bna_pretty);
    /* It's not fatal to not find something you were going to delete */
    BK_RETURN(B, 0);
  }

  netinfo_addrs_delete(bni->bni_addrs, bna);

  /*
   * If we're deleting one our primary or secondary addresses, make sure it
   * gets NULL'ed out.
   */

  if (bna == bni->bni_addr)
    bni->bni_addr = NULL;

  if (bna == bni->bni_addr2)
    bni->bni_addr2 = NULL;

  if (update_bni_pretty(B, bni) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not update a bni pretty string: %s\n", strerror(errno));
    /* Whatever */
  }

  if (obna) *obna = bna;
  BK_RETURN(B, 0);
}



/**
 * Update the pretty string of a @a netinfo.
 *
 * THREADS: MT-SAFE (assuming different bni)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param bni The @a netinfo to update.
 *	@returns <i>-1</i> on failure. <br>
 *	@returns <i>0</i> on success.
 */
static int
update_bni_pretty(bk_s B, struct bk_netinfo *bni)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char scratch[SCRATCHLEN];

  if (!bni)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (bni->bni_pretty)
    free (bni->bni_pretty);
  bni->bni_pretty = NULL;

  snprintf(scratch, SCRATCHLEN, "[%s:%s:%s]",
	   (bni->bni_addr && bni->bni_addr->bna_pretty)?bni->bni_addr->bna_pretty:"NO_ADDR",
	   (bni->bni_bsi && bni->bni_bsi->bsi_servstr)?bni->bni_bsi->bsi_servstr:"NO_SERV",
	   (bni->bni_bpi && bni->bni_bpi->bpi_protostr)?bni->bni_bpi->bpi_protostr:"NO_PROTO");

  if (!(bni->bni_pretty = strdup(scratch)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup bni pretty: %s\n", strerror(errno));
    goto error;
  }

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}



/**
 * Set the primary address for a @a netaddr. The meaning of <i>primary
 * address</i> depends whatever you, the caller, want it to be, though
 * logic should play a role. For instance when the @a netaddr refers to a
 * network connection, the primay adress will likely be the addres to which
 * you are connected (with the addrs list containing alternatives). If the
 * @a netinfo is describing a network, the primary address might be be the
 * network address (with the secondary address set to the netmask). It's
 * your call. However in order for there to be a useful "pretty string",
 * the primary address must be set.
 *
 * THREADS: MT-SAFE (assumming different bni)
 * THREADS: REENTRANT
 *
 * @param B Baka thread/global enviornment
 * @param bni The @a netinfo to update
 * @param bna The address to update to (NULL for first)
 * @return <i>-1</i> on call failure, missing address, other railure
 * @return <br><i>0</i> on success
 */
int
bk_netinfo_set_primary_address(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *bna)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bni)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (bna)
  {
    bna = netinfo_addrs_search(bni->bni_addrs, bna);
  }
  else
  {
    bna = bk_netinfo_get_addr(B, bni);
  }

  if (!bna)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not determine bna to set as primary\n");
    goto error;
  }

  bni->bni_addr = bna;

  if (update_bni_pretty(B, bni)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not update bna pretty string\n");
    goto error;
  }

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}



/**
 * Reset the primary address.
 *
 * THREADS: MT-SAFE (assuming different bni)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_netinfo_reset_primary_address(bk_s B, struct bk_netinfo *bni)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bni)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  bni->bni_addr = NULL;
  update_bni_pretty(B, bni);

  BK_RETURN(B, 0);
}



/**
 * Clone a @a netinfo structure.
 *
 * THREADS: MT-SAFE (assuming different obni)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param bni The source @a netinfo.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a @a new bk_netaddr on success.
 */
struct bk_netinfo *
bk_netinfo_clone(bk_s B, struct bk_netinfo *obni)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netinfo *nbni = NULL;
  struct bk_netaddr *obna, *nbna;

  if (!obni)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!(nbni = bk_netinfo_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bni\n");
    goto error;
  }

  nbni->bni_flags = obni->bni_flags;

  for (obna = netinfo_addrs_minimum(obni->bni_addrs);
       obna;
       obna = netinfo_addrs_successor(obni->bni_addrs, obna))
  {
    if (!(nbna = bk_netaddr_clone(B, obna)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not clone netaddr\n");
      goto error;
    }

    if (bk_netinfo_add_addr(B, nbni, nbna, NULL)<0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not insert netaddr into list\n");
      goto error;
    }

    if (obni->bni_addr == obna) nbni->bni_addr = nbna;
    if (obni->bni_addr2 == obna) nbni->bni_addr2 = nbna;
  }

  if (!(nbni->bni_bsi = bk_servinfo_clone(B, obni->bni_bsi)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not clone servinfo\n");
    goto error;
  }

  if (!(nbni->bni_bpi = bk_protoinfo_clone(B, obni->bni_bpi)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not clone protoinfo\n");
    goto error;
  }

  if (update_bni_pretty(B, nbni)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not update bni pretty string\n");
    goto error;
  }

  BK_RETURN(B, nbni);

 error:
  if (nbni) bk_netinfo_destroy(B, nbni);
  BK_RETURN(B, nbni);
}



/**
 * Update the servinfo part of netinfo based on a servent.
 *
 * THREADS: MT-SAFE (assuming different bni)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param bni The @a bk_netinfo to update.
 *	@param s The @a servent to copy.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_netinfo_update_servent(bk_s B, struct bk_netinfo *bni, struct servent *s)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bni || !s)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (bni->bni_bsi)
    bk_servinfo_destroy(B, bni->bni_bsi);

  if (!(bni->bni_bsi = bk_servinfo_serventdup(B, s)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not copy servent\n");
    goto error;
  }

  if (update_bni_pretty(B, bni))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not update bni pretty string\n");
    goto error;
  }

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}



/**
 * Update the protoinfo part of netinfo based on a protoent.
 *
 * THREADS: MT-SAFE (assuming different bni)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param bni The @a bk_netinfo to update
 *	@param p The @a protoent source.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_netinfo_update_protoent(bk_s B, struct bk_netinfo *bni, struct protoent *p)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bni || !p)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (bni->bni_bpi)
    bk_protoinfo_destroy(B, bni->bni_bpi);

  if (!(bni->bni_bpi = bk_protoinfo_protoentdup(B, p)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not clone protoent\n");
    goto error;
  }

  if (update_bni_pretty(B, bni))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not update bni pretty string\n");
    goto error;
  }

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}



/**
 * Update a @a bk_netinfo based on a hostent
 *
 * THREADS: MT-SAFE (assuming different bni)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param bni The @a bk_netinfo to update.
 *	@param h The @a hostent source.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_netinfo_update_hostent(bk_s B, struct bk_netinfo *bni, struct hostent *h)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netaddr *bna;
  int type;

  if (!bni || !h)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if ((type = bk_netaddr_af2nat(B, h->h_addrtype))<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not convert AF %d to netaddr type\n",
		    h->h_addrtype);
    goto error;
  }

  /*
   * NB: in both of the following loops, we take the following the
   * approach.  If we fail to allocate the bk_netaddr, we simply abort
   * leaving the bni in an incomplete, but nonetheless consistent state. If
   * we allocate the bna, but fail to *insert* it in the netinfo_addr list,
   * then we must clean up *that* bna (or we will lose it and all of its
   * memory) only.  What we're saying hers is that if we insert two
   * successfully then fail, we will make no attempt to clean up those we
   * have inserted.
   */
  switch (h->h_addrtype)
  {
  case AF_INET:
    {
      struct in_addr **ia;

      for(ia = (struct in_addr **)(h->h_addr_list); *ia; ia++)
      {
	if (!(bna = bk_netaddr_addrdup(B, type, *ia, 0)))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not create bna from addr\n");
	  goto error;
	}

	if (bk_netinfo_add_addr(B, bni, bna, NULL) < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not insert bna\n");
	  bk_netaddr_destroy(B, bna);		/* Must clean up */
	}
      }
    }
    break;

  case AF_INET6:
    {
      struct in6_addr **ia;

      for(ia = (struct in6_addr **)(h->h_addr_list); *ia; ia++)
      {
	if (!(bna = bk_netaddr_addrdup(B, type, *ia, 0)))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not create bna from addr\n");
	  goto error;
	}

	if (bk_netinfo_add_addr(B, bni, bna, NULL) < 0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not insert bna\n");
	  bk_netaddr_destroy(B, bna);		/* Must clean up */
	}
      }
    }
    break;

  default:
    bk_error_printf(B, BK_ERR_ERR, "Usupported address type: %d\n", h->h_addrtype);
    goto error;
    break;
  }

  /* This technically shouldn't do anything (here), but let's do it anyway */
  if (update_bni_pretty(B, bni) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not update bni prett string\n");
    goto error;
  }

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}



/**
 * Retrieve the primary address
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param bni The netinfo from which to retrieve address.
 *	@return <i>NULL</i> on failure.<br>
 *	@return @a bk_netaddr pointer on success.
 *	@bug There is no way to tell the difference between an error and @a
 *	bk_netinfo with no addresses inserted.
 */
struct bk_netaddr *
bk_netinfo_get_addr(bk_s B, struct bk_netinfo *bni)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netaddr *bna = NULL;

  if (!bni)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (bni->bni_addr)
    bna = bni->bni_addr;
  else
    bna = netinfo_addrs_minimum(bni->bni_addrs);

  BK_RETURN(B, bna);
}



/**
 * Convert a netinfo to a sockaddr.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param bni The @a bk_netinfo to use for the source.
XXX - This should not be used and the primary address only used--change the primary each time
 *	@param bna The @a bk_netaddr to use for the source (may be null).
XXX - This should be nuked since the bni will have the netinfo type set
 *	@param type Address type (may be BK_NETINFO_TYPE_UNKNOWN)
 *	@param sa The sockaddr to copy into.
 *	@param flags Random flags.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_netinfo_to_sockaddr(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *bna, bk_netaddr_type_e type, bk_sockaddr_t *bs, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret;

  if (!bni || !bs)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!bna && !(bna = bk_netinfo_get_addr(B, bni)))
  {
    bk_error_printf(B, BK_ERR_WARN, "Could not determine bk_netaddr to use\n");
    BK_RETURN(B, -1);
  }

  /* If type is not specified pull it out of the bna */
  if (type == BkNetinfoTypeUnknown)
  {
    if (bna)
    {
      type = bna->bna_type;
    }
    else
    {
      bk_error_printf(B, BK_ERR_ERR, "Cannot determine address type\n");
      goto error;
    }
  }

  switch (bk_netaddr_nat2af(B, type))
  {
  case AF_INET:
    ret = bni2sin(B, bni, bna, &bs->bs_sin, flags);
    break;

#ifdef HAVE_INET6
  case AF_INET6:
    ret = bni2sin6(B, bni, bna, &bs->bs_sin6, flags);
    break;
#endif

  case AF_LOCAL:
    ret = bni2un(B, bni, bna, &bs->bs_sun, flags);
    break;

  default:
    bk_error_printf(B, BK_ERR_ERR, "Unsupported address family\n");
    goto error;
    break;
  }

  if (ret == -1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not convert netinfo to sockaddr\n");
  }

  BK_RETURN(B, ret);

 error:
  BK_RETURN(B, -1);
}



/**
 * Convert a @a bk_netinfo to a struct sockaddr_in
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param bni The @a bk_netinfo to use for the source.
 *	@param bna The @a bk_netaddr to use for the source.
 *	@param sin4 The INET sockaddr to copy into.
 *	@param flags Random flags.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
bni2sin(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *bna, struct sockaddr_in *sin4, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bni || !sin4)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  /*
   * "For maximum portability, you should always zero the socket address
   * structure before populating it..." BSD man pages
   */
  memset(sin4, 0, sizeof(struct sockaddr_in));

  sin4->sin_family = AF_INET;

  BK_SET_SOCKADDR_LEN(B, sin4, sizeof(struct sockaddr_in));

  if (!bna)
  {
    if (BK_FLAG_ISSET(flags, BK_NETINFO2SOCKADDR_FLAG_FUZZY_ANY))
    {
      sin4->sin_addr.s_addr = INADDR_ANY;
    }
    else
    {
      bk_error_printf(B, BK_ERR_ERR, "No address to use\n");
      goto error;
    }
  }
  else
  {
    // use bna length instead of sin length (latter includes non-address crap)
    memmove(&(sin4->sin_addr), &(bna->bna_inet), bna->bna_len);
  }

  sin4->sin_port = bni->bni_bsi->bsi_port;

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);

}



#ifdef HAVE_INET6
/**
 * Convert a @a bk_netinfo to a struct sockaddr_in
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param bni The @a bk_netinfo to use for the source.
 *	@param bna The @a bk_netaddr to use for the source.
 *	@param sin6 The INET6 sockaddr to copy into.
 *	@param flags Random flags.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
bni2sin6(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *bna, struct sockaddr_in6 *sin6, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct in6_addr in6any = IN6ADDR_ANY_INIT;

  if (!bni || !sin6)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  /*
   * "For maximum portability, you should always zero the socket address
   * structure before populating it..." BSD man pages
   */
  memset(sin6, 0, sizeof(struct sockaddr_in6));

  sin6->sin6_family = AF_INET6;

  BK_SET_SOCKADDR_LEN(B, sin6, sizeof(struct sockaddr_in6));

  if (!bna)
  {
    if (BK_FLAG_ISSET(flags, BK_NETINFO2SOCKADDR_FLAG_FUZZY_ANY))
    {
      memmove(&sin6->sin6_addr, &in6any, sizeof(in6any));
    }
    else
    {
      bk_error_printf(B, BK_ERR_ERR, "No address to use\n");
      goto error;
    }
  }
  else
  {
    memmove(&(sin6->sin6_addr), &(bna->bna_inet), bna->bna_len);
  }

  sin6->sin6_port = bni->bni_bsi->bsi_port;

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);

}
#endif /* HAVE_INET6 */



/**
 * Convert a @a bk_netinfo to a struct sockaddr_un
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param bni The @a bk_netinfo to use for the source.
 *	@param bna The @a bk_netaddr to use for the source.
 *	@param sunix The INET6 sockaddr to copy into.
 *	@param flags Random flags.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
bni2un(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *bna, struct sockaddr_un *sunix, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  /* Fuzzy ANY does not make sense in AF_LOCAL */
  if (!bni || !bna || !sunix)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  /*
   * "For maximum portability, you should always zero the socket address
   * structure before populating it..." BSD man pages
   */
  memset(sunix, 0, sizeof(struct sockaddr_un));

  sunix->sun_family = AF_LOCAL;

  BK_SET_SOCKADDR_LEN(B, sunix, sizeof(struct sockaddr_un));

  snprintf(sunix->sun_path, sizeof(sunix->sun_path), "%s", bna->bna_path);

  BK_RETURN(B, 0);
}



/**
 * Create a @a bk_netinfo from a sockaddr.
 *
 * THREADS: MT-SAFE (assuming s is not closed)
 *
 *	@param B BAKA thread/global state.
 *	@param s The socket from which to create the @a bk_netinfo
 *	@param proto The protocol number.
 *	@param side Want local or remote address
 *	@return <i>NULL</i> on failure.<br>
 *	@return an allocated @a bk_netinfo.
 */
struct bk_netinfo *
bk_netinfo_from_socket(bk_s B, int s, int proto, bk_socket_side_e side)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  bk_sockaddr_t bs;
  socklen_t len;
  bk_netaddr_type_e netaddr_type;
  int socket_type;
  socklen_t socket_type_len;

  memset(&bs, 0,sizeof(bs));

  switch (side)
  {
  case BkSocketSideLocal:
    len = sizeof(bs);
    if (getsockname(s, &(bs.bs_sa), &len) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not get local sockname: %s\n", strerror(errno));
      goto error;
    }
    break;

  case BkSocketSideRemote:
    len = sizeof(bs);
    if (getpeername(s, &(bs.bs_sa), &len) < 0)
    {
      // This is quite common in server case, so a WARN not an ERR
      bk_error_printf(B, BK_ERR_WARN, "Could not get peer sockname: %s\n", strerror(errno));
      goto error;
    }
    break;

  default:
    bk_error_printf(B, BK_ERR_ERR, "Unknown sock side specifier: %d\n", side);
    goto error;
    break;
  }

  if (!proto)
  {
    socket_type_len = sizeof(socket_type);

    if (getsockopt(s, SOL_SOCKET, SO_TYPE, &socket_type, &socket_type_len)<0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not get socket type: %s\n", strerror(errno));
      goto error;
    }

    netaddr_type = bk_netaddr_af2nat(B, bs.bs_sa.sa_family);
    switch (netaddr_type)
    {
    case BkNetinfoTypeInet:
      /* Guess the protocol */
      switch (socket_type)
      {
      case SOCK_STREAM:
	proto = IPPROTO_TCP;
	break;
      case SOCK_DGRAM:
	proto = IPPROTO_UDP;
	break;
      default:
	bk_error_printf(B, BK_ERR_ERR, "Unknown or unsupported socket type: %d\n", socket_type);
	goto error;
	break;
      }
      break;
    case BkNetinfoTypeInet6:
      switch (socket_type)
      {
      case SOCK_STREAM:
	proto = IPPROTO_TCP;
	break;
      case SOCK_DGRAM:
	proto = IPPROTO_UDP;
	break;
      default:
	bk_error_printf(B, BK_ERR_ERR, "Unknown or unsupported socket type: %d\n", socket_type);
	goto error;
	break;
      }
      break;
    case BkNetinfoTypeLocal:
      switch (socket_type)
      {
      case SOCK_STREAM:
	proto = BK_GENERIC_STREAM_PROTO;
	break;

      case SOCK_DGRAM:
	proto = BK_GENERIC_DGRAM_PROTO;
	break;

      default:
	bk_error_printf(B, BK_ERR_ERR, "Unknown or unsupported socket type: %d\n", socket_type);
	goto error;
	break;
      }
      break;
    default:
      bk_error_printf(B, BK_ERR_ERR,"Unknown netaddr_type: %d\n", netaddr_type);
      break;
    }

  }

  BK_RETURN(B, bk_netinfo_from_sockaddr(B, &bs, proto, 0));

 error:
  BK_RETURN(B, NULL);
}


/**
 * Create a bk_netinfo structure from a sockaddr
 *
 *	@param B BAKA thread/global state.
 *	@param bs The bk_sockaddr_t from which to create the bni
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
struct bk_netinfo *
bk_netinfo_from_sockaddr(bk_s B, bk_sockaddr_t *bs, int proto, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netinfo *bni = NULL;
  struct bk_netaddr *bna = NULL;
  char scratch[100];
  bk_netaddr_type_e netaddr_type;
  const char *proto_str = NULL;

  if (!bs)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!(bni = bk_netinfo_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create bni\n");
    goto error;
  }

  netaddr_type = bk_netaddr_af2nat(B, bs->bs_sa.sa_family);

  switch (netaddr_type)
  {
  case BkNetinfoTypeInet:
    snprintf(scratch, 100, "%d", proto);
    if (bk_getprotobyfoo(B, scratch, NULL, bni, 0)<0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not set protoent\n");
      goto error;
    }

    snprintf(scratch, 100, "%d", ntohs(bs->bs_sin.sin_port));
    if (bk_getservbyfoo(B, scratch, bni->bni_bpi->bpi_protostr, NULL, bni, 0)<0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not set servent\n");
      goto error;
    }

    /*BK_GET_SOCKADDR_LEN(B, bs->bs_sin, len);*/
    if (!(bna = bk_netaddr_user(B, netaddr_type, &(bs->bs_sin.sin_addr), sizeof(bs->bs_sin.sin_addr), 0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create netaddr\n");
      goto error;
    }
    break;

  case BkNetinfoTypeInet6:
    snprintf(scratch, 100, "%d", proto);
    if (bk_getprotobyfoo(B, scratch, NULL, bni, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not set protoent\n");
      goto error;
    }

    snprintf(scratch, 100, "%d", ntohs(bs->bs_sin6.sin6_port));
    if (bk_getservbyfoo(B, scratch, bni->bni_bpi->bpi_protostr, NULL, bni, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not set servent\n");
      goto error;
    }

    /*BK_GET_SOCKADDR_LEN(B, bs->bs_sin6, len);*/
    if (!(bna = bk_netaddr_user(B, netaddr_type, &(bs->bs_sin6.sin6_addr), sizeof(bs->bs_sin6.sin6_addr), 0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create netaddr\n");
      goto error;
    }
    break;

  case BkNetinfoTypeLocal:
    switch (proto)
    {
    case BK_GENERIC_STREAM_PROTO:
      proto_str = BK_GENERIC_STREAM_PROTO_STR;
      break;

    case BK_GENERIC_DGRAM_PROTO:
      proto_str = BK_GENERIC_DGRAM_PROTO_STR;
      break;

    default:
      bk_error_printf(B, BK_ERR_ERR, "Unknown AF_LOCAL protocol: %d\n", proto);
      goto error;
    }

    if (!(bni->bni_bpi = bk_protoinfo_create(B, proto, proto_str, 0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create proto info\n");
      goto error;
    }

    if (!(bna = bk_netaddr_user(B, BkNetinfoTypeLocal, bs->bs_sun.sun_path, 0, 0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create netaddr\n");
      goto error;
    }

    break;

  default:
    bk_error_printf(B, BK_ERR_ERR, "Unsupported netaddr type: %d\n", netaddr_type);
    goto error;
    break;
  }

  if (bk_netinfo_add_addr(B, bni, bna, NULL) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert bna into bna\n");
    goto error;
  }

  update_bni_pretty(B, bni);

  BK_RETURN(B, bni);

 error:
  /* Despite appearences, this will *not* double free */
  if (bna) bk_netaddr_destroy(B, bna);
  if (bni) bk_netinfo_destroy(B, bni);
  BK_RETURN(B, NULL);
}



/**
 * Return the pretty printing string. NB This space is "static" as far a
 * the user is concerned, so I suppose this function is not reentrent.
 *
 * THREADS: MT-SAFE (assuming different bni)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param bni @a bk_netinfo to print out.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
const char *
bk_netinfo_info(bk_s B, struct bk_netinfo *bni)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bni)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  update_bni_pretty(B, bni);
  BK_RETURN(B, bni->bni_pretty);
}



/**
 * Set the primary address to the next address on the list. If the primary
 * address is currently unset, then set it to the first entry in the list
 * (assuming there is one).
 *
 * THREADS: MT-SAFE (assuming different bni)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param bni The @a bk_netinfo on which to work
 *	@return <i>NULL</i> on failure <em>or</em> no more entries in list.<br>
 *	@return @a bk_netinfo on success with more entries.
 */
struct bk_netaddr *
bk_netinfo_advance_primary_address(bk_s B, struct bk_netinfo *bni)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netaddr *bna;

  if (!bni)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }
  if (!bni->bni_addr)
  {
    if (bk_netinfo_set_primary_address(B, bni, NULL)<0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not set primary address on bni\n");
      goto error;
    }
  }
  else
  {
    if ((bna=netinfo_addrs_successor(bni->bni_addrs, bni->bni_addr)))
    {
      if (bk_netinfo_set_primary_address(B, bni, bna)<0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not set primary address on bni\n");
	goto error;
      }
    }
    else
    {
      /* Nobody left in list */
      bni->bni_addr=NULL;
      update_bni_pretty(B, bni);
    }
  }
  BK_RETURN(B, bni->bni_addr);

 error:
  BK_RETURN(B, NULL);
}



/**
 * Return the adress type of a netinfo structure. All addresses contained
 * within are assumed to be homogeneous so the type of any one should be
 * the type of all
 *
 *	@param B BAKA thread/global state.
 *	@param bni The netinfo to check
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return netinfo <i>type</i> on success.
 */
int
bk_netinfo_addr_type(bk_s B, struct bk_netinfo *bni, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netaddr *bna;

  if (!bni)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!(bna = netinfo_addrs_minimum(bni->bni_addrs)))
    BK_RETURN(B, BkNetinfoTypeUnknown);

  BK_RETURN(B, bna->bna_type);
}


/**
 * See if a bk_netinfo structure matches a sockaddr
 *
 *	@param B BAKA thread/global state.
 *	@param bni The bk_netinfo to check
 *	@param bs The sockaddr to match against.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on no match
 *	@return <i>1</i> on match
 */
int
bk_netinfo_match_sockaddr(bk_s B, struct bk_netinfo *bni, bk_sockaddr_t *bs, int proto, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netaddr *addr = NULL;
  struct bk_netaddr *addr2 = NULL;
  struct bk_netaddr *bna;
  int match = 0;

  if (!bni || !bs)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (proto != bni->bni_bpi->bpi_proto)
    goto done;

  switch(bs->bs_sa.sa_family)
  {
  case AF_INET:
    if (bs->bs_sin.sin_port != bni->bni_bsi->bsi_port)
      goto done;
    break;

  case AF_INET6:
    if (bs->bs_sin6.sin6_port != bni->bni_bsi->bsi_port)
      goto done;
    break;

  case AF_LOCAL:
    break;

  default:
    bk_error_printf(B, BK_ERR_ERR, "Illegal network family: %d\n", bs->bs_sa.sa_family);
    goto error;
  }

  addr = bni->bni_addr;
  addr2 = bni->bni_addr2;

  if (bk_netinfo_reset_primary_address(B, bni) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not reset the bni for searching\n");
    goto error;
  }

  for (bna = bk_netinfo_advance_primary_address(B, bni);
       bna;
       bna = bk_netinfo_advance_primary_address(B, bni))
  {
    switch(bs->bs_sa.sa_family)
    {
    case AF_INET:
      if ((bna->bna_type == BkNetinfoTypeInet) &&
	  !memcmp(&bs->bs_sin.sin_addr, &bna->bna_inet, bna->bna_len))
      {
	match = 1;
	goto done;
      }
      break;

    case AF_INET6:
      if ((bna->bna_type == BkNetinfoTypeInet) &&
	  !memcmp(&bs->bs_sin6.sin6_addr, &bna->bna_inet6, bna->bna_len))
      {
	match = 1;
	goto done;
      }
      break;

    case AF_LOCAL:
    if ((bna->bna_type != BkNetinfoTypeLocal) &&
	!strncmp(bs->bs_sun.sun_path, bna->bna_path, bna->bna_len))
      {
	match = 1;
	goto done;
      }
      break;
    }
  }

 done:
  // Reset the primary and secondary addresses.
  bni->bni_addr = addr;
  bni->bni_addr2 = addr2;

  BK_RETURN(B, match);

 error:
  if (addr)
    bni->bni_addr = addr;

  if (addr2)
    bni->bni_addr2 = addr2;

  BK_RETURN(B, -1);
}



/*
 * CLC netaddr comparison routines. NB these are ==/!= only. We don't care
 * about sorting (wouldn't make too much sense anyway)
 *
 * THREADS: MT-SAFE
 *
 */
static int bna_oo_cmp(void *a, void *b)
{
  struct bk_netaddr *bna1 = a, *bna2 = b;
  /* <WARNING> Should we require that bna_flags be equal too? </WARNING> */
  return (!(bna1->bna_type == bna2->bna_type &&
	    bna1->bna_len == bna2->bna_len &&
	    memcmp(&(bna1->bna_addr), &(bna2->bna_addr), bna1->bna_len) == 0));
}
static int bna_ko_cmp(void *a, void *b)
{
  return(bna_oo_cmp(a, b));
}
