/*
 *
 *
 * ++Copyright BAKA++
 *
 * Copyright © 2003-2019 The Authors. All rights reserved.
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
 * Versions of common network structures
 */

#ifndef _libbk_inline_h_
#define _libbk_inline_h_

/*
 * This file provides baka with a "light weight" form of dll's which can in
 * particular cases (outlined below) replace CLC's dll's with (we hope)
 * significant performance gains. These functions are optmized for
 * unordered lists without unique keys, but this covers a surprising number
 * of cases (and you *can* use the for any dll you want). BAKA dll's also
 * have the drawback in that it gains some its performance at the cost of
 * intruding on your datastructures. You *must* have the first two fields
 * of DS be pointers which will form the links in the opposing
 * directions. Finally they suffer from the fact that you can only include
 * any given object in (exactly) one list; if you need to insert it in more
 * than one than you have to use normal CLC dll's for all but one.
 *
 * You make BAKA dll's look "just like" CLC dll's via the usual #define
 * trickery. Indeed you should.
 *
 * For performance and API reasons these functions are intionally *not*
 * BAKAfied. In addition we maintain *full* CLC bugability in the sense
 * that we also do not check arguments and therefore NULL args cause core
 * dumps. The reason for this is simple. In functions which basically do
 * nothing but but return an offset into its argument, checking for NULL
 * adds *considerable* overhead (probably close to doubling the
 * time). Since one of the primary purposes of these routines is for them
 * to be as fast as possible, we avoid the overhead of arg checking (and as
 * I noted above coincidently become core-dump compatible with CLC.
 */

extern int dict_errno;

/*
 * Generic dll handle
 *
 * There are no flags here (and please avoid creating any if possible)
 * owing to the fact that to support flags we would have to "violate" the
 * "clean replacement of CLC" philosophy -- unless we supported the DICT_
 * flags, but that seems too grotty for words.
 */
struct bk_generic_dll_handle
{
  struct bk_generic_dll_element *	gdh_head; ///< The head of the list.
  struct bk_generic_dll_element *	gdh_tail; ///< The tail of the list.
  dict_function				gdh_oo_cmp; ///< The object-object function
  dict_function				gdh_ko_cmp; ///< The key object function.
  int					gdh_flags; ///< Dict flags (hence not bk_flags type)
  int					gdh_errno; ///< Dict errno value.
};



/*
 * An element of the generic dll.
 *
 * NB: THIS STRUCTURE IS OVERLAYED ON THE CALLER'S STRUCTURE, SO THE
 * CALLER'S STRUCT *MUST* ALLOW FOR THIS.
 */
struct bk_generic_dll_element
{
  struct bk_generic_dll_element *	gde_next; ///< Forward link;
  struct bk_generic_dll_element *	gde_prev; ///< Reverse link;
};



struct bk_dll_iterator
{
  struct bk_generic_dll_element *	di_current; ///< The currently returned element.
  enum dict_direction			di_direction; ///< The direction which we iterate.
};


static __inline__ dict_h	bk_dll_create(dict_function oo_cmp, dict_function ko_cmp, int flags);
static __inline__ void		bk_dll_destroy(dict_h handle);
static __inline__ int		bk_dll_insert(dict_h handle, dict_obj obj);
static __inline__ int		bk_dll_insert_uniq(dict_h handle, dict_obj obj, dict_obj *oldp);
static __inline__ int		bk_dll_append(dict_h handle, dict_obj obj);
static __inline__ int		bk_dll_append_uniq(dict_h handle, dict_obj obj, dict_obj *oldp);
static __inline__ int		bk_dll_delete(dict_h handle, dict_obj obj);
static __inline__ dict_obj	bk_dll_minimum(dict_h handle);
static __inline__ dict_obj	bk_dll_maximum(dict_h handle);
static __inline__ dict_obj	bk_dll_successor(dict_h handle, dict_obj obj);
static __inline__ dict_obj	bk_dll_predecessor(dict_h handle, dict_obj obj);
static __inline__ dict_obj	bk_dll_search(dict_h handle, dict_key key);
static __inline__ dict_iter	bk_dll_iterate(dict_h handle, enum dict_direction direction);
static __inline__ void		bk_dll_iterate_done(dict_h handle, dict_iter iter);
static __inline__ dict_obj	bk_dll_nextobj(dict_h handle, dict_iter iter);
/* defined in b_dll.c */
extern char *			bk_dll_error_reason(dict_h handle, int *errnop);
extern int			bk_dll_insert_internal(struct bk_generic_dll_handle *gdh, struct bk_generic_dll_element *gde, struct bk_generic_dll_element **old_gdep, int flags, int append);


