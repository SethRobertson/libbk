#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_netaddr.c,v 1.1 2001/11/13 03:38:04 jtt Exp $";
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
 * All of the baka run public and private functions.
 */

#include <libbk.h>
#include "libbk_internal.h"



/**
 * @file
 */

static struct bk_netaddr *bna_create(bk_s B);
static void bna_destroy(bk_s B, struct bk_netaddr *bna);
static int update_bna_pretty(bk_s B, struct bk_netaddr *bna);


/**
 * Create a @a struct @a bk_netaddr instance
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

  BK_RETURN(B,bna);

 error: 
  if (bna) bk_netaddr_destroy(B, bna);
  BK_RETURN(B,NULL);
}



/**
 * Destroy a @a struct @a bk_netaddr
 *	@param B BAKA thread/global state 
 *	@param bna The @a struct @a bk_netaddr to destroy.
 */
static void
bna_destroy(bk_s B, struct bk_netaddr *bna)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (bna)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (bna->bna_pretty) free (bna->bna_pretty);
  if (bna->bna_type == BK_NETINFO_TYPE_LOCAL && bna->bna_path) 
    free(bna->bna_path);
  free(bna);
  BK_VRETURN(B);
}



/**
 * Public interface to destroy a @a netaddr.
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



/** Add an address to a @a struct @a netaddr. If @a len is 0, then @a len is
 * intuited from the type. Filename strings are bounded to MAXPATH.
 *	@param B BAKA thread/global state.
 *	@param type The address type. 
 *	@param addr The address data.
 *	@param flags Everyone needs flags.
 *	@returns A new @a struct @a bk_netaddr pointer on success.<br>
 *	@returns <i>NULL</i> on failure.
 * 	@bug Why do we accept the caller's word on the len when we can
 * 	figure it out ourselves?
 */
