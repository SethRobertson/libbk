#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_intinfo.c,v 1.2 2004/06/25 00:30:48 dupuy Exp $";
static const char libbk__copyright[] = "Copyright (c) 2003";
static const char libbk__contact[] = "<projectbaka@baka.org>";
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
 * b_intinfo.c: Routines for creating and processing network interface lists.
 */

#include <libbk.h>
#include "libbk_internal.h"
#include "libbk_net.h"
#include <net/if.h>

#define MAX_NUM_IFREQ 512

struct bk_name_value_map intinfo_ioctl_map[] =
{
  { "SIOCGIFADDR", 	SIOCGIFADDR 		},
  { "SIOCGIFBRDADDR", 	SIOCGIFBRDADDR 		},
  { "SIOCGIFCONF", 	SIOCGIFCONF		},
  { "SIOCGIFDSTADDR", 	SIOCGIFDSTADDR		},
  { "SIOCGIFFLAGS", 	SIOCGIFFLAGS		},
  { "SIOCGIFMETRIC", 	SIOCGIFMETRIC		},
  { "SIOCGIFMTU",	SIOCGIFMTU		},
  { "SIOCGIFNETMASK", 	SIOCGIFNETMASK		},
};




/**
 * @name Defines: intinfo_list clc
 * list of interface info structures
 */
// @{
#define intinfo_list_create(o,k,f)	dll_create((o),(k),(f))
#define intinfo_list_destroy(h)		dll_destroy(h)
#define intinfo_list_insert(h,o)	dll_insert((h),(o))
#define intinfo_list_insert_uniq(h,n,o)	dll_insert_uniq((h),(n),(o))
#define intinfo_list_append(h,o)	dll_append((h),(o))
#define intinfo_list_append_uniq(h,n,o)	dll_append_uniq((h),(n),(o))
#define intinfo_list_search(h,k)	dll_search((h),(k))
#define intinfo_list_delete(h,o)	dll_delete((h),(o))
#define intinfo_list_minimum(h)		dll_minimum(h)
#define intinfo_list_maximum(h)		dll_maximum(h)
#define intinfo_list_successor(h,o)	dll_successor((h),(o))
#define intinfo_list_predecessor(h,o)	dll_predecessor((h),(o))
#define intinfo_list_iterate(h,d)	dll_iterate((h),(d))
#define intinfo_list_nextobj(h,i)	dll_nextobj((h),(i))
#define intinfo_list_iterate_done(h,i)	dll_iterate_done((h),(i))
#define intinfo_list_error_reason(h,i)	dll_error_reason((h),(i))
// @}

static struct bk_interface_info *bii_create(bk_s B, bk_flags flags);
static void bii_destroy(bk_s B, struct bk_interface_info *bii);


/**
 * Create a baka interface info struct.
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new <i>bk_interface_info</i> on success.
 */
static struct bk_interface_info *
bii_create(bk_s B, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_interface_info *bii = NULL;

  if (!(BK_CALLOC(bii)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate interface info struct: %s\n", strerror(errno));
    goto error;
  }

  BK_RETURN(B, bii);  

 error:
  if (bii)
    bii_destroy(B, bii);
  
  BK_RETURN(B, NULL);  
}



/**
 * Destroy a baka interface info struct
 *
 *	@param B BAKA thread/global state.
 *	@param bii The @a bk_interface_info to nuke.
 */
static void
bii_destroy(bk_s B, struct bk_interface_info *bii)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bii)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (bii->bii_name)
    free(bii->bii_name);

  free(bii);

  BK_VRETURN(B);  
}



#define IF_GET_INFO(B, s, n, i) 							\
do {											\
  if (ioctl((s), (n), (i)) < 0)								\
  {											\
    bk_error_printf((B), BK_ERR_ERR, "Could not get information for %s ioctl\n",	\
		    bk_nvmap_value2name((B), intinfo_ioctl_map, (n)));			\
    goto error;										\
  }											\
} while(0)


/**
 * Create a list of baka interface info structures.
 *
 *	@param B BAKA thread/global state.
 *	@param pos_filter Interface flags which interface must match
 *	@param neg_filter Interface flags which interface must not match
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new <i>interface list</i> on success.
 */
