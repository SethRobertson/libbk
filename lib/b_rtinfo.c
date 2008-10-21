#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2004-2008";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2004-2008 The Authors. All rights reserved.
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
 *
 * b_intinfo.c: Routines for creating and processing network interface lists.
 */

#include <libbk.h>
#include "libbk_internal.h"
#include "libbk_net.h"



/**
 * @name Defines: intinfo_list clc
 * list of interface info structures
 */
// @{
#define rtinfo_list_create(o,k,f)	dll_create((o),(k),(f))
#define rtinfo_list_destroy(h)		dll_destroy(h)
#define rtinfo_list_insert(h,o)		dll_insert((h),(o))
#define rtinfo_list_insert_uniq(h,n,o)	dll_insert_uniq((h),(n),(o))
#define rtinfo_list_append(h,o)		dll_append((h),(o))
#define rtinfo_list_append_uniq(h,n,o)	dll_append_uniq((h),(n),(o))
#define rtinfo_list_search(h,k)		dll_search((h),(k))
#define rtinfo_list_delete(h,o)		dll_delete((h),(o))
#define rtinfo_list_minimum(h)		dll_minimum(h)
#define rtinfo_list_maximum(h)		dll_maximum(h)
#define rtinfo_list_successor(h,o)	dll_successor((h),(o))
#define rtinfo_list_predecessor(h,o)	dll_predecessor((h),(o))
#define rtinfo_list_iterate(h,d)	dll_iterate((h),(d))
#define rtinfo_list_nextobj(h,i)	dll_nextobj((h),(i))
#define rtinfo_list_iterate_done(h,i)	dll_iterate_done((h),(i))
#define rtinfo_list_error_reason(h,i)	dll_error_reason((h),(i))

static int rtinfo_oo_cmp(struct bk_route_info *a, struct bk_route_info *b);
static int rtinfo_ko_cmp(struct bk_route_info *a, struct bk_route_info *b);
// @}

static struct bk_route_info *bri_create(bk_s B, bk_flags flags);
static void bri_destroy(bk_s B, struct bk_route_info *bri);
static int obtain_route_table(bk_s B, dict_h bri_list, bk_flags flags);


/**
 * Create a @a bk_route_info.
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a nwe <i>bk_route_info</i> on success.
 */
static struct bk_route_info *
bri_create(bk_s B, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_route_info *bri = NULL;

  if (!(BK_CALLOC(bri)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create route info structure: %s\n", strerror(errno));
    goto error;
  }

  // We only support IP for now.

  BK_RETURN(B, bri);

 error:
  if (bri)
    bri_destroy(B, bri);

  BK_RETURN(B, NULL);
}



/**
 * Destroy a @a bk_route_info
 *
 *	@param B BAKA thread/global state.
 *	@param bri The @a bk_route_info to nuke.
 */
static void
bri_destroy(bk_s B, struct bk_route_info *bri)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bri)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (bri->bri_if_name)
    free((char *)bri->bri_if_name);

  free(bri);

  BK_VRETURN(B);
}


#define COMPUTE_NETMASK_LENGTH(bri)								\
do {												\
  u_int __mask = ntohl(((struct sockaddr_in *)(&(bri)->bri_mask))->sin_addr.s_addr);		\
												\
  for((bri)->bri_mask_len = 32; (bri)->bri_mask_len && !(__mask & 0x1); (bri)->bri_mask_len--)	\
    __mask = __mask >> 1;									\
} while(0);



/**
 * Create a list of routes
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
bk_rtinfo_list_t
bk_rtinfo_list_create(bk_s B, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  dict_h bri_list = NULL;

  if (!(bri_list = rtinfo_list_create((dict_function)rtinfo_oo_cmp, (dict_function)rtinfo_ko_cmp, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create route info list: %s\n", rtinfo_list_error_reason(NULL, NULL));
    goto error;
  }

  if (obtain_route_table(B, bri_list, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not obtain routes\n");
    goto error;
  }

  BK_RETURN(B, (bk_rtinfo_list_t)bri_list);

 error:
  if (bri_list)
    bk_rtinfo_list_destroy(B, bri_list);

  BK_RETURN(B, NULL);
}



/**
 * Destroy a list of route.
 *
 *	@param B BAKA thread/global state.
 *	@param rtlist The route list to nuke
 */