struct bk_netaddr *
bk_netaddr_user(bk_s B, bk_netaddr_type_t type, void *addr, int len, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netaddr *bna=NULL;

  if (!addr)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  switch (type)
  {
  case BK_NETINFO_TYPE_INET:
    if (!len) len = sizeof(struct in_addr);
    break;
  case BK_NETINFO_TYPE_INET6:
    if (!len) len = sizeof(struct in6_addr);
    break;
  case BK_NETINFO_TYPE_LOCAL:
    if (!len) 
    {
      if ((len=bk_strnlen(B, addr, MAXPATHLEN))<0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not determine the length of the AF_LOCAL address\n");
	goto error;
      }
    }
    else
    {
      if (len > MAXPATHLEN)
	bk_error_printf(B, BK_ERR_ERR, "Filename too long\n");
    }
    break;
  case BK_NETINFO_TYPE_ETHER:
    if (!len) len = sizeof(struct ether_addr);
    break;
  default:
    bk_error_printf(B, BK_ERR_ERR, "Unknown address type: %d\n", type);
    break;
  }

  if (!(bna=bna_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could note create bna\n");
    goto error;
  }

  bna->bna_type=type;
  bna->bna_flags=flags;
  bna->bna_len=len;

  if (type == BK_NETINFO_TYPE_LOCAL)
  {
    if (!(bna->bna_path=strdup(addr)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not strdup filename: %s\n", strerror(errno));
      goto error;
    }
  }
  else
  {
    memmove(&(bna->bna_addr), addr, len);
  }

  if (update_bna_pretty(B, bna)<0)
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
 * Create a @a netaddr from a network address. 
 *	@param B BAKA thread/global state.
 *	@param type The BK_NETINFO_TYPE_* of the address.
 *	@param addr The address to copy.
 *	@param flags Initial flags.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a netaddr on success.
 */
struct bk_netaddr *
bk_netaddr_addrdup (bk_s B, int type, void *addr, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netaddr *bna=NULL;

  if (!addr)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,NULL);
  }
  
  if (!(bna=bna_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bna: %s\n", strerror(errno));
    goto error;
  }

  bna->bna_flags=flags;
  
  switch (type)
  {
  case BK_NETINFO_TYPE_INET:
    bk_debug_printf_and(B,1,"Copying INET");
    memmove(&bna->bna_inet, addr, sizeof(struct in_addr));
    break;
  case BK_NETINFO_TYPE_INET6:
    bk_debug_printf_and(B,1,"Copying INET6");
    memmove(&bna->bna_inet6, addr, sizeof(struct in6_addr));
    break;
  case BK_NETINFO_TYPE_LOCAL:
    bk_debug_printf_and(B,1,"Copying LOCAL");
    if (!(bna->bna_path=strdup((char *)addr)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not strdup addr path: %s\n", strerror(errno));
      goto error;
    }
    break;
  case BK_NETINFO_TYPE_ETHER:
    bk_debug_printf_and(B,1,"Copying ETHER");
    memmove(&bna->bna_ether, addr, sizeof(struct ether_addr));
    break;
  default: 
    bk_error_printf(B, BK_ERR_ERR, "Unsupported BK_NETINFO_TYPE: %d\n", type);
    goto error;
    break;
  }
  
  if (update_bna_pretty(B,bna)<0)
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
  case BK_NETINFO_TYPE_INET:
    if (!inet_ntop(AF_INET, &bna->bna_inet, scratch2, SCRATCHLEN2))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not convert ip addr to string: %s\n", strerror(errno));
      goto error;
    }
    snprintf(scratch, SCRATCHLEN,"<AF_INET:%s>", scratch2);
    break;
  case BK_NETINFO_TYPE_INET6:
    if (!inet_ntop(AF_INET6, &bna->bna_inet6, scratch2, SCRATCHLEN2))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not convert ip6 addr to string: %s\n", strerror(errno));
      goto error;
    }
    snprintf(scratch, SCRATCHLEN,"<AF_INET6:%s>", scratch2);
    break;
  case BK_NETINFO_TYPE_LOCAL:
    snprintf(scratch, SCRATCHLEN,"<AF_LOCAL:%s>", bna->bna_path);
    break;
  case BK_NETINFO_TYPE_ETHER:
    /* XXX Finish this */
    snprintf(scratch, SCRATCHLEN,"<ETHER:%s>", "");
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

  if (!(nbna=bna_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bna: %s\n", strerror(errno));
    goto error;
  }

  nbna->bna_flags=obna->bna_flags;
  nbna->bna_len=obna->bna_len;
  nbna->bna_type=obna->bna_type;
  
  switch (obna->bna_type)
  {
  case BK_NETINFO_TYPE_INET:
  case BK_NETINFO_TYPE_INET6:
  case BK_NETINFO_TYPE_ETHER:
    memmove(&(nbna->bna_addr), &(obna->bna_addr), obna->bna_len);
    break;

  case BK_NETINFO_TYPE_LOCAL:
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
  
  if (update_bna_pretty(B,nbna)<0)
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
 * Convert address family to netinfo type
 *	@param B BAKA thread/global state.
 *	@param af The address family.
 *	@return <i>-1</i> on failure.<br>
 *	@return BK_NETINFO_TYPE on success.
 */
int
bk_netaddr_af2nat(bk_s B, int af)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int type;
  
  switch (af)
  {
  case AF_INET:
    type=BK_NETINFO_TYPE_INET;
    break;
  case AF_INET6:
    type=BK_NETINFO_TYPE_INET6;
    break;
  case AF_LOCAL:
    type=BK_NETINFO_TYPE_LOCAL;
    break;
  default:
    bk_error_printf(B, BK_ERR_ERR, "Unsupported address family: %d\n", af);
    type=-1;
  }
  BK_RETURN(B,type);

}



/**
 * Convert netinfo type to address family.
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
  case BK_NETINFO_TYPE_INET:
    af=AF_INET;
    break;
  case BK_NETINFO_TYPE_INET6:
    af=AF_INET6;
    break;
  case BK_NETINFO_TYPE_LOCAL:
    af=AF_LOCAL;
    break;
  case BK_NETINFO_TYPE_ETHER:
    bk_error_printf(B, BK_ERR_ERR, "ETHER has not address family\n");
    af=-1;
  default:
    bk_error_printf(B, BK_ERR_ERR, "Unsupported netaddr type: %d\n", type);
    af=-1;
  }
  BK_RETURN(B,af);
}