/**
 * Destroy a generic baka dll
 *
 *	@param gdh The handle of the geneneric list.
 */
static __inline__ void
bk_dll_destroy(dict_h handle)
{
  struct bk_generic_dll_handle *gdh = (struct bk_generic_dll_handle *)handle;

  if (gdh)
    free(gdh);

  return;
}



/**
 * Create the handle for a BAKA dll. NB: the usually expected "flags"
 * argument is not set here since it is assumed that this function might
 * replace dll_create in some code and the flags thus passed would be DICT
 * flags which have no meaning.
 *
 *	@param oo_cmp The "object-object" comparison routine (saved but currently ignored).
 *	@param ko_cmp The "key-object" comparison routine (only needed for searching).
 *	@param flags The CLC flags (currently ignored).
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new generic dll handle on success.
 */
static __inline__ dict_h
bk_dll_create(dict_function oo_cmp, dict_function ko_cmp, int flags)
{
  struct bk_generic_dll_handle *gdh = NULL;

  if (!(gdh = malloc(sizeof(*gdh))))
  {
    // Dict assumes (proabably correctly) that *any* malloc failure is ENOMEM
    dict_errno = DICT_ENOMEM;
    goto error;
  }

  // Unlike normal CLC dll we assume *unordered* lists.
  // <TRICKY> and turn off all flags *except* the ones we deal with. </TRICKY>
  flags &= (DICT_ORDERED | DICT_UNIQUE_KEYS);

  gdh->gdh_oo_cmp = oo_cmp;
  gdh->gdh_ko_cmp = ko_cmp;
  gdh->gdh_flags = flags;
  gdh->gdh_errno = 0;
  gdh->gdh_head = NULL;
  gdh->gdh_tail = NULL;

  if ((flags & (DICT_ORDERED || DICT_UNIQUE_KEYS)) && !oo_cmp)
  {
    dict_errno = DICT_ENOOOCOMP;
    goto error;
  }

  return((dict_h)gdh);

 error:
  if (gdh)
    bk_dll_destroy((dict_h)gdh);
  return(NULL);
}



#define BK_DLL_INSERT_BEFORE(handle1,new1,cur1)				\
do									\
{									\
  struct bk_generic_dll_handle *handle = handle1;			\
  struct bk_generic_dll_element *new = new1;				\
  struct bk_generic_dll_element *cur = cur1;				\
									\
  if (!cur)								\
  {									\
    handle->gdh_head = new;						\
    handle->gdh_tail = new;						\
  }									\
  else									\
  {									\
    new->gde_next = cur;						\
    if (cur->gde_prev)							\
      cur->gde_prev->gde_next = new;					\
    else								\
      handle->gdh_head = new;						\
    new->gde_prev = cur->gde_prev;					\
    cur->gde_prev = new;						\
  }									\
} while(0)



#define BK_DLL_INSERT_AFTER(handle1,new1,cur1)				\
do									\
{									\
  struct bk_generic_dll_handle *handle = handle1;			\
  struct bk_generic_dll_element *new = new1;				\
  struct bk_generic_dll_element *cur = cur1;				\
									\
  if (!cur)								\
  {									\
    handle->gdh_head = new;						\
    handle->gdh_tail = new;						\
  }									\
  else									\
  {									\
    new->gde_prev = cur;						\
    if (cur->gde_next)							\
      cur->gde_next->gde_prev = new;					\
    else								\
      handle->gdh_tail = new;						\
    new->gde_next = cur->gde_next;					\
    cur->gde_next = new;						\
  }									\
} while (0)






/**
 * Insert a dll element onto the list. In keeping with CLC insert
 * prepends. Again flags is supporessed to keep this in line with the CLC
 * APi.
 *
 *	@param handle The handle of dll.
 *	@param obj The object to insert.
 *	@return !<i>DICT_ERR</i> on failure.<br>
 *	@return <i>DICT_OK</i> on success.
 */
