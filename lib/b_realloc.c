#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_realloc.c,v 1.1 2004/05/22 00:11:36 jtt Exp $";
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
 *
 * Also other bitish functions
 */

#if defined(__INSURE__) && !defined(BK_NO_MALLOC_WRAP)

#include <libbk.h>
#include "libbk_internal.h"


#ifdef malloc 
#undef malloc
#endif 

#ifdef realloc
#undef realloc
#endif

#ifdef free
#undef free
#endif


static dict_h buffer_pool = NULL;

/**
 * @name Defines: buffer_pool_clc
 * Stores allocated buffer information (vptrs)
 */
// @{
#define buffer_pool_create(o,k,f,a)		ht_create(o,k,f,a)
#define buffer_pool_destroy(h)			ht_destroy(h)
#define buffer_pool_insert(h,o)			ht_insert(h,o)
#define buffer_pool_insert_uniq(h,n,o)		ht_insert_uniq(h,n,o)
#define buffer_pool_append(h,o)			ht_append(h,o)
#define buffer_pool_append_uniq(h,n,o)		ht_append_uniq(h,n,o)
#define buffer_pool_search(h,k)			ht_search(h,k)
#define buffer_pool_delete(h,o)			ht_delete(h,o)
#define buffer_pool_minimum(h)			ht_minimum(h)
#define buffer_pool_maximum(h)			ht_maximum(h)
#define buffer_pool_successor(h,o)		ht_successor(h,o)
#define buffer_pool_predecessor(h,o)		ht_predecessor(h,o)
#define buffer_pool_iterate(h,d)		ht_iterate(h,d)
#define buffer_pool_nextobj(h,i)		ht_nextobj(h,i)
#define buffer_pool_iterate_done(h,i)		ht_iterate_done(h,i)
#define buffer_pool_error_reason(h,i)		ht_error_reason(h,i)

static int buffer_pool_oo_cmp(struct bk_vptr *a, struct bk_vptr *b);
static int buffer_pool_ko_cmp(char *a, struct bk_vptr *b);
static unsigned int buffer_pool_obj_hash(struct bk_vptr *b);
static unsigned int buffer_pool_key_hash(char *a);
static struct ht_args buffer_pool_args = { 1021, 2, (ht_func)buffer_pool_obj_hash, (ht_func)buffer_pool_key_hash };
// @}


/**
 * Initialize the Baka malloc wrapper.
 *
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_malloc_wrap_init(bk_flags flags)
{
  if (!(buffer_pool = buffer_pool_create((dict_function)buffer_pool_oo_cmp, (dict_function)buffer_pool_ko_cmp, 0, &buffer_pool_args)))
  {
    goto error;
  }

  return(0);

 error:
  bk_malloc_wrap_destroy(0);

  return(-1);
}



/**
 * Destroy the the malloc wrapper state
 *
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
void
bk_malloc_wrap_destroy(bk_flags flags)
{
  struct bk_vptr *vector;

  if (!buffer_pool)
    return;

  for(vector = buffer_pool_minimum(buffer_pool); 
      vector;
      vector = buffer_pool_successor(buffer_pool, vector))
  {
    // There really shouldn't be any of these
    if (buffer_pool_delete(buffer_pool, vector) != DICT_OK)
    {
      break;
    }
    /*
     * Do NOT free vector->ptr. If the vector still exists then presumably
     * someone is using the memory. If this is *not* the case, then this
     * memory has already been leaked and we certainly don't want to hide
     * the leak to Insure by freeing it here, since, after all, in
     * production this will not happen.
     */
    free(vector);
  }

  buffer_pool_destroy(buffer_pool);

  return;
}



/**
 * Wrapper around malloc(3). Allocate vptr and buffer. Insert vptr in list. 
 *
 */
void *
bk_malloc_wrapper(size_t size)
{
  struct bk_vptr *vector = NULL;

  if (!buffer_pool && bk_malloc_wrap_init(0) < 0)
    goto error;

  if (!(vector = malloc(sizeof(*vector))))
    goto error;

  if (!(vector->ptr = malloc(size)))
    goto error;

  if (buffer_pool_insert_uniq(buffer_pool, vector, NULL) != DICT_OK)
    goto error;

  vector->len = size;
  
  return(vector->ptr);

 error:
  if (vector)
  {
    if (vector->ptr)
      free(vector->ptr);
    free(vector);
  }

  return(NULL);
}