bk_intinfo_list_t
bk_intinfo_list_create(bk_s B, int pos_filter, int neg_filter, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  dict_h bii_list = NULL;
  int s = -1;
  struct ifconf ifc;
  struct ifreq ifc_buffer[MAX_NUM_IFREQ];
  int num_ifreq;
  int cnt;
  struct bk_interface_info *bii = NULL;

  ifc.ifc_len = sizeof(ifc_buffer);
  ifc.ifc_buf = (char *) ifc_buffer;

  if ((s = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not open socket on ip protocol: %s\n", strerror(errno));
    goto error;
  }

  if (ioctl(s, SIOCGIFCONF, &ifc) < 0) 
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not obtain interface list from kernel: %s\n", strerror(errno));
    goto error;
  }

  if (!(bii_list = intinfo_list_create(NULL, NULL, DICT_UNORDERED)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create new interface info list: %s\n", intinfo_list_error_reason(NULL, NULL));
    goto error;
  }

  num_ifreq = ifc.ifc_len / sizeof(struct ifreq);

  for (cnt = 0 ; cnt < num_ifreq; cnt++)
  {
    struct ifreq *ifr = &ifc.ifc_req[cnt];

    IF_GET_INFO(B, s, SIOCGIFFLAGS, ifr);

    // If pos_filter is set, then all the flags in the filter must exist.
    if (pos_filter && ((ifr->ifr_flags & pos_filter) != pos_filter))
      continue;

    // If the neg_filter set then interfcast must not match *any* of the filter flags.
    if (neg_filter && (ifr->ifr_flags & neg_filter))
      continue;

    if (!(bii = bii_create(B, 0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create intinfo structure\n");
      goto error;
    }

    if (!(bii->bii_name = bk_strndup(B, ifr->ifr_name, IF_NAMESIZE)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not copy interface name: %s\n", strerror(errno));
      goto error;
    }

    bii->bii_flags = ifr->ifr_flags;

    IF_GET_INFO(B, s, SIOCGIFADDR, ifr);

    bii->bii_addr = ifr->ifr_addr;

    if (flags & IFF_BROADCAST)
    {
      IF_GET_INFO(B, s, SIOCGIFBRDADDR, ifr);
      
      bii->bii_broadaddr = ifr->ifr_broadaddr;
      
      BK_FLAG_SET(bii->bii_avail, BK_INTINFO_FIELD_BROADCAST);
    }

    if (flags & IFF_POINTOPOINT)
    {
      IF_GET_INFO(B, s, SIOCGIFDSTADDR, ifr);
      
      bii->bii_dstaddr = ifr->ifr_dstaddr;
      
      BK_FLAG_SET(bii->bii_avail, BK_INTINFO_FIELD_DSTADDR);
    }

    IF_GET_INFO(B, s, SIOCGIFNETMASK, ifr);

#ifdef ifr_netmask
    bii->bii_netmask = ifr->ifr_netmask;
#else
    bii->bii_netmask = ifr->ifr_addr;
#endif
    
    IF_GET_INFO(B, s, SIOCGIFMTU, ifr);
    
    bii->bii_mtu = ifr->ifr_mtu;
    
    IF_GET_INFO(B, s, SIOCGIFMETRIC, ifr);
    
    bii->bii_metric = ifr->ifr_metric;
    
    if (intinfo_list_insert(bii_list, bii) != DICT_OK)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not insert new intinfo into list: %s\n", intinfo_list_error_reason(bii_list, NULL));
      goto error;
    }

    bii = NULL;
  }

  close(s);
  s = -1;

  BK_RETURN(B, (bk_intinfo_list_t)bii_list);

 error:
  if (s != -1)
    close(s);

  if (bii_list)
    bk_intinfo_list_destroy(B, bii_list);

  if (bii)
    bii_destroy(B, bii);

  BK_RETURN(B, NULL);  
}



/**
 * Destroy a list of baka interface info strerror
 *
 *	@param B BAKA thread/global state.
 *	@param bii_list List to nuke.
 */
void
bk_intinfo_list_destroy(bk_s B, bk_intinfo_list_t list)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  dict_h bii_list = (dict_h)list;
  struct bk_interface_info *bii;

  if (!bii_list)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  while(bii = intinfo_list_minimum(bii_list))
  {
    if (intinfo_list_delete(bii_list, bii) != DICT_OK)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not delete intinfo from list: %s\n", intinfo_list_error_reason(bii_list, NULL));
      break;
    }
    bii_destroy(B, bii);
  }
  intinfo_list_destroy(bii_list);

  BK_VRETURN(B);  
}



/**
 * Get the first @a bk_interface_info in a list
 *
 *	@param B BAKA thread/global state.
 *	@param list The interface list.
 *	@return <i>NULL</i> if list is empty.
 *	@return the first <i>bk_interface_info</i> otherwise
 */
struct bk_interface_info *
bk_intinfo_list_minimum(bk_s B, bk_intinfo_list_t list)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  dict_h bii_list = (dict_h)list;

  if (!bii_list)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }
  
  BK_RETURN(B, intinfo_list_minimum(bii_list));
}



/**
 * Get the next @a bk_interface_info in a list
 *
 *	@param B BAKA thread/global state.
 *	@param list The interface list.
 *	@param bii The preceding @a bk_interface_info.
 *	@return <i>NULL</i> if there are no more elements
 *	@return the next <i>bk_interface_info</i> otherwise.
 */
struct bk_interface_info *
bk_intinfo_list_successor(bk_s B, bk_intinfo_list_t list, struct bk_interface_info *bii)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  dict_h bii_list = (dict_h)list;

  if (!bii_list || bii)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }
  
  BK_RETURN(B, intinfo_list_successor(bii_list, bii));
}



/**
 * Search for an interface by name
 *
 *	@param B BAKA thread/global state.
 *	@param list The interface list.
 *	@param bii The preceding @a bk_interface_info.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>bk_interface_info</i> on success
 */
struct bk_interface_info *
bk_intinfo_list_search(bk_s B, bk_intinfo_list_t list, const char *name, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  dict_h bii_list = (dict_h)list;
  struct bk_interface_info *bii;

  if (!bii_list || !name)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  for(bii = intinfo_list_minimum(bii_list);
      bii;
      bii = intinfo_list_successor(bii_list, bii))
  {
    if (BK_STREQ(bii->bii_name, name))
      break;
  }
  BK_RETURN(B, bii);  
}