void
bk_rtinfo_list_destroy(bk_s B, bk_rtinfo_list_t rtlist)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  dict_h bri_list = (dict_h)rtlist;
  struct bk_route_info *bri;

  if (!bri_list)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  while(bri = rtinfo_list_minimum(bri_list))
  {
    if (rtinfo_list_delete(bri_list, bri) != DICT_OK)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not delete route info structure from list: %s\n", rtinfo_list_error_reason(bri_list, NULL));
      break;
    }
    bri_destroy(B, bri);
  }
  rtinfo_list_destroy(bri_list);

  BK_VRETURN(B);
}



/**
 * Get the first route in the list
 *
 *	@param B BAKA thread/global state.
 *	@param rtlist The route list.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return the first <i>bk_route_info</i> on success.
 */
struct bk_route_info *
bk_rtinfo_list_minimum(bk_s B, bk_rtinfo_list_t rtlist, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  dict_h bri_list = (dict_h)rtlist;

  if (!bri_list)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B, rtinfo_list_minimum(bri_list));
}



/**
 * Return the next route the list
 *
 *	@param B BAKA thread/global state.
 *	@param rtlist The route list .
 *	@param bri The preceeding route
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return the next <i>bk_route_info</i> on success.
 */
struct bk_route_info *
bk_rtinfo_list_successor(bk_s B, bk_rtinfo_list_t rtlist, struct bk_route_info *bri, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  dict_h bri_list = (dict_h)rtlist;

  if (!bri_list || !bri)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B, rtinfo_list_successor(bri_list, bri));
}



/**
 * Get a route to a destination. This function is a little bogus since some
 * OS's provide and API for this and it obviously makes much more sense to
 * use their lookups which will understand all the complexities of their
 * routing tables, but for consitency and speed of development we simply
 * run the process our selves. This function simple returns the first @a
 * bk_route_info struct which matches the destination provided, since the
 * insert order is supposed to list the routes in order of most specific to
 * least. If not route is found owing to a recect route, errno will be set
 * to ENETUNREACH.
 *
 *	@param B BAKA thread/global state.
 *	@param rtlist The list of routes.
 *	@param dst The @a in_addr which specifies the destination.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>bk_route_info</i> on success.
 */
struct bk_route_info *
bk_rtinfo_get_route(bk_s B, bk_rtinfo_list_t rtlist, struct in_addr *dst, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  dict_h bri_list = (dict_h)rtlist;
  struct bk_route_info *bri;

  if (!bri_list || !dst)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  for (bri = rtinfo_list_minimum(bri_list);
       bri;
       bri = rtinfo_list_successor(bri_list, bri))
  {
   if (BK_INET_NET_MATCH((&((struct sockaddr_in *)(&bri->bri_dst))->sin_addr), dst, (&((struct sockaddr_in *)(&bri->bri_mask))->sin_addr)))
    {
      if (bri->bri_flags & RTF_REJECT)
      {
	errno = ENETUNREACH;
	BK_RETURN(B, NULL);
      }
      BK_RETURN(B, bri);
    }
  }

  BK_RETURN(B, NULL);
}



