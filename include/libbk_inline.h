/*
 * $Id: libbk_inline.h,v 1.8 2003/06/24 22:16:02 jtt Exp $
 *
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
 * Versions of common network structures
 */

#ifndef _libbk_inline_h_
#define _libbk_inline_h_


/*
 * This file provides baka with a "light weight" form of dll's which can in
 * particular cases (outlined below) replace CLC's dll's with (we hope)
 * significant performance gains. The limitations on the use are that we
 * only support unordered lists without either unique keys or searching
 * available (ie in CLC speak there are no object-object or key-object
 * compares). Thus this is possible when you need only insert/delete and
 * iteratate. But this covers a surprising number of cases. BAKA dll's also
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
 * BAKAfied. In addition we maintain *full* CLCL compatibility in the sense
 * that we also do not check arguments and therefore NULL args cause core
 * dumps. The reason for this is simple. In functions which basically do
 * nothing but but return an offset into its argument, checking for NULL
 * adds *considerable* overhead (probably close to doubling the
 * time). Since one of the primary purposes of these routines is for them
 * to be as fast as possible, we avoid the overhead of arg checking (and as
 * I noted above coincidently become core-dump compatible with CLCL
 */

typedef void *		bk_dll_h;		///< Opaque handle to "generic" baka dll.


/*
 * Generic dll header
 *
 * There are no flags here (and please avoid creating any if possible)
 * owing to the fact that to support flags we would have to "violate" the
 * "clean replacement of CLC" philosophy -- unless we supported the DICT_
 * flags, but that seems too grotty for words.
 */
struct bk_generic_dll_header
{
  struct bk_generic_dll_element *	gdh_head; ///< The head of the list.
  struct bk_generic_dll_element *	gdh_tail; ///< The tail of the list.
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
  enum dict_direction 			di_direction; ///< The direction which we iterate.
};


static __inline__ bk_dll_h bk_dll_create(void);
static __inline__ void bk_dll_destroy(bk_dll_h gdh);
static __inline__ int bk_dll_insert(bk_dll_h header, void *obj);
static __inline__ int bk_dll_append(bk_dll_h header, void *obj);
static __inline__ int  bk_dll_delete(bk_dll_h header, void *obj);
static __inline__ void *bk_dll_minimum(bk_dll_h header);
static __inline__ void *bk_dll_maximum(bk_dll_h header);
static __inline__ void *bk_dll_successor(bk_dll_h header, void *obj);
static __inline__ void *bk_dll_predecessor(bk_dll_h header, void *obj);
static __inline__ dict_iter bk_dll_iterate(bk_dll_h header, enum dict_direction direction);
static __inline__ void bk_dll_iterate_done(dict_iter iter);
static __inline__ dict_obj bk_dll_nextobj(dict_h header, dict_iter iter);


/**
 * Create the header for a BAKA dll. NB: the usually expected "flags"
 * argument is not set here since it is assumed that this function might
 * replace dll_create in some code and the flags thus passed would be DICT
 * flags which have no meaning.
 *
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new generic dll handle on success.
 */
static __inline__ bk_dll_h
bk_dll_create(void)
{
  struct bk_generic_dll_header *gdh = NULL;
 
  if (!(gdh = calloc(1, sizeof(*gdh))))
    goto error;

  return((bk_dll_h)gdh); 
 
 error:
  if (gdh)
    bk_dll_destroy(gdh);
  return(NULL); 
}


/**
 * Destroy a generic baka dll
 *
 *	@param gdh The header of the geneneric list.
 */
static __inline__ void
bk_dll_destroy(bk_dll_h gdh)
{
  if (gdh)
    free(gdh);
  return;
}



/**
 * Insert a dll element onto the list. In keeping with CLC insert
 * prepends. Again flags is supporessed to keep this in line with the CLC
 * APi.
 *
 *	@param header The header of dll.
 *	@param obj The object to insert.
 *	@return !<i>DICT_ERR</i> on failure.<br>
 *	@return <i>DICT_OK</i> on success.
 */
static __inline__ int
bk_dll_insert(bk_dll_h header, void *obj)
{
  struct bk_generic_dll_header *gdh = (struct bk_generic_dll_header *)header;
  struct bk_generic_dll_element *gde = (struct bk_generic_dll_element *)obj;

  gde->gde_prev = NULL;
  gde->gde_next = gdh->gdh_head;

  if (gdh->gdh_head)
    gdh->gdh_head->gde_prev = gde;
  else
    gdh->gdh_tail = gde;

  gdh->gdh_head = gde;

  return(DICT_OK); 
}



