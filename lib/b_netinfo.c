#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_netinfo.c,v 1.5 2001/11/16 16:03:41 jtt Exp $";
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

#include <libbk.h>
#include "libbk_internal.h"

/**
 * @file 
 * Stuff having to do with network information
 */

static int update_bni_pretty(bk_s B, struct bk_netinfo *bni);
static int bni2sin(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *bna, struct sockaddr_in *sin4, bk_flags flags);
static int bni2sin6(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *bna, struct sockaddr_in6 *sin6, bk_flags flags);
static int bni2un(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *bna, struct sockaddr_un *sun, bk_flags flags);


/**
 * Create a bk_netinfo struct
 *	@param B BAKA thread/global state.
 *	@returns <i>NULL</i> on failure.<br>
 *	@returns the @a struct @a bk_netinfo on success.
 */
struct bk_netinfo *
bk_netinfo_create(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netinfo *bni=NULL;

  if (!BK_CALLOC(bni))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not calloc bni: %s\n", strerror(errno));
    goto error;
  }

  if (!(bni->bni_addrs=netinfo_addrs_create(bna_oo_cmp,bna_ko_cmp, DICT_UNIQUE_KEYS)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create address list\n");
    goto error;
  }

  if (update_bni_pretty(B, bni)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not update bni pretty string\n");
    goto error;
  }

  BK_RETURN(B,bni);

 error:
  if (bni) bk_netinfo_destroy(B, bni);
  BK_RETURN(B,NULL);
}



/**
 * Destroy a baka netinfo struct.
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
    while((bna=netinfo_addrs_minimum(bni->bni_addrs)))
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
 *	@param B BAKA thread/global state.
 *	@param bni The @a netinfo in which to inser the @a netaddr.
 *	@param ibna The @a netaddr to insert.
 *	@param obna Will contain the @a netaddr which conflicts (if
 *	applicable). <em>Optional</em>
 *	@returns <i>-1</i> on failure.<br>
 *	@returns <i>0</i> on success.
 */
int
bk_netinfo_add_addr(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *ibna, struct bk_netaddr **obna)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netaddr *bna;
  int first_entry=0;

  if (!bni || !ibna)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (obna) *obna =NULL;			/* Initialize copyout */

  if ((bna=netinfo_addrs_minimum(bni->bni_addrs)) && 
      bna->bna_type != ibna->bna_type)
  {
    bk_error_printf(B, BK_ERR_ERR, "Baka netinfo structures do not support heterogenious address types (%d != %d)\n", bna->bna_type, ibna->bna_type);
    BK_RETURN(B,1); /* XXX What type of error is this? */
  }

  if (!netinfo_addrs_minimum(bni->bni_addrs))
  {
    first_entry=0;
  }

  if (netinfo_addrs_insert_uniq(bni->bni_addrs, ibna, (dict_obj *)obna) != DICT_OK)
  {
    if (obna && *obna)
    {
      bk_error_printf(B, BK_ERR_WARN, "Address %s already exists\n", (*obna)->bna_pretty);
      /* The fact that there the user has a copyout means this is OK */
      BK_RETURN(B,0); 
    }
    /* 
     * NB: If there was an object clash and the user did not supply obna,
     * it's essentially a fatal error.
     */
    bk_error_printf(B, BK_ERR_ERR, "Could not insert address\n");
    goto error;
  }

  ibna->bna_netinfo_addrs=bni->bni_addrs;

  if (first_entry)
  {
    if (bk_netinfo_set_primary_address(B, bni, NULL))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not set primary address\n");
      goto error;
    }
  }
  
  update_bni_pretty(B, bni);

  BK_RETURN(B,0);
  
 error:
  BK_RETURN(B,-1);
}  



