#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_netinfo.c,v 1.2 2001/11/08 23:11:47 jtt Exp $";
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

/**
 * Enum list of known network address types.
 */
typedef enum 
{ 
  BK_NETINFO_TYPE_IPV4,				/** IPv4 address */
  BK_NETINFO_TYPE_IPV6,				/** IPv6 address */
  BK_NETINFO_TYPE_LOCAL,			/** AF_LOCAL/AF_UNIX address */
  BK_NETINFO_TYPE_ETHER,			/** Ethernet address */
} bk_netaddr_type_t;


/** 
 * Everything you ever wanted to know about a network address. 
 */
struct bk_netaddr
{
  bk_flags		bna_flags;		/** Everyone needs flags */
  bk_netaddr_type_t	bna_type;		/** Type of address */
  u_int			bna_len;		/** Length of address */
  union
  {
    struct in_addr	bnau_ip;		/** IPv4 address */
    struct in6_addr	bnau_ip6;		/** IPv6 address */
    char *		bnau_filename;		/** AF_LOCAL/AF_UNIX address */
    struct ether_addr	bnau_ether;		/** Ethernet address */
  } bna_un;
  char *		bna_pretty;		/** Printable form of addr */
};


#define bna_ip		bna_un.bnau_ip
#define bna_ip6		bna_un.bnau_ip6
#define bna_filename	bna_un.bnau_filename
#define bna_ether	bna_un.bnau_ether

/**
 * Everything you ever wanted to know about networks (well not routing info)
 */
struct bk_netinfo
{
  bk_flags		bni_flags;		/** Everyone needs flags */
  struct bk_netaddr *	bni_addr;		/** Primary address */
  struct bk_netaddr *	bni_addr2;		/** Secondary address */
  dict_h		bni_addrs;		/** dll of addrs */
  u_short		bni_port;		/** Port */
  char			bni_proto;		/** Protocol */
  char *		bni_pretty;		/** Printable forms */
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

static int update_bna_pretty(bk_s B, struct bk_netaddr *bna);
static int update_bni_pretty(bk_s B, struct bk_netinfo *bni);

/**
 * Create a @a struct @a bk_netaddr instance
 *	@param B BAKA thread/global state.
 *	@returns <i>NULL</i> on failure. <br>
 *	@returns allocated @a struct @a bk_netaddr on success.
 */
struct bk_netaddr *
bk_netaddr_create(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netaddr *bna=NULL;

  if (!BK_CALLOC(bna))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bna: %s\n", strerror(errno));
    goto error;
  }

  /* Make sure this is initialized to something free(2)'able */
  if (!(bna->bna_pretty=strdup("")))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup netaddr pretty\n");
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
void
bk_netaddr_destroy(bk_s B, struct bk_netaddr *bna)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (bna)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (bna->bna_pretty) free (bna->bna_pretty);
  if (bna->bna_type == BK_NETINFO_TYPE_LOCAL && bna->bna_filename) 
    free(bna->bna_filename);
  free(bna);
  BK_VRETURN(B);
}



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

  if (BK_CALLOC(bni))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not calloc bni: %s\n", strerror(errno));
    goto error;
  }

  if (!(bni->bni_addrs=netinfo_addrs_create(bna_oo_cmp,bna_ko_cmp, DICT_UNIQUE_KEYS)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create address list\n");
    goto error;
  }

  /* Make sure this is initialized to something free(2)'able */
  if (!(bni->bni_pretty=strdup("")))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup netinfo pretty\n");
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
  
  while((bna=netinfo_addrs_minimum(bni->bni_addrs)))
  {
    netinfo_addrs_delete(bni->bni_addrs, bna);
    bk_netaddr_destroy(B, bna);
  }
  
  if (bni->bni_pretty) free (bni->bni_pretty);
  free(bni);
  BK_VRETURN(B);
}



