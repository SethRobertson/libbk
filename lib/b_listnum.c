#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_listnum.c,v 1.4 2002/05/06 17:41:54 jtt Exp $";
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
 * Special purpose list software
 */

#include <libbk.h>
#include "libbk_internal.h"



/**
 * Information about list of lists
 */
struct bk_listnum_main
{
  dict_h	blm_list;		///< List of lists (bst?)
  bk_flags	blm_flags;		///< Saved flags
};



/**
 * @name Defines: listnum_kv_clc
 * listnum list CLC definitions
 * to hide CLC choice.
 */
// @{
#define listnum_create(o,k,f,a)		bst_create((o),(k),(f))
#define listnum_destroy(h)		bst_destroy(h)
#define listnum_insert(h,o)		bst_insert((h),(o))
#define listnum_insert_uniq(h,n,o)	bst_insert_uniq((h),(n),(o))
#define listnum_append(h,o)		bst_append((h),(o))
#define listnum_append_uniq(h,n,o)	bst_append_uniq((h),(n),(o))
#define listnum_search(h,k)		bst_search((h),(k))
#define listnum_delete(h,o)		bst_delete((h),(o))
#define listnum_minimum(h)		bst_minimum(h)
#define listnum_maximum(h)		bst_maximum(h)
#define listnum_successor(h,o)		bst_successor((h),(o))
#define listnum_predecessor(h,o)	bst_predecessor((h),(o))
#define listnum_iterate(h,d)		bst_iterate((h),(d))
#define listnum_nextobj(h,i)		bst_nextobj((h),(i))
#define listnum_iterate_done(h,i)	bst_iterate_done((h),(i))
#define listnum_error_reason(h,i)	bst_error_reason((h),(i))
static int listnum_oo_cmp(struct bk_listnum_head *a, struct bk_listnum_head *b);
static int listnum_ko_cmp(int *a, struct bk_listnum_head *b);
static ht_val listnum_obj_hash(struct bk_listnum_head *a);
static ht_val listnum_key_hash(int *a);
static struct ht_args listnum_args = { 512, 1, (ht_func)listnum_obj_hash, (ht_func)listnum_key_hash };
// @}



/**
 * Create the special purpose list management list.  Anyway, you need
 * to create one of these before you can get the actual lists.
 *
 * Why this use instead of CLC dicts or pq?  Well, one example is if
 * you are expiring very large numbers of events with a small (but
 * greater than one) number of different expiration times with
 * frequent list deletions (without having to track which list the
 * item is on), this can be *far* more efficient than normal time
 * based event expiration.
 *
 *	@param B BAKA Thread/global environment
 *	@param flags Fun for the future
 *	@return <i>NULL</i> on call failure, allocation failure.
 *	@return <br><i>new list management list</i> on success
 */
struct bk_listnum_main *bk_listnum_create(bk_s B, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_listnum_main *mainl;

  if (!BK_MALLOC(mainl))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate management list: %s\n", strerror(errno));
    goto error;
  }

  mainl->blm_flags = flags;

  if (!(mainl->blm_list = listnum_create((dict_function)listnum_oo_cmp, (dict_function)listnum_ko_cmp, DICT_BALANCED_TREE, &listnum_args)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create list management list: %s\n", listnum_error_reason(NULL, NULL));
    goto error;
  }

  BK_RETURN(B, mainl);

 error:
  if (mainl)
  {
    if (mainl->blm_list)
      listnum_destroy(mainl->blm_list);
    free(mainl);
  }
  BK_RETURN(B, NULL);
}



/**
 * Get the doubly linked list associated with "number".
 *
 *	@param B BAKA Thread/global environment
 *	@param mainl The list management list
 *	@param number The number for which you want the associated list head
 *	@param flags Fun for the future
 *	@return <i>NULL</i> on call failure, allocation failure.
 *	@return <br><i>list head object associated with number</i> on success
 */
struct bk_listnum_head *bk_listnum_get(bk_s B, struct bk_listnum_main *mainl, u_int number, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_listnum_head *head;

  if (!mainl)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  if (head = listnum_search(mainl->blm_list, &number))
  {						// Found one cached
    BK_RETURN(B, head);
  }

  if (!(BK_MALLOC(head)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate new list for %d: %s\n", number, strerror(errno));
    BK_RETURN(B, NULL);
  }
  head->blh_first = head;
  head->blh_last = head;
  head->blh_num = number;

  if (listnum_insert(mainl->blm_list, head) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert new item: %s\n", listnum_error_reason(mainl->blm_list, NULL));
    goto error;
  }

  BK_RETURN(B, head);

 error:
  if (head)
    free(head);
  BK_RETURN(B, NULL);
}



/**
 * Destroy the list management list and all sub-lists (but not the
 * items on the sub-lists!)
 *
 *	@param B BAKA Thread/global environment
 *	@param mainl The list management list
 */
void bk_listnum_destroy(bk_s B, struct bk_listnum_main *mainl)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_listnum_head *head;

  if (!mainl)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  DICT_NUKE(mainl->blm_list, listnum, head, bk_error_printf(B, BK_ERR_ERR, "Could not delete minimum: %s\n", listnum_error_reason(mainl->blm_list, NULL)); break, free(head));

  free(mainl);

  BK_VRETURN(B);
}



/**
 * Get all lists for all numbers, one at a time
 *
 *	@param B BAKA Thread/global environment
 *	@param mainl The list management list
 *	@param prev The last list returned by this function (NULL to start from the beginning)
 *	@return <i>NULL</i> on call failure, end-of-lists
 *	@return <br><i>next list</i> on success
 */
struct bk_listnum_head *bk_listnum_next(bk_s B, struct bk_listnum_main *mainl, struct bk_listnum_head *prev)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!mainl)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  if (prev)
  {
    BK_RETURN(B, listnum_successor(mainl->blm_list, prev));
  }

  BK_RETURN(B, listnum_minimum(mainl->blm_list));
}



/*
 * CLC routines
 */
static int listnum_oo_cmp(struct bk_listnum_head *a, struct bk_listnum_head *b)
{
  return(a->blh_num - b->blh_num);
}
static int listnum_ko_cmp(int *a, struct bk_listnum_head *b)
{
  return(*a - b->blh_num);
}
static ht_val listnum_obj_hash(struct bk_listnum_head *a)
{
  return(a->blh_num);
}
static ht_val listnum_key_hash(int *a)
{
  return(*a);
}