/**
 * Delete a @a netaddr from a @a netinfo, returning the old structure in @a
 * obna (if supplied). This function does <em>not</em> clean up the @a
 * netaddr (since it can't be certain others don't have references to it),
 * so if you don't supply @a obna, then you'd better be certain that you
 * know what you are doing or face the possibility of a memory leak. Don't
 * tempt fate, supply @a obna.
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
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (obna) *obna=NULL;				/* Initialize copyout */

  if (!(bna=netinfo_addrs_search(bni->bni_addrs, ibna)))
  {
    bk_error_printf(B, BK_ERR_WARN, "Could not find address %s to delete\n", bna->bna_pretty);
    /* It's not fatal to not find something you were going to delete */
    BK_RETURN(B,0);
  }
  
  netinfo_addrs_delete(bni->bni_addrs, bna);

  /* 
   * If we're deleting one our primary or secondary addresses, make sure it
   * gets NULL'ed out.
   * XXX Should this return an error or special return value?
   */
   
  if (bna == bni->bni_addr) bni->bni_addr = NULL;
  if (bna == bni->bni_addr2) bni->bni_addr2 = NULL;

  if (update_bni_pretty(B,bni)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not update a bni pretty string: %s\n", strerror(errno));
    /* Whatever */
  }
  
  if (obna) *obna = bna;
  BK_RETURN(B,0);
}



/**
 * Update the pretty string of a @a netinfo.
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
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  if (bni->bni_pretty) free (bni->bni_pretty);
  bni->bni_pretty=NULL;

  snprintf(scratch,SCRATCHLEN, "[%s:%s:%s]", 
	   (bni->bni_addr && bni->bni_addr->bna_pretty)?bni->bni_addr->bna_pretty:"NO_ADDDR",
	   (bni->bni_bsi && bni->bni_bsi->bsi_servstr)?bni->bni_bsi->bsi_servstr:"NO_SERV", 
	   (bni->bni_bpi && bni->bni_bpi->bpi_protostr)?bni->bni_bpi->bpi_protostr:"NO_PROTO");
  if (!(bni->bni_pretty=strdup(scratch)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup bni pretty: %s\n", strerror(errno));
    goto error;
  }

  BK_RETURN(B,0);

 error:
  BK_RETURN(B,-1);
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
 */
int
bk_netinfo_set_primary_address(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *bna)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bni)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  if (bna)
  {
    bna=netinfo_addrs_search(bni->bni_addrs, bna);
  }
  else
  {
    bna=bk_netinfo_get_addr(B, bni);
  }

  if (!bna)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not determine bna to set as primary\n");
    goto error;
  }

  bni->bni_addr=bna;
    
  if (update_bni_pretty(B, bni)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not update bna pretty string\n");
    goto error;
  }

  BK_RETURN(B,0);

 error:
  BK_RETURN(B,-1);
  
}



/**
 * Reset the primary address.
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
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  bni->bni_addr=NULL;
  update_bni_pretty(B, bni);

  BK_RETURN(B,0);
}



/**
 * Clone a @a netinfo structure.
 *	@param B BAKA thread/global state.
 *	@param bni The source @a netinfo.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a @a new bk_netaddr on success.
 */