/** 
 * Add an address to a @a struct @a netaddr. If @a len is 0, then @a len is
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
bk_netaddr_mkaddr(bk_s B, bk_netaddr_type_t type, void *addr, int len, bk_flags flags)
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
  case BK_NETINFO_TYPE_IPV4:
    if (!len) len = sizeof(struct in_addr);
    break;
  case BK_NETINFO_TYPE_IPV6:
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

  if (!(bna=bk_netaddr_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could note create bna\n");
    goto error;
  }

  bna->bna_type=type;
  bna->bna_flags=flags;
  bna->bna_len=len;

  if (type == BK_NETINFO_TYPE_LOCAL)
  {
    if (!(bna->bna_filename=strdup(addr)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not strdup filename: %s\n", strerror(errno));
      goto error;
    }
  }
  else
  {
    memmove(&(bna->bna_un), addr, len);
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


#define SCRATCHLEN (MAX(MAXPATHLEN,1024))
#define SCRATCHLEN2 (SCRATCHLEN-100)
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

  if (!bna)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  if (bna->bna_pretty) free(bna->bna_pretty);
  bna->bna_pretty=NULL;

  memset(scratch, SCRATCHLEN, 0);
  memset(scratch2, SCRATCHLEN2, 0);
  switch (bna->bna_type)
  {
  case BK_NETINFO_TYPE_IPV4:
    if (!inet_ntop(AF_INET, &bna->bna_ip, scratch2, SCRATCHLEN2))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not convert ip addr to string: %s\n", strerror(errno));
      goto error;
    }
    snprintf(scratch, SCRATCHLEN,"<AF_INET,%s>", scratch2);
    break;
  case BK_NETINFO_TYPE_IPV6:
    if (!inet_ntop(AF_INET6, &bna->bna_ip6, scratch2, SCRATCHLEN2))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not convert ip6 addr to string: %s\n", strerror(errno));
      goto error;
    }
    snprintf(scratch, SCRATCHLEN,"<AF_INET6,%s>", scratch2);
    break;
  case BK_NETINFO_TYPE_LOCAL:
    snprintf(scratch, SCRATCHLEN,"[AF_LOCAL,%s]", bna->bna_filename);
    break;
  case BK_NETINFO_TYPE_ETHER:
    snprintf(scratch, SCRATCHLEN,"[]");
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
bk_netaddr_delete_addr(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *ibna, struct bk_netaddr **obna)
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

  if (bni)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  if (bni->bni_pretty) free (bni->bni_pretty);
  bni->bni_pretty=NULL;

  snprintf(scratch,SCRATCHLEN, "[%s:%u:%u]", (bni->bni_addr)?bni->bni_addr->bna_pretty:"UNSET",bni->bni_port, bni->bni_proto);
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
bk_netinfo_set_primary_address(bk_s B, struct bk_netinfo *bni, struct bk_netaddr *ibna, struct bk_netaddr **obna)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_netaddr *bna=NULL;

  if (!bni || !ibna)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  if (obna) *obna=NULL;				/* Initialize copyout */

  if (bk_netinfo_add_addr(B, bni, ibna, &bna)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not pass netinfo address insertion\n");
    goto error;
  }
  

  if (!bna)					
  {
    /* We must have actually inserted ibna above */
    bna=ibna;
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

/*
 * CLC netaddr comparison routines. NB these are ==/!= only. We don't care
 * about sorting (wouldn't make too much sense anyway)
 */
static int bna_oo_cmp(void *a, void *b)
{
  struct bk_netaddr *bna1=a, *bna2=b;
  /* XXX Should we require that bna_flags be equal too? */
  return (bna1->bna_type == bna1->bna_type && 
	  bna1->bna_len == bna1->bna_len &&
	  memcmp(&(bna1->bna_un), &(bna2->bna_un), bna1->bna_len));
} 
static int bna_ko_cmp(void *a, void *b)
{
  return(bna_oo_cmp(a,b));
} 