/**
 * Wrapper around calloc(3)
 *
 *	@param nmemb Number of units
 *	@param size The size of each unit
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>ptr</i> on success.
 */
void *
bk_calloc_wrapper(size_t nmemb, size_t size)
{
  void *buf;

  if (!buffer_pool && bk_malloc_wrap_init(0) < 0)
    goto error;

  if (!(buf = bk_malloc_wrapper(nmemb * size)))
    goto error;

  memset(buf, 0, nmemb * size);

  return(buf);

 error:
  return(NULL);
}


/**
 * Wrapper around free(3).
 *
 *	@param buf The buffer to free(3).
 */
void
bk_free_wrapper(void *buf)
{
  struct bk_vptr *vector;

  if (!buffer_pool && bk_malloc_wrap_init(0) < 0)
    goto error;

  if (!buf)
    return;

  if (vector = buffer_pool_search(buffer_pool, buf))
  {
    buffer_pool_delete(buffer_pool, vector);
    free(vector);
  }

  free(buf);

 error:
  return;
}



/**
 * Wrapper around realloc()
 *
 *	@param buf The buffer to realloc
 *	@param size The new size
 *	@return <i>NULL</i> on failure.<br>
 *	@return new <i>buf</i> on success.
 */
void *
bk_realloc_wrapper(void *buf, size_t size)
{
  struct bk_vptr *vector;
  void *new_buf = NULL;

  if (!buffer_pool && bk_malloc_wrap_init(0) < 0)
    goto error;

  if (!buf)
    return(bk_malloc_wrapper(size));

  if (!(vector = buffer_pool_search(buffer_pool, buf)))
    goto error;

  if (!(new_buf = bk_malloc_wrapper(size)))
    goto error;

  memmove(new_buf, buf, MIN(size, vector->len));

  bk_free_wrapper(buf);

  return(new_buf);

 error:
  return(NULL);
}



/**
 * strdup(3) wrapper
 *
 *	@param str The string to copy
 *	@return <i>NULL</i> on failure.<br>
 *	@return dup'ed <i>str</i> on success.
 */
char *
bk_strdup_wrapper(const char *str)
{
  char *new_str = NULL;
  int len;

  if (!buffer_pool && bk_malloc_wrap_init(0) < 0)
    goto error;

  if (!str)
    goto error;

  len = strlen(str);

  if (!(new_str = bk_malloc_wrapper(len + 1)))
    goto error;
    
  memmove(new_str, str, len);
  new_str[len]='\0';

  return(new_str);

 error:
  if (new_str)
    free(new_str);

  return(NULL);
}


/**
 * strndup(3) wrapper
 *
 *	@param str The string to copy
 *	@return <i>NULL</i> on failure.<br>
 *	@return dup'ed <i>str</i> on success.
 */
char *
bk_strndup_wrapper(const char *str, size_t n)
{
  char *new_str = NULL;
  int len;

  if (!buffer_pool && bk_malloc_wrap_init(0) < 0)
    goto error;

  if (!str)
    goto error;

  len = MIN(strlen(str), n);

  if (!(new_str = bk_malloc_wrapper(len + 1)))
    goto error;
    
  memmove(new_str, str, len);
  new_str[len]='\0';

  
  return(new_str);

 error:
  if (new_str)
    free(new_str);

  return(NULL);
}


/** CLC helper functions and structures for buffer_pool_clc */
static int buffer_pool_oo_cmp(struct bk_vptr *a, struct bk_vptr *b)
{
  return((int)a->ptr - (int)b->ptr);
}

/** CLC helper functions and structures for buffer_pool_clc */
static int buffer_pool_ko_cmp(char *a, struct bk_vptr *b)
{
  return((int)a - (int)b->ptr);
}

/** CLC helper functions and structures for buffer_pool_clc */
static unsigned int buffer_pool_obj_hash(struct bk_vptr *a)
{
  return((unsigned int)a->ptr);
}

/** CLC helper functions and structures for buffer_pool_clc */
static unsigned int buffer_pool_key_hash(char *a)
{
  return((unsigned int)a);
}

#endif /* defined(__INSURE__) && !defined(BK_NO_REALLOC_REPLACE) */
