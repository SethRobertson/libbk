#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_netaddr.c,v 1.13 2003/06/17 05:10:46 seth Exp $";
static const char libbk__copyright[] = "Copyright (c) 2001";
static const char libbk__contact[] = "<projectbaka@baka.org>";
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
 * All the voodoo to make @a bk_netaddrs work.
 */

#include <libbk.h>
#include "libbk_internal.h"



static struct bk_netaddr *bna_create(bk_s B);
static void bna_destroy(bk_s B, struct bk_netaddr *bna);
static int update_bna_pretty(bk_s B, struct bk_netaddr *bna);



/**
 * Create a @a struct @a bk_netaddr instance
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@returns <i>NULL</i> on failure. <br>
 *	@returns allocated @a struct @a bk_netaddr on success.
 */
static struct bk_netaddr *
bna_create(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netaddr *bna=NULL;

  if (!BK_CALLOC(bna))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bna: %s\n", strerror(errno));
    goto error;
  }

  bk_debug_printf_and(B, 128, "bna allocate: %p\n", bna);

  BK_RETURN(B, bna);

 error:
  if (bna) bk_netaddr_destroy(B, bna);
  BK_RETURN(B,NULL);
}



/**
 * Public interface to creating an empty @a bk_netaddr.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a bk_netaddr on success.
 */
struct bk_netaddr *
bk_netaddr_create(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netaddr *bna;

  if (!(bna = bna_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create bk_netaddr\n");
  }

  BK_RETURN(B, bna);
}



/**
 * Destroy a @a struct @a bk_netaddr
 *
 * THREADS: MT-SAFE (assuming different bna)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state
 *	@param bna The @a struct @a bk_netaddr to destroy.
 */
static void
bna_destroy(bk_s B, struct bk_netaddr *bna)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bna)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (bna->bna_netinfo_addrs) netinfo_addrs_delete(bna->bna_netinfo_addrs, bna);
  if (bna->bna_pretty) free (bna->bna_pretty);
  if (bna->bna_type == BkNetinfoTypeLocal && bna->bna_path)
    free(bna->bna_path);

  bk_debug_printf_and(B, 128, "bna free: %p\n", bna);

  free(bna);
  BK_VRETURN(B);
}



/**
 * Public interface to destroy a @a netaddr.
 *
 * THREADS: MT-SAFE (assuming different bna)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param bna The @a netaddr to nuke.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
void
bk_netaddr_destroy (bk_s B, struct bk_netaddr *bna)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bna)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  bna_destroy(B, bna);

  BK_VRETURN(B);
}



/**
 * Add an address to a @a struct @a netaddr. If @a len is 0, then @a len is
 * intuited from the type. Filename strings are bounded to MAXPATH.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param type The address type.
 *	@param addr The address data. (Stored in returned ptr)
 *	@param len The length of the object (zero to discover outselves)
 *	@param flags Everyone needs flags.
 *	@returns A new @a struct @a bk_netaddr pointer on success.<br>
 *	@returns <i>NULL</i> on failure.
 *	@bug Why do we accept the caller's word on the len when we can
 *	figure it out ourselves?
 */