static __inline__ int
bk_dll_insert(dict_h handle, dict_obj obj)
{
  struct bk_generic_dll_handle *gdh = (struct bk_generic_dll_handle *)handle;
  struct bk_generic_dll_element *gde = (struct bk_generic_dll_element *)obj;

  return(bk_dll_insert_internal(gdh, gde, NULL, gdh->gdh_flags, 0));
}



/**
 * Insert a dll element onto the list. In keeping with CLC insert
 * prepends. Again flags is supporessed to keep this in line with the CLC
 * APi.
 *
 *	@param handle The handle of dll.
 *	@param obj The object to insert.
 *	@return !<i>DICT_ERR</i> on failure.<br>
 *	@return <i>DICT_OK</i> on success.
 */
static __inline__ int
bk_dll_insert_uniq(dict_h handle, dict_obj obj, dict_obj *old_objp)
{
  struct bk_generic_dll_handle *gdh = (struct bk_generic_dll_handle *)handle;
  struct bk_generic_dll_element *gde = (struct bk_generic_dll_element *)obj;
  struct bk_generic_dll_element **old_gdep = (struct bk_generic_dll_element **)old_objp;

  return(bk_dll_insert_internal(gdh, gde, old_gdep, (DICT_UNIQUE_KEYS | gdh->gdh_flags), 0));
}



/**
 * Insert a dll element onto the list. In keeping with CLC insert
 * prepends. Again flags is supporessed to keep this in line with the CLC
 * APi.
 *
 *	@param handle The handle of dll.
 *	@param obj The object to insert.
 *	@return !<i>DICT_ERR</i> on failure.<br>
 *	@return <i>DICT_OK</i> on success.
 */
static __inline__ int
bk_dll_append(dict_h handle, dict_obj obj)
{
  struct bk_generic_dll_handle *gdh = (struct bk_generic_dll_handle *)handle;
  struct bk_generic_dll_element *gde = (struct bk_generic_dll_element *)obj;

  return(bk_dll_insert_internal(gdh, gde, NULL, gdh->gdh_flags, 1));
}




/**
 * Insert a dll element onto the list. In keeping with CLC insert
 * prepends. Again flags is supporessed to keep this in line with the CLC
 * APi.
 *
 *	@param handle The handle of dll.
 *	@param obj The object to insert.
 *	@return !<i>DICT_ERR</i> on failure.<br>
 *	@return <i>DICT_OK</i> on success.
 */
static __inline__ int
bk_dll_append_uniq(dict_h handle, dict_obj obj, dict_obj *old_objp)
{
  struct bk_generic_dll_handle *gdh = (struct bk_generic_dll_handle *)handle;
  struct bk_generic_dll_element *gde = (struct bk_generic_dll_element *)obj;
  struct bk_generic_dll_element **old_gdep = (struct bk_generic_dll_element **)old_objp;

  return(bk_dll_insert_internal(gdh, gde, old_gdep, (DICT_UNIQUE_KEYS | gdh->gdh_flags), 1));
}




/**
 * Delete an element from the list.
 *
 *	@param handle The handle of dll (not used but needed to keep the dict API).
 *	@param obj The object to insert.
 *	@return <i>DICT_ERR</i> on failure.<br>
 *	@return <i>DICT_OK</i> on success.
 */
static __inline__ int
bk_dll_delete(dict_h handle, dict_obj obj)
{
  struct bk_generic_dll_handle *gdh = (struct bk_generic_dll_handle *)handle;
  struct bk_generic_dll_element *gde = (struct bk_generic_dll_element *)obj;

  if (gde->gde_next)
    gde->gde_next->gde_prev = gde->gde_prev;
  else
    gdh->gdh_tail = gde->gde_prev;

  if (gde->gde_prev)
    gde->gde_prev->gde_next = gde->gde_next;
  else
    gdh->gdh_head = gde->gde_next;

  gde->gde_next = gde->gde_prev = NULL;

  return(DICT_OK);
}



/**
 * Find the minimum node in the dll.,
 *
 *	@param handle The handle of dll.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>node</i> on success.
 */
static __inline__ dict_obj
bk_dll_minimum(dict_h handle)
{
  struct bk_generic_dll_handle *gdh = (struct bk_generic_dll_handle *)handle;

  return(gdh->gdh_head);
}



/**
 * Find the maximum node in the dll.,
 *
 *	@param handle The handle of dll.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>node</i> on success.
 */