struct bk_netinfo *
bk_netinfo_clone (bk_s B, struct bk_netinfo *obni)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netinfo *nbni=NULL;
  struct bk_netaddr *obna, *nbna;

  if (!obni)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }
  
  if (!(nbni=bk_netinfo_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bni\n");
    goto error;    
  }

  nbni->bni_flags=obni->bni_flags;

  for (obna=netinfo_addrs_minimum(obni->bni_addrs);
       obna;
       obna=netinfo_addrs_successor(obni->bni_addrs, obna))
  {
    if (!(nbna=bk_netaddr_clone(B,obna)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not clone netaddr\n");
      goto error;
    }
    if (bk_netinfo_add_addr(B,nbni,nbna,NULL)<0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not insert netaddr into list\n");
      goto error;
    }
    if (obni->bni_addr == obna) nbni->bni_addr = nbna;
    if (obni->bni_addr2 == obna) nbni->bni_addr2 = nbna;
  }
  
  if (!(nbni->bni_bsi=bk_servinfo_clone(B,obni->bni_bsi)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not clone servinfo\n");
    goto error;
  }

  if (!(nbni->bni_bpi=bk_protoinfo_clone(B,obni->bni_bpi)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not clone protoinfo\n");
    goto error;
  }

  if (update_bni_pretty(B,nbni)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not update bni pretty string\n");
    goto error;
  }

  BK_RETURN(B,nbni);

 error:
  if (nbni) bk_netinfo_destroy(B, nbni);
  BK_RETURN(B,nbni);

}


/**
 * Update the servinfo part of netinfo based on a servent. 
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
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (bni->bni_bsi) bk_servinfo_destroy(B,bni->bni_bsi);
  if (!(bni->bni_bsi=bk_servinfo_serventdup(B, s, bni->bni_bpi)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not copy servent\n");
    goto error;
  }

  if (update_bni_pretty(B,bni))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not update bni pretty string\n");
    goto error;
  }

  BK_RETURN(B,0);

 error:
  BK_RETURN(B,-1);
}



/**
 * Update the protoinfo part of netinfo based on a protoent. 
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
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (bni->bni_bpi) bk_protoinfo_destroy(B,bni->bni_bpi);
  if (!(bni->bni_bpi=bk_protoinfo_protoentdup(B, p)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not clone protoent\n");
    goto error;
  }

  if (update_bni_pretty(B,bni))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not update bni pretty string\n");
    goto error;
  }

  BK_RETURN(B,0);

 error:
  BK_RETURN(B,-1);
}



/**
 * Update a @a bk_netinfo based on a hostent
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
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  if ((type=bk_netaddr_af2nat(B, h->h_addrtype))<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not convert af to netaddr type\n");
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
      for(ia=(struct in_addr **)(h->h_addr_list); *ia; ia++)
      {
	if (!(bna=bk_netaddr_addrdup(B, type, *ia, 0)))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not create bna from addr\n");
	  goto error;
	}
	if (bk_netinfo_add_addr(B,bni, bna, NULL)<0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not insert bna\n");
	  bk_netaddr_destroy(B,bna);		/* Must clean up */
	}
      }
    }
    break;
  case AF_INET6:
    {
      struct in6_addr **ia;
      for(ia=(struct in6_addr **)(h->h_addr_list); *ia; ia++)
      {
	if (!(bna=bk_netaddr_addrdup(B, type, *ia, 0)))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not create bna from addr\n");
	  goto error;
	}
	if (bk_netinfo_add_addr(B,bni, bna, NULL)<0)
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not insert bna\n");
	  bk_netaddr_destroy(B,bna);		/* Must clean up */
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
  if (update_bni_pretty(B, bni)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not update bni prett string\n");
    goto error;
  }

  BK_RETURN(B,0);

 error:
  BK_RETURN(B,-1);

}



/**
 * Retrieve the primary address
 *	@param B BAKA thread/global state.
 *	@param bni The netinfo from which to retrieve address.
 *	@return <i>NULL</i> on failure.<br>
 *	@return @a bk_netaddr pointer on success.
 * 	@bug There is no way to tell the difference between an error and @a
 * 	bk_netinfo with no addresses inserted.
 */
struct bk_netaddr *
bk_netinfo_get_addr(bk_s B, struct bk_netinfo *bni)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netaddr *bna=NULL;

  if (!bni)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }
  
  if (bni->bni_addr)
    bna=bni->bni_addr;
  else
    bna=netinfo_addrs_minimum(bni->bni_addrs);

  BK_RETURN(B,bna);
}