struct bk_netaddr *
bk_netaddr_user(bk_s B, bk_netaddr_type_e type, void *addr, int len, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netaddr *bna = NULL;

  if (!addr)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  switch (type)
  {
  case BkNetinfoTypeInet:
    if (!len) len = sizeof(struct in_addr);
    break;

  case BkNetinfoTypeInet6:
    if (!len) len = sizeof(struct in6_addr);
    break;

  case BkNetinfoTypeLocal:
    if (!len)
    {
      if ((len=bk_strnlen(B, addr, PATH_MAX))<0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not determine the length of the AF_LOCAL address\n");
	goto error;
      }
    }
    else
    {
      if (len > PATH_MAX)
	bk_error_printf(B, BK_ERR_ERR, "Filename too long\n");
    }
    break;

  case BkNetinfoTypeEther:
    if (!len) len = sizeof(struct ether_addr);
    break;

  default:
    bk_error_printf(B, BK_ERR_ERR, "Unknown address type: %d\n", type);
    break;
  }

  if (!(bna = bna_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could note create bna\n");
    goto error;
  }

  bna->bna_type = type;
  bna->bna_flags = flags;
  bna->bna_len = len;

  if (type == BkNetinfoTypeLocal)
  {
    if (!(bna->bna_path = strdup(addr)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not strdup filename: %s\n", strerror(errno));
      goto error;
    }
  }
  else
  {
    memmove(&(bna->bna_addr), addr, len);
  }

  if (update_bna_pretty(B, bna) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not update pretty printing string\n");
    /*
     * While this might not seem like a good candidate for a fatal
     * error. Currently the only ways in which this function can fail are
     * sufficiently fatal
     */
    goto error;
  }
  BK_RETURN(B,bna);

 error:
  if (!bna) bk_netaddr_destroy(B, bna);
  BK_RETURN(B,NULL);
}



/**
 * Create a @a netaddr from a network address.  Duplicates address.
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state.
 *	@param type The BK_NETINFO_TYPE_* of the address.
 *	@param addr The address to copy.
 *	@param flags Initial flags.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a netaddr on success.
 */
struct bk_netaddr *
bk_netaddr_addrdup (bk_s B, bk_netaddr_type_e type, void *addr, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netaddr *bna=NULL;

  if (!addr)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!(bna = bna_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bna: %s\n", strerror(errno));
    goto error;
  }

  bna->bna_flags = flags;
  bna->bna_type = type;


  switch (type)
  {
  case BkNetinfoTypeInet:
    bk_debug_printf_and(B, 1, "Copying INET\n");
    memmove(&bna->bna_inet, addr, sizeof(struct in_addr));
    bna->bna_len=sizeof(struct in_addr);
    break;

#ifdef HAVE_INET6
  case BkNetinfoTypeInet6:
    bk_debug_printf_and(B, 1, "Copying INET6\n");
    memmove(&bna->bna_inet6, addr, sizeof(struct in6_addr));
    bna->bna_len=sizeof(struct in6_addr);
    break;
#endif

  case BkNetinfoTypeLocal:
    bk_debug_printf_and(B, 1, "Copying LOCAL\n");
    if (!(bna->bna_path=strdup((char *)addr)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not strdup addr path: %s\n", strerror(errno));
      goto error;
    }
    /* <WARNING> Should this be +1? </WARNING> */
    bna->bna_len=strlen((char *)addr);
    break;

  case BkNetinfoTypeEther:
    bk_debug_printf_and(B, 1, "Copying ETHER\n");
    memmove(&bna->bna_ether, addr, sizeof(struct ether_addr));
    bna->bna_len=sizeof(struct ether_addr);
    break;

  default:
    bk_error_printf(B, BK_ERR_ERR, "Unsupported BK_NETINFO_TYPE: %d\n", type);
    goto error;
    break;
  }

  if (update_bna_pretty(B, bna) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not update printable version of addr\n");
    goto error;
  }

  BK_RETURN(B,bna);

 error:
  if (bna) bna_destroy(B,bna);
  BK_RETURN(B,NULL);
}



/**
 * Update the "pretty printing" string associated with a @a netaddr.
 *
 * THREADS: MT-SAFE (assuming different bna)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param bna The @a netaddr to update.
 *	@returns <i>-1</i> on failure.<br>
 *	@returns <i>0</i> on success.
 */
static int
update_bna_pretty(bk_s B, struct bk_netaddr *bna)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char scratch[SCRATCHLEN];
  char scratch2[SCRATCHLEN2];

  /* BK_RETURN(B,0); */

  if (!bna)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (bna->bna_pretty) free(bna->bna_pretty);
  bna->bna_pretty=NULL;

  memset(scratch, 0, SCRATCHLEN);
  memset(scratch2, 0, SCRATCHLEN2);

  switch (bna->bna_type)
  {
  case BkNetinfoTypeInet:
    snprintf(scratch, SCRATCHLEN, "<AF_INET,%u.%u.%u.%u>",
	     ((unsigned char *)&bna->bna_inet)[0],
	     ((unsigned char *)&bna->bna_inet)[1],
	     ((unsigned char *)&bna->bna_inet)[2],
	     ((unsigned char *)&bna->bna_inet)[3]);
    break;

#ifdef HAVE_INET6
  case BkNetinfoTypeInet6:
    if (!inet_ntop(AF_INET6, &bna->bna_inet6, scratch2, SCRATCHLEN2))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not convert ip6 addr to string: %s\n", strerror(errno));
      goto error;
    }
    snprintf(scratch, SCRATCHLEN, "<AF_INET6,%s>", scratch2);
    break;
#endif

  case BkNetinfoTypeLocal:
    snprintf(scratch, SCRATCHLEN, "<AF_LOCAL,%s>", bna->bna_path);
    break;

  case BkNetinfoTypeEther:
    snprintf(scratch, SCRATCHLEN, "<AF_ETHER,%x:%x:%x:%x:%x:%x>",
	     ((unsigned char *)&bna->bna_ether)[0],
	     ((unsigned char *)&bna->bna_ether)[1],
	     ((unsigned char *)&bna->bna_ether)[2],
	     ((unsigned char *)&bna->bna_ether)[3],
	     ((unsigned char *)&bna->bna_ether)[4],
	     ((unsigned char *)&bna->bna_ether)[5]);
    break;

  default:
    break;
  }

  if (!(bna->bna_pretty=strdup(scratch)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup address info: %s\n", strerror(errno));
    goto error;
  }
  BK_RETURN(B,0);

 error:
  BK_RETURN(B,-1);
}



/**
 * Clone a @a netaddr. Allocates memory.
 *
 * THREADS: MT-SAFE (assuming different bna)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param obna @a netaddr to copy.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a netaddr on success.
 */
struct bk_netaddr *
bk_netaddr_clone (bk_s B, struct bk_netaddr *obna)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netaddr *nbna=NULL;

  if (!obna)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,NULL);
  }

  if (!(nbna = bna_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bna: %s\n", strerror(errno));
    goto error;
  }

  nbna->bna_flags = obna->bna_flags;
  nbna->bna_len = obna->bna_len;
  nbna->bna_type = obna->bna_type;

  switch (obna->bna_type)
  {
  case BkNetinfoTypeInet:
  case BkNetinfoTypeInet6:
  case BkNetinfoTypeEther:
    memmove(&(nbna->bna_addr), &(obna->bna_addr), obna->bna_len);
    break;

  case BkNetinfoTypeLocal:
    if (!(nbna->bna_path=strdup(obna->bna_path)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not strdup netaddr path: %s\n", strerror(errno));
      goto error;
    }

  default:
    bk_error_printf(B, BK_ERR_ERR, "Unknown addr type: %d\n", obna->bna_type);
    goto error;
    break;
  }

  if (update_bna_pretty(B, nbna) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not update bna pretty string: %s\n", strerror(errno));
    goto error;
  }

  BK_RETURN(B,nbna);

 error:
  if (nbna) bna_destroy(B, nbna);
  BK_RETURN(B,NULL);
}



/**
 * Convert address family to netinfo type.  Note this does not work for _TYPE_ETHER.
 *
 * THREADS: MT-REENTRANT
 *
 *	@param B BAKA thread/global state.
 *	@param af The address family.
 *	@return <i>-1</i> on failure.<br>
 *	@return BK_NETINFO_TYPE on success.
 */
bk_netaddr_type_e
bk_netaddr_af2nat(bk_s B, int af)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int type;

  switch (af)
  {
  case AF_INET:
    type=BkNetinfoTypeInet;
    break;

#ifdef AF_INET6
  case AF_INET6:
    type=BkNetinfoTypeInet6;
    break;
#endif

  case AF_LOCAL:
    type=BkNetinfoTypeLocal;
    break;

  default:
    bk_error_printf(B, BK_ERR_ERR, "Unsupported address family: %d\n", af);
    type=-1;
  }
  BK_RETURN(B,type);

}



/**
 * Convert netinfo type to address family.  Convert address family to
 * netinfo type.  Note this does not work for _TYPE_ETHER.
 *
 * THREADS: MT-REENTRANT
 *
 *	@param B BAKA thread/global state.
 *	@param af The address family.
 *	@return <i>-1</i> on failure.<br>
 *	@return BK_NETINFO_TYPE on success.
 */
int
bk_netaddr_nat2af(bk_s B, int type)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int af;

  switch (type)
  {
  case BkNetinfoTypeInet:
    af=AF_INET;
    break;

#ifdef AF_INET6
  case BkNetinfoTypeInet6:
    af=AF_INET6;
    break;
#endif

  case BkNetinfoTypeLocal:
    af=AF_LOCAL;
    break;

  case BkNetinfoTypeEther:
    bk_error_printf(B, BK_ERR_ERR, "ETHER has not address family\n");
    af=-1;
    break;

  default:
    bk_error_printf(B, BK_ERR_ERR, "Unsupported netaddr type: %d\n", type);
    af=-1;
  }
  BK_RETURN(B,af);
}