static __inline__ dict_obj
bk_dll_maximum(dict_h handle)
{
  struct bk_generic_dll_handle *gdh = (struct bk_generic_dll_handle *)handle;

  return(gdh->gdh_tail);
}





/**
 * Find the successor of obj in the dll.
 *
 *	@param handle The handle of dll (unneeded and declared for API matching)
 *	@param obj The current object.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>node</i> on success.
 */
static __inline__ dict_obj
bk_dll_successor(dict_h handle, dict_obj obj)
{
  struct bk_generic_dll_element *gde = (struct bk_generic_dll_element *)obj;

  return(gde->gde_next);
}




/**
 * Find the predecessor of obj in the dll.
 *
 *	@param handle The handle of dll (unneeded and declared for API matching)
 *	@param obj The current object.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>node</i> on success.
 */
static __inline__ dict_obj
bk_dll_predecessor(dict_h handle, dict_obj obj)
{
  struct bk_generic_dll_element *gde = (struct bk_generic_dll_element *)obj;

  return(gde->gde_prev);
}



/**
 * Search for a key within a bk_dll
 *
 *	@param handle The bk_dll handle
 *	@param keyp Pointer to the key value.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static __inline__ dict_obj
bk_dll_search(dict_h handle, dict_key key)
{
  struct bk_generic_dll_handle *gdh = (struct bk_generic_dll_handle *)handle;
  struct bk_generic_dll_element *gde;

  for(gde = gdh->gdh_head; gde; gde = gde->gde_next)
    if (!(*gdh->gdh_ko_cmp)(key,(dict_obj)gde))
      break;
  return((dict_obj)gde);
}



/**
 * Allocate a baka dll iterator.
 *
 * Note on bk_dll iterators: These exist for purposes of "dropping" in the
 * bk_dll stuff to replace an existing CLC. But the purpose of the CLC
 * iterator is to allow you the obtain the next element in the CLC without
 * having to search for the "current" value (using clc_successor(h, o)
 * sometimes requires the CLC to first locate o, using a linear
 * search). Since bk_dll's use the acutal object as the vessle for the back
 * and forward pointers, there is never any need to search even when using
 * bk_dll_successor()/predecessor(). *However*
 * bk_dll_successor()/predecessor() do have the advantage of not having to
 * allocate a free state when they are used. Therefore they are preferable.
 *
 *	@param handle The handle of dll (unneeded and declared for API matching)
 *	@param direction The direction for nextobj.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>node</i> on success.
 */
static __inline__ dict_iter
bk_dll_iterate(dict_h handle, enum dict_direction direction)
{
  struct bk_generic_dll_handle *gdh = (struct bk_generic_dll_handle *)handle;
  struct bk_dll_iterator *di = NULL;

  if (!(di = malloc(sizeof(struct bk_dll_iterator))))
    return((dict_iter)NULL);

  switch (direction)
  {
  case DICT_FROM_START:
    di->di_current = gdh->gdh_head;
    break;
  case DICT_FROM_END:
    di->di_current = gdh->gdh_tail;
    break;
  default:
    return((dict_iter)NULL);
  }
  di->di_direction = direction;
  return((dict_iter)di);
}



/**
 * Destroy the iterator.
 *
 *	@param iter Iterator to destroy
 */
static __inline__ void
bk_dll_iterate_done(dict_h handle, dict_iter iter)
{
  // All seatbelts off..
  free(iter);
  return;
}



/**
 * Get the next object in a list
 *
 *	@param handle The bk_dll handle
 *	@param iter The iterator in use.
 *	@return <i>NULL</i> when list is at the end.<br>
 *	@return <i>obj</i> on success.
 */
static __inline__ dict_obj
bk_dll_nextobj(dict_h handle, dict_iter iter)
{
  struct bk_dll_iterator *di = (struct bk_dll_iterator *)iter;
  dict_obj cur;

  // All seatbelts off;

  if (!(cur = di->di_current))
    return(cur);

  switch (di->di_direction)
  {
  case DICT_FROM_START:
    di->di_current = bk_dll_successor(handle, di->di_current);
    break;
  case DICT_FROM_END:
    di->di_current = bk_dll_predecessor(handle, di->di_current);
    break;
  default:
    return((dict_obj)NULL);
  }

  return(cur);
}


#endif /* _libbk_inline_h_ */