/**
 * Get the a route as per @a bk_rtinf_get_rotue, but with string argument.
 *
 *	@param B BAKA thread/global state.
 *	@param rtlist The list of routes.
 *	@param dst_str The string version of destination
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
struct bk_route_info *
bk_rtinfo_get_route_by_string(bk_s B, bk_rtinfo_list_t rtlist, const char *dst_str, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct in_addr addr;

  if (!rtlist || !dst_str)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!inet_aton(dst_str, &addr))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not convert %s to address structure: %s\n", dst_str, strerror(errno));
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B, bk_rtinfo_get_route(B, rtlist, &addr, flags));
}



#ifdef HAVE__PROC_NET_ROUTE

#define NET_ROUTE_FILE "/proc/net/route"

/**
 * Method for getting routes if /proc/net/route exists.
 *
 *	@param B BAKA thread/global state.
 *	@param bri_list The empty list of routes to fill out.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
obtain_route_table(bk_s B, dict_h bri_list, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  FILE *fp = NULL;
  char line[1000];
  struct bk_route_info *bri = NULL;
  char **tokens = NULL;
  struct sockaddr_in *sock_in;

  if (!bri_list)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!(fp = fopen(NET_ROUTE_FILE, "r")))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not open %s: %s\n", NET_ROUTE_FILE, strerror(errno));
    goto error;
  }

  // Skip first line
  if (!fgets(line, sizeof(line), fp))
  {
    if (feof(fp))
      bk_error_printf(B, BK_ERR_ERR, "%s appears to be empty. This is not right\n", NET_ROUTE_FILE);
    else
      bk_error_printf(B, BK_ERR_ERR, "Error reading in header line of %s\n", NET_ROUTE_FILE);
  }

  while(fgets(line, sizeof(line), fp))
  {
    u_int32_t route_flags;

    if (!(bri = bri_create(B, 0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create route information\n");
      goto error;
    }

    if (!(tokens = bk_string_tokenize_split(B, line, 0, NULL, NULL, NULL, NULL, 0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not token line from %s\n", NET_ROUTE_FILE);
      goto error;
    }

    if (!(bri->bri_if_name = strdup(tokens[0])))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not copy interface name from route: %s\n", strerror(errno));
      goto error;
    }

    sock_in = (struct sockaddr_in *)(&bri->bri_dst);
    sock_in->sin_family = AF_INET;
    BK_SET_SOCKADDR_LEN(B, sock_in, sizeof(struct in_addr));
    if (bk_string_atou32(B, tokens[1], &sock_in->sin_addr.s_addr, BK_STRING_ATOI_FLAG_HEX) != 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not convert route destination to address\n");
      goto error;
    }

    sock_in = (struct sockaddr_in *)(&bri->bri_gateway);
    sock_in->sin_family = AF_INET;
    BK_SET_SOCKADDR_LEN(B, sock_in, sizeof(struct in_addr));
    if (bk_string_atou32(B, tokens[2], &sock_in->sin_addr.s_addr, BK_STRING_ATOI_FLAG_HEX) != 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not convert route gateway to address\n");
      goto error;
    }

    sock_in = (struct sockaddr_in *)(&bri->bri_mask);
    sock_in->sin_family = AF_INET;
    BK_SET_SOCKADDR_LEN(B, sock_in, sizeof(struct in_addr));
    if (bk_string_atou32(B, tokens[7], &sock_in->sin_addr.s_addr, BK_STRING_ATOI_FLAG_HEX) != 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not convert route netmask to address\n");
      goto error;
    }

    if (bk_string_atou32(B, tokens[3], &route_flags, BK_STRING_ATOI_FLAG_HEX) != 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not convert route flags\n");
      goto error;
    }

    bri->bri_flags = route_flags;

    COMPUTE_NETMASK_LENGTH(bri);

    if (rtinfo_list_insert(bri_list, bri) != DICT_OK)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not insert route info struct in list: %s\n", rtinfo_list_error_reason(bri_list, NULL));
      goto error;
    }
    bri = NULL;
  }

  if (ferror(fp))
  {
    bk_error_printf(B, BK_ERR_ERR, "An error occured while reading from %s: %s\n", NET_ROUTE_FILE, strerror(errno));
    goto error;
  }

  fclose(fp);
  fp = NULL;

  BK_RETURN(B, 0);

 error:
  if (fp)
    fclose(fp);

  if (bri)
    bri_destroy(B, bri);

  if (tokens)
    bk_string_tokenize_destroy(B, tokens);

  BK_RETURN(B, -1);
}

#else /* Do not have any route obtaining method for this platform */

/**
 * Default function for obtain_route_table. Simply returns an error.
 *
 *	@param B BAKA thread/global state.
 *	@param bri_list The empty list of routes to fill out.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
obtain_route_table(bk_s B, dict_h bri_list, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_route_info *bri = NULL;

  if (!bri_list)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!(bri = bri_create(B, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create route information\n");
    goto error;
  }

  if (rtinfo_list_insert(bri_list, bri) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert route info struct in list: %s\n", rtinfo_list_error_reason(bri_list, NULL));
    goto error;
  }

 error:
  BK_RETURN(B, -1);
}

#endif /* Platform specific route obtaining function */


// High --> low ordering of netmask length. Reject routes outweigh real routes.
static int rtinfo_oo_cmp(struct bk_route_info *a, struct bk_route_info *b)
{
  int ret;
  if (ret = b->bri_mask_len - a->bri_mask_len) return(ret);

  if (a->bri_flags & RTF_REJECT)
    return(-1);

  if (b->bri_flags & RTF_REJECT)
    return(1);

  return(0);
}
static int rtinfo_ko_cmp(struct bk_route_info *a, struct bk_route_info *b)
{
  return(rtinfo_oo_cmp(a, b));
}