/**
 * Insert a dll element onto the list. In keeping with CLC insert
 * prepends. Again flags is supporessed to keep this in line with the CLC
 * APi.
 *
 *	@param header The header of dll.
 *	@param obj The object to insert.
 *	@return !<i>DICT_ERR</i> on failure.<br>
 *	@return <i>DICT_OK</i> on success.
 */
static __inline__ int
bk_dll_append(bk_dll_h header, void *obj)
{
  struct bk_generic_dll_header *gdh = (struct bk_generic_dll_header *)header;
  struct bk_generic_dll_element *gde = (struct bk_generic_dll_element *)obj;

  gde->gde_next = NULL;
  gde->gde_prev = gdh->gdh_tail;

  if (gdh->gdh_tail)
    gdh->gdh_tail->gde_next = gde;
  else
    gdh->gdh_head = gde;

  gdh->gdh_tail = gde;

  return(DICT_OK);
}




/**
 * Delete an element from the list.
 *
 *	@param header The header of dll.
 *	@param obj The object to insert.
 *	@return <i>DICT_ERR</i> on failure.<br>
 *	@return <i>DICT_OK</i> on success.
 */
static __inline__ int
bk_dll_delete(bk_dll_h header, void *obj)
{
  struct bk_generic_dll_header *gdh = (struct bk_generic_dll_header *)header;
  struct bk_generic_dll_element *gde = (struct bk_generic_dll_element *)obj;

  if (gde->gde_next)
    gde->gde_next->gde_prev = gde->gde_prev;
 
  if (gde->gde_prev)
    gde->gde_prev->gde_next = gde->gde_next;

  if (gdh->gdh_tail == gde)
    gdh->gdh_tail = gde->gde_prev;

  if (gdh->gdh_head == gde)
    gdh->gdh_head = gde->gde_next;

  return(DICT_OK);
}



/**
 * Find the minimum node in the dll.,
 *
 *	@param header The header of dll.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>node</i> on success.
 */
static __inline__ void *
bk_dll_minimum(bk_dll_h header)
{
  struct bk_generic_dll_header *gdh = (struct bk_generic_dll_header *)header;
 
  return(gdh->gdh_head);
}



/**
 * Find the maximum node in the dll.,
 *
 *	@param header The header of dll.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>node</i> on success.
 */
static __inline__ void *
bk_dll_maximum(bk_dll_h header)
{
  struct bk_generic_dll_header *gdh = (struct bk_generic_dll_header *)header;
 
  return(gdh->gdh_tail);
}





/**
 * Find the successor of obj in the dll.
 *
 *	@param header The header of dll (unneeded and declared for API matching)
 *	@param obj The current object.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>node</i> on success.
 */
static __inline__ void *
bk_dll_successor(bk_dll_h header, void *obj)
{
  struct bk_generic_dll_element *gde = (struct bk_generic_dll_element *)obj;
 
  return(gde->gde_next);
}




/**
 * Find the predecessor of obj in the dll.
 *
 *	@param header The header of dll (unneeded and declared for API matching)
 *	@param obj The current object.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>node</i> on success.
 */
static __inline__ void *
bk_dll_predecessor(bk_dll_h header, void *obj)
{
  struct bk_generic_dll_element *gde = (struct bk_generic_dll_element *)obj;
 
  return(gde->gde_prev);
}


/**
 * Allocate a baka dll iterator
 *
 *	@param header The header of dll (unneeded and declared for API matching)
 *	@param direction The direction for nextobj.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>node</i> on success.
 */
static __inline__ dict_iter 
bk_dll_iterate(bk_dll_h header, enum dict_direction direction)
{
  struct bk_generic_dll_header *gdh = (struct bk_generic_dll_header *)header;
  struct bk_dll_iterator *di = NULL;

  if (!(di = malloc(sizeof(*di))))
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
bk_dll_iterate_done(dict_iter iter)
{
  // All seatbelts off..
  free(iter);
  return;
}



/**
 * Get the next object in a list
 *
 *	@param header The bk_dll header
 *	@param iter The iterator in use.
 *	@return <i>NULL</i> when list is at the end.<br>
 *	@return <i>obj</i> on success.
 */
static __inline__ dict_obj 
bk_dll_nextobj(dict_h header, dict_iter iter)
{
  struct bk_dll_iterator *di = (struct bk_dll_iterator *)iter;
  dict_obj cur;

  // All seatbelts off;

  if (!(cur = di->di_current))
    return(cur);
  
  switch (di->di_direction)
  {
  case DICT_FROM_START:
    di->di_current = bk_dll_successor(header, di->di_current);
    break;
  case DICT_FROM_END:
    di->di_current = bk_dll_predecessor(header, di->di_current);
    break;
  default:
    return((dict_obj)NULL);
  }
  
  return(cur);
}


#endif /* _libbk_inline_h_ */