/**
 * Convert a netinfo to a sockaddr.
 *	@param B BAKA thread/global state.
 *	@param bni The @a bk_netinfo to use for the source.
 *	@param bna The @a bk_netaddr to use for the source (may be null).
 *	@param type Address type (may be BK_NETINFO_TYPE_UNKNOWN)
 *	@param sa The sockaddr to copy into.
 *	@param flags Random flags.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_netinfo_to_sockaddr(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *bna, bk_netaddr_type_t type, struct sockaddr *sa, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret;

  if (!bni || !sa)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!bna && !(bna=bk_netinfo_get_addr(B, bni)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not determine bk_netaddr to use\n");
  }
  
  /* If type is not specified pull it out of the bna */
  if (type == BK_NETINFO_TYPE_UNKNOWN)
  {
    type=bna->bna_type;
  }

  switch (bk_netaddr_nat2af(B, type))
  {
  case AF_INET:
    ret=bni2sin(B, bni, bna, (struct sockaddr_in *)sa, flags);
    break;
  case AF_INET6:
    ret=bni2sin6(B, bni, bna, (struct sockaddr_in6 *)sa, flags);
    break;
  case AF_LOCAL:
    ret=bni2un(B, bni, bna, (struct sockaddr_un *)sa, flags);
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
  
  BK_RETURN(B,ret);

 error:
  BK_RETURN(B,-1);
}



/**
 * Convert a @a bk_netinfo to a struct sockaddr_in
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
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  sin4->sin_family=AF_INET;

  BK_SET_SOCKADDR_LEN(B,sin4,bna->bna_len);
  
  if (!bna)
  {
    if (BK_FLAG_ISSET(flags, BK_NETINFO2SOCKADDR_FLAG_FUZZY_ANY))
    {
      sin4->sin_addr.s_addr=INADDR_ANY;
    }
    else
    {
      bk_error_printf(B, BK_ERR_ERR, "No address to use\n");
      goto error;
    }
  }
  else
  {
    /* 
     * sigh.. use bna length instead of sin length 'cause bloody linux
     * doesn't support sockaddr length. What modern OS doesn't do this??!!?
     */
    memmove(&(sin4->sin_addr),&(bna->bna_inet),bna->bna_len);
  }

  sin4->sin_port=bni->bni_bsi->bsi_port;

  BK_RETURN(B,0);

 error:
  BK_RETURN(B,-1);

}



/**
 * Convert a @a bk_netinfo to a struct sockaddr_in
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
  struct in6_addr in6any=IN6ADDR_ANY_INIT;

  if (!bni || !sin6)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  sin6->sin6_family=AF_INET;

  BK_SET_SOCKADDR_LEN(B,sin6,bna->bna_len);
  
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
    memmove(&(sin6->sin6_addr),&(bna->bna_inet),bna->bna_len);
  }

  sin6->sin6_port=bni->bni_bsi->bsi_port;

  BK_RETURN(B,0);

 error:
  BK_RETURN(B,-1);

}



/**
 * Convert a @a bk_netinfo to a struct sockaddr_un
 *	@param B BAKA thread/global state.
 *	@param bni The @a bk_netinfo to use for the source.
 *	@param bna The @a bk_netaddr to use for the source.
 *	@param sun The INET6 sockaddr to copy into.
 *	@param flags Random flags.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
bni2un(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *bna, struct sockaddr_un *sun, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  /* Fuzzy ANY does not make sense in AF_LOCAL */
  if (!bni || !bna || !sun)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  sun->sun_family=AF_LOCAL;
  
  BK_SET_SOCKADDR_LEN(B,sun,bna->bna_len);

  snprintf(sun->sun_path,sizeof(sun->sun_path),"%s", bna->bna_path);

  BK_RETURN(B,0);

 error:
  BK_RETURN(B,-1);

}



/**
 * Create a @a bk_netinfo from a sockaddr.
 *	@param B BAKA thread/global state.
 *	@param sa The sockaddr from which to create the @a bk_netinfo
 *	@param proto The protocol number.
 *	@return <i>NULL</i> on failure.<br>
 *	@return an allocated @a bk_netinfo.
 */
struct bk_netinfo *
bk_netinfo_from_sockaddr(bk_s B, int s, int proto, bk_socket_side_t side)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret;
  struct bk_netinfo *bni=NULL;
  struct bk_netaddr *bna=NULL;
  char scratch[100];
  bk_netaddr_type_t netaddr_type;
  struct sockaddr_in *sin4;
  struct sockaddr_in6 *sin6;
  struct sockaddr_un *sun;
  int len;
  struct sockaddr sa;

  switch (side)
  {
  case BK_SOCKET_SIDE_LOCAL:
    len=sizeof(sa);
    if (getsockname(s, &sa, &len))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not get local sockname: %s\n", strerror(errno));
      goto error;
    }
      
    break;

  case BK_SOCKET_SIDE_REMOTE:
    if (getpeername(s, &sa, &len))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not get local sockname: %s\n", strerror(errno));
      goto error;
    }
    break;

  default:
    bk_error_printf(B, BK_ERR_ERR, "Unknown sock side specifier: %d\n", side);
    goto error;
  }

  if (!(bni=bk_netinfo_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create bni\n");
    goto error;
  }

  netaddr_type=bk_netaddr_af2nat(B, sa.sa_family);

  switch (netaddr_type)
  {
  case BK_NETINFO_TYPE_INET:
  case BK_NETINFO_TYPE_INET6:
    if (!proto)
    {
      int type;

      len=sizeof(type);
      /* Guess the protocol */
      if (getsockopt(s, SOL_SOCKET, SO_TYPE, &type, &len)<0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not get socket type: %s\n", strerror(errno));
	goto error;
      }
      
      switch (type)
      {
      case SOCK_STREAM:
	proto=IPPROTO_TCP;
	break;
      case SOCK_DGRAM:
	proto=IPPROTO_UDP;
	break;
      default:
	bk_error_printf(B, BK_ERR_ERR, "Unknown or unsupported socket type: %d\n", type);
	goto error;
      }
    }

    snprintf(scratch,100, "%d", proto);
    if (bk_getprotobyfoo(B, scratch, NULL, bni, 0)<0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not set protoent\n");
      goto error;
    }

    snprintf(scratch,100,"%d", ntohs(sin4->sin_port));
    if (bk_getservbyfoo(B, scratch, bni->bni_bpi->bpi_protostr, NULL, bni, 0)<0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not set servent\n");
      goto error;
    }

    BK_GET_SOCKADDR_LEN(B,&sin4,len);
    if (!(bna=bk_netaddr_user(B, netaddr_type, &sa, len, 0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create netaddr\n");
      goto error;
    }
  
  case BK_NETINFO_TYPE_LOCAL:
    /* XXXX Do this for local type */
#if 0
    if (un2bni(B, bni, bna, (struct sockaddr_un *)sa, flags)<0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not convert bni to sockaddr_un\n");
      goto error;
    }
#endif
    break;

  default:
    bk_error_printf(B, BK_ERR_ERR, "Unsupported netaddr type: %d\n", netaddr_type);
    goto error;
    break;
  }

  if (bk_netinfo_add_addr(B, bni, bna, NULL)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert bna into bna\n");
    goto error;
  }

  update_bni_pretty(B, bni);

  BK_RETURN(B,bni);

 error:
  /* Despite appearences, this will *not* double free */
  if (bna) bk_netaddr_destroy(B,bna);
  if (bni) bk_netinfo_destroy(B,bni);
  BK_RETURN(B,NULL);
}



/*
 * CLC netaddr comparison routines. NB these are ==/!= only. We don't care
 * about sorting (wouldn't make too much sense anyway)
 */
static int bna_oo_cmp(void *a, void *b)
{
  struct bk_netaddr *bna1=a, *bna2=b;
  /* XXX Should we require that bna_flags be equal too? */
  return (!(bna1->bna_type == bna2->bna_type && 
	    bna1->bna_len == bna2->bna_len &&
	    memcmp(&(bna1->bna_addr), &(bna2->bna_addr), bna1->bna_len)==0));
} 
static int bna_ko_cmp(void *a, void *b)
{
  return(bna_oo_cmp(a,b));
} 
