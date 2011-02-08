#if !defined(lint)
static const char tcs__copyright[] = "Copyright © 2002-2010 TCS Commercial, Inc.";
static const char tcs__contact[] = "TCS Commercial, Inc. <cssupport@TrustedCS.com>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2002-2010 The Authors. All rights reserved.
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
 * Dynamic statistics and performance routines
 */
#include <libbk.h>
#include "libbk_internal.h"

struct bk_dynamic_stat
{
  char *				bds_name;		//< Statistic name
  bk_dynamic_stats_value_type_e		bds_value_type; 	//< Type of the stat value
  bk_dynamic_stats_access_type_e	bds_access_type;	//< How to access the stat value
  u_int					bds_priority;		//< Stat priority
  bk_dynamic_stat_value_u		bds_value;		//< Value of the stat
  void *				bds_opaque;		//< User defined private data
  bk_dynamic_stat_destroy_h		bds_destroy_callback;	//< Callback to destroy opaque data.
  bk_flags				bds_flags;		//< Everyone needs flags
};

#define bds_int32	bds_value.bdsv_int32
#define bds_uint32	bds_value.bdsv_uint32
#define bds_int64	bds_value.bdsv_int64
#define bds_uint64	bds_value.bdsv_uint64
#define bds_float	bds_value.bdsv_float
#define bds_double	bds_value.bdsv_double
#define bds_string	bds_value.bdsv_string
#define bds_ptr		bds_value.bdsv_ptr

struct bk_dynamic_stats_list
{
  dict_h			bdsl_list;		// Data struct containing all the stats
#ifdef BK_USING_PTHREADS
  pthread_mutex_t		bdsl_lock;		// Lock out other threads
#endif /* BK_USING_PTHREADS */
  bk_flags			bdsl_flags;		// Everyone needs flags
#define BDSL_FLAG_MUTEXT_INITIALIZED	0x1		// Indicates whether the mutex has been initialized.
};


/**
 * @name Defines: List of checkpoint descriptors.
 *
 * NB: The API matches that of a hash tables for easy conversion.
 */
// @{
#define stats_list_create(o,k,f)	bst_create((o),(k),(f))
#define stats_list_destroy(h)		bst_destroy(h)
#define stats_list_insert(h,o)		bst_insert((h),(o))
#define stats_list_insert_uniq(h,n,o)	bst_insert_uniq((h),(n),(o))
#define stats_list_append(h,o)		bst_append((h),(o))
#define stats_list_append_uniq(h,n,o)	bst_append_uniq((h),(n),(o))
#define stats_list_search(h,k)		bst_search((h),(k))
#define stats_list_delete(h,o)		bst_delete((h),(o))
#define stats_list_minimum(h)		bst_minimum(h)
#define stats_list_maximum(h)		bst_maximum(h)
#define stats_list_successor(h,o)	bst_successor((h),(o))
#define stats_list_predecessor(h,o)	bst_predecessor((h),(o))
#define stats_list_iterate(h,d)		bst_iterate((h),(d))
#define stats_list_nextobj(h,i)		bst_nextobj(h,i)
#define stats_list_iterate_done(h,i)	bst_iterate_done(h,i)
#define stats_list_error_reason(h,i)	bst_error_reason((h),(i))
static int stats_list_oo_cmp(struct bk_dynamic_stat *a, struct bk_dynamic_stat *b);
static int stats_list_ko_cmp(char *a, struct bk_dynamic_stat *b);
#ifdef STATS_LIST_IS_HASH
// Reenable these if the stats list becomes an hash table
static ht_val stats_list_obj_hash(struct bk_dynamic_stat *a);
static ht_val stats_list_key_hash(char *a);
static struct ht_args stats_list_args = { 500, 1, (ht_func)stats_list_obj_hash, (ht_func)stats_list_key_hash };
#endif /* STATS_LIST_KEY_HASH */

static struct bk_dynamic_stat *bds_create(bk_s B, bk_flags flags);
static void bds_destroy(bk_s B, struct bk_dynamic_stat *bds);
static struct bk_dynamic_stats_list*bdsl_create(bk_s B, bk_flags flags);
static void bdsl_destroy(bk_s B, struct bk_dynamic_stats_list *bdsl);
static int bdsl_getnext(bk_s B, struct bk_dynamic_stats_list *bdsl, struct bk_dynamic_stat **bdsp, u_int priority, bk_dynamic_stats_value_type_e *typep, bk_dynamic_stat_value_u *valuep, bk_flags flags);
static int extract_value(bk_s B, struct bk_dynamic_stat *bds, void *buf, u_int len, bk_flags flags);
static void starttime_destroy(bk_s B, void *data);


/**
 * Create a bds struct
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new <i>bds</i> struct on success.
 */
static struct bk_dynamic_stat *
bds_create(bk_s B, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stat *bds = NULL;

  if (!(BK_CALLOC(bds)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not calloc: %s\n", strerror(errno));
    goto error;
  }

  BK_RETURN(B, bds);

 error:
  if (bds)
    bds_destroy(B, bds);
  BK_RETURN(B, NULL);
}


/**
 * Destroy a bds struct.
 *
 *	@param B BAKA thread/global state.
 *	@param bds The bds struct to nuke.
 */

static void
bds_destroy(bk_s B, struct bk_dynamic_stat *bds)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bds)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (bds->bds_destroy_callback)
    (*bds->bds_destroy_callback)(B, bds->bds_opaque);

  if (bds->bds_name)
    free(bds->bds_name);

  if ((bds->bds_access_type == DynamicStatsAccessTypeDirect) &&
      (bds->bds_value_type == DynamicStatsValueTypeString) &&
      bds->bds_string)
  {
    free(bds->bds_string);
  }

  free(bds);
  BK_VRETURN(B);
}



/**
 * Create a bdsl struct
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new <i>bdsl</i> on success.
 */
static struct bk_dynamic_stats_list*
bdsl_create(bk_s B, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = NULL;

  if (!(BK_CALLOC(bdsl)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not calloc: %s\n", strerror(errno));
    goto error;
  }

  if (!(bdsl->bdsl_list = stats_list_create((dict_function)stats_list_oo_cmp, (dict_function)stats_list_ko_cmp, DICT_UNIQUE_KEYS | DICT_BALANCED_TREE /*, &stats_list_args */)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create dynamic statistics list: %s\n", stats_list_error_reason(NULL, NULL));
    goto error;
  }

#ifdef BK_USING_PTHREADS
  if (pthread_mutex_init(&bdsl->bdsl_lock, NULL) != 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create dynamic statistics list mutex: %s\n", strerror(errno));
    goto error;
  }

  BK_FLAG_SET(bdsl->bdsl_flags, BDSL_FLAG_MUTEXT_INITIALIZED);
#endif /* BK_USING_PTHREADS */

  BK_RETURN(B, bdsl);

 error:
  if (bdsl)
    bdsl_destroy(B, bdsl);
  BK_RETURN(B, NULL);
}


/**
 * Destroy a bdsl struct
 *
 *	@param B BAKA thread/global state.
 *	@param bdsl The @a bdsl to destroy.
 */
static void
bdsl_destroy(bk_s B, struct bk_dynamic_stats_list *bdsl)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stat *bds;

  if (!bdsl)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (BK_FLAG_ISSET(bdsl->bdsl_flags, BDSL_FLAG_MUTEXT_INITIALIZED))
    BK_SIMPLE_LOCK(B, &bdsl->bdsl_lock);

  if (bdsl->bdsl_list)
  {
    while(bds = stats_list_minimum(bdsl->bdsl_list))
    {
      if (stats_list_delete(bdsl->bdsl_list, bds) != DICT_OK)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not delete a bds from the list: %s\n", stats_list_error_reason(bdsl->bdsl_list, NULL));
      }
      bds_destroy(B, bds);
    }
    stats_list_destroy(bdsl->bdsl_list);
  }

  /*
   * Nothing (of consequence) should occur between this step and free(bdsl);
   */
  if (BK_FLAG_ISSET(bdsl->bdsl_flags, BDSL_FLAG_MUTEXT_INITIALIZED))
  {
    BK_SIMPLE_UNLOCK(B, &bdsl->bdsl_lock);
#ifdef BK_USING_PTHREADS
    if (pthread_mutex_destroy(&bdsl->bdsl_lock) != 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not destroy dynamic statistics list lock: %s\n", strerror(errno));
    }
#endif /* BK_USING_PTHREADS */
  }

  free(bdsl);
  BK_VRETURN(B);
}



/**
 * Create a new list of dynamic stats. You may ask what's the point of a
 * public contstructor that nothing but call the private constructor. You
 * may also shut the hell up.
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new <i>bk_dynamic_stats_h</i> on success.
 */
bk_dynamic_stats_h
bk_dynamic_stats_create(bk_s B, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = NULL;

  if (!(bdsl = bdsl_create(B, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create bdsl struct\n");
    goto error;
  }

  BK_RETURN(B, bdsl);

 error:
  if (bdsl)
    bdsl_destroy(B, bdsl);
  BK_RETURN(B, NULL);
}



/**
 * Destroy a stats list.
 *
 *	@param B BAKA thread/global state.
 *	@param bdsl The stats list to destroy.
 */
void
bk_dynamic_stats_destroy(bk_s B, bk_dynamic_stats_h stats_list)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;

  if (!bdsl)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  bdsl_destroy(B, bdsl);

  BK_VRETURN(B);
}



/**
 * Register a stat.
 *
 *	@param B BAKA thread/global state.
 *	@param stats_list The list stats to which to add this new stat
 *	@param name  The name of the stat (for lookup).
 *	@param value_type The storage type of the value.
 *	@param access_type How to access the value (ie directly or indirectly)
 *	@param opaque Optional user defined data
 *	@param destroy_callback Callback used to free @opaque (optional obviously)
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stat_register(bk_s B, bk_dynamic_stats_h stats_list, const char *name, u_int priority, bk_dynamic_stats_value_type_e value_type, bk_dynamic_stats_access_type_e access_type, void *opaque, bk_dynamic_stat_destroy_h destroy_callback, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stat *bds = NULL;
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  int locked = 0;

  if (!bdsl || !name)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if ((value_type == DynamicStatsValueTypeString) && (access_type == DynamicStatsAccessTypeIndirect))
  {
    bk_error_printf(B, BK_ERR_ERR, "Sting stats can only be access directly\n");
    goto error;
  }

  if (!(bds = bds_create(B, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create bds structure\n");
    goto error;
  }

  if (!(bds->bds_name = strdup(name)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not copy stat name to storage: %s\n", strerror(errno));
    goto error;
  }

  bds->bds_value_type = value_type;
  bds->bds_access_type = access_type;
  bds->bds_priority = priority;
  bds->bds_opaque = opaque;
  bds->bds_destroy_callback = destroy_callback;

  if (BK_FLAG_ISSET(bdsl->bdsl_flags, BDSL_FLAG_MUTEXT_INITIALIZED))
  {
    BK_SIMPLE_LOCK(B, &bdsl->bdsl_lock);
    locked = 1;
  }

  if (stats_list_insert(bdsl->bdsl_list, bds) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not insert bds in bdsl: %s\n", stats_list_error_reason(bdsl->bdsl_list, NULL));
    goto error;
  }
  bds = NULL;


  if (locked)
    BK_SIMPLE_UNLOCK(B, &bdsl->bdsl_lock);

  BK_RETURN(B, 0);

 error:
  if (bds)
    bds_destroy(B, bds);

  if (locked)
    BK_SIMPLE_UNLOCK(B, &bdsl->bdsl_lock);

  BK_RETURN(B, -1);
}



/**
 * Deregister a stat
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stat_deregister(bk_s B, bk_dynamic_stats_h stats_list, const char *name, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  struct bk_dynamic_stat *bds = NULL;
  int locked = 0;

  if (!stats_list || !name)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (BK_FLAG_ISSET(bdsl->bdsl_flags, BDSL_FLAG_MUTEXT_INITIALIZED))
  {
    BK_SIMPLE_LOCK(B, &bdsl->bdsl_lock);
    locked = 1;
  }

  if (!(bds = stats_list_search(bdsl->bdsl_list, (char *)name)))
  {
    bk_error_printf(B, BK_ERR_WARN, "Could not find a stats struct with name: %s\n", name);
    goto error;
  }

  if (stats_list_delete(bdsl->bdsl_list, bds) != DICT_OK)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not delete stat struct from list: %s\n", stats_list_error_reason(bdsl->bdsl_list, NULL));

    /*
     * Hmm...no matter what we do here, it's not good. Either we leave the
     * bds in the bdsl, counter to what the user wants, or we nuke the bds
     * leaving the bdsl with a dangling pointer. Well I guess the
     * get_call() feature strongly suggests that the best course of action
     * is to leave the bds allocated.
     */
    goto error;
  }

  if (locked)
    BK_SIMPLE_UNLOCK(B, &bdsl->bdsl_lock);

  BK_RETURN(B, 0);

 error:
  if (locked)
    BK_SIMPLE_UNLOCK(B, &bdsl->bdsl_lock);

  BK_RETURN(B, -1);
}



#define EXTRACT_BDS_VALUE(bds, buf, len, type, value)					\
do											\
{											\
  if ((len) < sizeof(type))								\
  {											\
    bk_error_printf(B, BK_ERR_ERR, "Insufficent space to copy variable of type\n");	\
    goto error;										\
  }											\
											\
  if ((bds)->bds_access_type == DynamicStatsAccessTypeDirect)				\
  {											\
    *((type *)(buf)) = (value);								\
  }											\
  else											\
  {											\
    *((type *)(buf)) = *((type *)((bds)->bds_ptr));					\
  }											\
}while(0)



/**
 * Extract a value from an bds.
 *
 *	@param B BAKA thread/global state.
 *	@param bds The stat struct containing the value.
 *	@param buf The C/O results buf (optional since internal callers may only care about extracting the bds).
 *	@param len The length of the results buf.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
extract_value(bk_s B, struct bk_dynamic_stat *bds, void *buf, u_int len, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bds)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  switch(bds->bds_value_type)
  {
  case DynamicStatsValueTypeInt32:
    EXTRACT_BDS_VALUE(bds, buf, len, int32_t, bds->bds_int32);
    break;
  case DynamicStatsValueTypeUInt32:
    EXTRACT_BDS_VALUE(bds, buf, len, u_int32_t, bds->bds_uint32);
    break;
  case DynamicStatsValueTypeInt64:
    EXTRACT_BDS_VALUE(bds, buf, len, int64_t, bds->bds_int64);
    break;
  case DynamicStatsValueTypeUInt64:
    EXTRACT_BDS_VALUE(bds, buf, len, u_int64_t, bds->bds_uint64);
    break;
  case DynamicStatsValueTypeFloat:
    EXTRACT_BDS_VALUE(bds, buf, len, float, bds->bds_float);
    break;
  case DynamicStatsValueTypeDouble:
    EXTRACT_BDS_VALUE(bds, buf, len, double, bds->bds_double);
    break;
  case DynamicStatsValueTypeString:
    if (((!*(char **)buf) && (len > 0)) ||
	((*(char **)buf) && (len == 0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Argument mismatch while extracting string value type. \n");
      goto error;
    }
    if (len == 0)
    {
      if (!(*(char **)buf = strdup(bds->bds_string)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not strdup string value: %s\n", strerror(errno));
      }
    }
    else
    {
      strncpy(*(char **)buf, bds->bds_string, len);
    }
    break;
  default:
    bk_error_printf(B, BK_ERR_ERR,"Unknown stats value type: %d\n", bds->bds_value_type);
    break;
  }

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}



/**
 * Get a single a value, and/or any of the other attributes of the
 * statistic described by @a name.
 *
 *	@param B BAKA thread/global state.
 *	@param name The description of the stat to get
 *	@param valuep Optional (C/O) value of the statistic
 *	@param typep Optional (C/O) type of the statistic
 *	@param piorityp Optional (C/O) priority of the statistic
 *	@param opaque Optional (C/O) opaque data of the statistic
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stat_get(bk_s B, bk_dynamic_stats_h stats_list, const char *name, bk_dynamic_stat_value_u *valuep, bk_dynamic_stats_value_type_e *typep, int *priorityp, void **opaquep, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  struct bk_dynamic_stat *bds = NULL;
  int locked = 0;

  if (!name || !bdsl || !valuep)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (BK_FLAG_ISSET(bdsl->bdsl_flags, BDSL_FLAG_MUTEXT_INITIALIZED))
  {
    BK_SIMPLE_LOCK(B, &bdsl->bdsl_lock);
    locked = 1;
  }

  if (!(bds = stats_list_search(bdsl->bdsl_list, (char *)name)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not locate a statisitic described as: %s\n", name);
  }

  if (priorityp)
    *priorityp = bds->bds_priority;

  if (typep)
    *typep = bds->bds_value_type;

  if (valuep)
  {
    if (extract_value(B, bds, valuep, sizeof(*valuep), 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not extract value from statistic described as: %s\n", name);
      goto error;
    }
  }

  if (locked)
    BK_SIMPLE_UNLOCK(B, &bdsl->bdsl_lock);
  BK_RETURN(B, 0);

 error:
  if (locked)
    BK_SIMPLE_UNLOCK(B, &bdsl->bdsl_lock);
  BK_RETURN(B, -1);
}


/**
 * Obtain the first value in a list.
 *
 *	@param B BAKA thread/global state.
 *	@param stats_list The stats list.
 *	@param name The name of the statistic to upate.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stat_set(bk_s B, bk_dynamic_stats_h stats_list, const char *name, bk_flags flags, ...)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  struct bk_dynamic_stat *bds = NULL;
  va_list ap;
  int locked = 0;

  if (!bdsl || !name)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (BK_FLAG_ISSET(bdsl->bdsl_flags, BDSL_FLAG_MUTEXT_INITIALIZED))
  {
    BK_SIMPLE_LOCK(B, &bdsl->bdsl_lock);
    locked = 1;
  }

  if (!(bds = stats_list_search(bdsl->bdsl_list, (char *)name)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not locate a statistics: %s\n", name);
    goto error;
  }

  if (bds->bds_access_type == DynamicStatsAccessTypeIndirect)
  {
    bk_error_printf(B, BK_ERR_ERR, "Indirect stats should not use %s\n", __FUNCTION__);
    goto error;
  }

  va_start(ap, flags);

  switch(bds->bds_value_type)
  {
  case DynamicStatsValueTypeInt32:
    bds->bds_int32 = va_arg(ap, int32_t);
    break;
  case DynamicStatsValueTypeUInt32:
    bds->bds_uint32 = va_arg(ap, u_int32_t);
    break;
  case DynamicStatsValueTypeInt64:
    bds->bds_int64 = va_arg(ap, int64_t);
    break;
  case DynamicStatsValueTypeUInt64:
    bds->bds_uint64 = va_arg(ap, u_int64_t);
    break;
  case DynamicStatsValueTypeFloat:
    // Floats are (apparently) promoted to doubles when passed through stdargs
    bds->bds_float = va_arg(ap, double);
    break;
  case DynamicStatsValueTypeDouble:
    bds->bds_double = va_arg(ap, double);
    break;
  case DynamicStatsValueTypeString:
    {
      char *s = va_arg(ap, char *);
      if (!(bds->bds_string = strdup(s)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not copy string: %s\n", strerror(errno));
	goto error;
      }
    }
    break;
  default:
    bk_error_printf(B, BK_ERR_ERR, "Unknown statistics value type: %d\n", bds->bds_value_type);
    goto error;
    break;
  }

  va_end(ap);

  if (locked)
    BK_SIMPLE_UNLOCK(B, &bdsl->bdsl_lock);
  BK_RETURN(B, 0);

 error:
  if (locked)
    BK_SIMPLE_UNLOCK(B, &bdsl->bdsl_lock);
  BK_RETURN(B, -1);
}



/**
 * Update any of the non-value attributes of a stat. NB If the @a
 * value_type gets changed, it would probably be a good idea to follow it
 * immediately with a bk_dynamic_stat_set(). This functions differs from
 * said set() function in that all the types are known and thus no stdargs
 * are needed.
 *
 *	@param B BAKA thread/global state.
 *	@param stats_list The statistics list.
 *	@param name The name/description of the list
 *	@param value_type The type of the stat value
 *	@param access-type The access type of the stat
 *	@param priority The priority of the stat
 *	@param opaque The user opaque data.
 *	@param destroy_callback The callback that destroys @a opaque
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stat_attr_update(bk_s B, bk_dynamic_stats_h stats_list, const char *name, bk_dynamic_stats_value_type_e value_type, bk_dynamic_stats_access_type_e access_type, int priority, void *opaque, bk_dynamic_stat_destroy_h destroy_callback, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  struct bk_dynamic_stat *bds;

  if (!bdsl || !name)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!(bds = stats_list_search(bdsl->bdsl_list, (char *)name)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not find a statistic with the description: %s\n", name);
    goto error;
  }

  if (BK_FLAG_ISSET(bds->bds_flags, BK_DYNAMIC_STAT_UPDATE_FLAG_VALUE_TYPE))
    bds->bds_value_type = value_type;

  if (BK_FLAG_ISSET(bds->bds_flags, BK_DYNAMIC_STAT_UPDATE_FLAG_ACCESS_TYPE))
    bds->bds_access_type = access_type;

  if (BK_FLAG_ISSET(bds->bds_flags, BK_DYNAMIC_STAT_UPDATE_FLAG_PRIORITY))
    bds->bds_priority = priority;

  if (BK_FLAG_ISSET(bds->bds_flags, BK_DYNAMIC_STAT_UPDATE_FLAG_DESTROY_OPAQUE) &&
      bds->bds_destroy_callback)
  {
    (*bds->bds_destroy_callback)(B, bds->bds_opaque);
  }

  if (BK_FLAG_ISSET(bds->bds_flags, BK_DYNAMIC_STAT_UPDATE_FLAG_OPAQUE))
    bds->bds_opaque = opaque;

  if (BK_FLAG_ISSET(bds->bds_flags, BK_DYNAMIC_STAT_UPDATE_FLAG_DESTROY_CALLBACK))
    bds->bds_destroy_callback = destroy_callback;

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}



/**
 * Increment a stat by a integral value. The value of incr must be the
 * argument which immediately follows @name. It must be the same type as
 * the statitic value.
 *
 *	@param B BAKA thread/global state.
 *	@param stats_list The stats list.
 *	@param name The name of the statistic to upate.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stat_increment(bk_s B, bk_dynamic_stats_h *stats_list, const char *name, bk_flags flags, ...)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  struct bk_dynamic_stat *bds = NULL;
  va_list ap;
  int locked = 0;

  if (!bdsl || !name)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (BK_FLAG_ISSET(bdsl->bdsl_flags, BDSL_FLAG_MUTEXT_INITIALIZED))
  {
    BK_SIMPLE_LOCK(B, &bdsl->bdsl_lock);
    locked = 1;
  }

  if (!(bds = stats_list_search(bdsl->bdsl_list, (char *)name)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not locate a statistics: %s\n", name);
    goto error;
  }

  if (bds->bds_access_type == DynamicStatsAccessTypeIndirect)
  {
    bk_error_printf(B, BK_ERR_ERR, "Indirect stats should not use %s\n", __FUNCTION__);
    goto error;
  }

  va_start(ap, flags);

  switch(bds->bds_value_type)
  {
  case DynamicStatsValueTypeInt32:
    bds->bds_int32 += va_arg(ap, int32_t);
    break;
  case DynamicStatsValueTypeUInt32:
    bds->bds_uint32 += va_arg(ap, u_int32_t);
    break;
  case DynamicStatsValueTypeInt64:
    bds->bds_int64 += va_arg(ap, int64_t);
    break;
  case DynamicStatsValueTypeUInt64:
    bds->bds_uint64 += va_arg(ap, u_int64_t);
    break;
  case DynamicStatsValueTypeFloat:
	// Float is promoted to double when passed through stdargs
    bds->bds_float += va_arg(ap, double);
    break;
  case DynamicStatsValueTypeDouble:
    bds->bds_double += va_arg(ap, double);
    break;
  case DynamicStatsValueTypeString:
    bk_error_printf(B, BK_ERR_ERR, "Incrementing a string value is not permitted\n");
    goto error;
    break;
  default:
    bk_error_printf(B, BK_ERR_ERR,"Unknown statistics value type: %d\n", bds->bds_value_type);
    goto error;
    break;
  }

  va_end(ap);

  if (locked)
    BK_SIMPLE_UNLOCK(B, &bdsl->bdsl_lock);
  BK_RETURN(B, 0);

 error:
  if (locked)
    BK_SIMPLE_UNLOCK(B, &bdsl->bdsl_lock);
  BK_RETURN(B, -1);
}



/**
 * Get the next value from the statistics list. If *bdsp == NULL, then get the firist value.
 *
 *	@param B BAKA thread/global state.
 *	@param bds The stat struct containing the value.
 *	@param bdsp The C/O (only) bds for use with the successor function()
 *	@param typep The C/O value indicating the value's type.
 *	@param valuep The C/O result.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> if there is value.
 *	@return <i>1</i> on success.
 */
static int
bdsl_getnext(bk_s B, struct bk_dynamic_stats_list *bdsl, struct bk_dynamic_stat **bdsp, u_int priority, bk_dynamic_stats_value_type_e *typep, bk_dynamic_stat_value_u *valuep, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stat *bds;

  if (!bdsl || !bdsp)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!*bdsp)
  {
    bds = stats_list_minimum(bdsl->bdsl_list);
  }
  else
  {
    bds = stats_list_successor(bdsl->bdsl_list, *bdsp);
  }

  while(bds)
  {
    struct bk_dynamic_stat *tbds = stats_list_successor(bdsl->bdsl_list, bds);
    if (priority >= bds->bds_priority)
      break;
    bds = tbds;
  }

  if (!bds)
    BK_RETURN(B, 0);

  *bdsp = bds;


  /*
   *  Internal callers of this function are permitted to extract just the
   *  bds structure since they can read it directly. The type and value
   *  copy outs are only required for external clients for whome the bds is
   *  an opaque value.
   */
  if (typep)
    *typep = bds->bds_value_type;

  if (bdsp && valuep)
    BK_RETURN(B, extract_value(B, bds, valuep, sizeof(*valuep), 0));

  BK_RETURN(B, 1);
}



/**
 * Extract, from a list of statistics values, the next value at or above a
 * particular priority order. If *statp == NULL, this function returns the
 * first value in the list (subject to the priority of course).
 *
 *	@param B BAKA thread/global state.
 *	@param stats_list The stat struct containing the value.
 *	@param iterator The C/O iterator (opaque)
 *	@param buf The C/O results buf.
 *	@param len The length of the results buf.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stats_getnext(bk_s B, bk_dynamic_stats_h stats_list, bk_dynamic_stat_h *statp, u_int priority, bk_dynamic_stats_value_type_e *typep, bk_dynamic_stat_value_u *valuep, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  struct bk_dynamic_stat **bdsp = (struct bk_dynamic_stat **)statp;
  int ret;
  int locked = 0;

  if (!bdsl)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (BK_FLAG_ISSET(bdsl->bdsl_flags, BDSL_FLAG_MUTEXT_INITIALIZED))
  {
    BK_SIMPLE_LOCK(B, &bdsl->bdsl_lock);
    locked = 1;
  }

  ret = bdsl_getnext(B, bdsl, bdsp, priority, typep, valuep, flags);

  if (locked)
    BK_SIMPLE_LOCK(B, &bdsl->bdsl_lock);

  BK_RETURN(B, ret);
}



#define STAT_XML_OUTPUT(fmt, value)								\
do												\
{												\
  vptr_ret = bk_vstr_cat(B, 0, &xml_vstr,							\
			 "%s\t<statistic description=\"%s\" priority=%u>"fmt"</statistic>\n",	\
			 prefix, bds->bds_name, bds->bds_priority, value);			\
}while(0);

/**
 * Obtain an (allocated) XML string which lists all the statistics at or
 * above a particular priority level. Caller must destroy.
 *
 *	@param B BAKA thread/global state.
 *	@param stats_list The stats list from which to extract the values
 *	@param priority The priority filter.
 *	@param prefix A fixed string to prepend to all output lines (presumably tab)
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new <i>XML string</i> on success.
 */
char *
bk_dynamic_stats_XML_create(bk_s B, bk_dynamic_stats_h stats_list, u_int priority, const char *prefix, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  struct bk_dynamic_stat *bds = NULL;
  bk_vstr xml_vstr;
  int ret;
  char *xml_str = NULL;				// Used for str2xml conversion
  time_t timenow = time(NULL);
  char time_buf[50];
  char stat_buf[1024];
  int locked = 0;
  void *bt;

  if (!bdsl || !prefix)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!(xml_vstr.ptr = malloc(1)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate initial NUL for XML output string: %s\n", strerror(errno));
    goto error;
  }

  *xml_vstr.ptr = '\0'; // Initialize empty string for the purpose of using bk_vstr.ptr
  xml_vstr.max = 1;
  xml_vstr.cur = 0;

  if (!strftime(time_buf, sizeof(time_buf), BK_STRFTIME_DEFAULT_FMT, gmtime(&timenow)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not convert current time to a string: %s\n", strerror(errno));
    goto error;
  }

  snprintf(stat_buf, sizeof(stat_buf), "%s<statistics generated=\"%s\">\n", prefix, (char *)time_buf);

  if (bk_vstr_cat(B, 0, &xml_vstr, "%s", stat_buf))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not begin XML statitics string\n");
    goto error;
  }

  /*
   * Read all the thread CPU clocks and print their usage
   * <TODO> These should really be pure statistics which are updated as per normal</TODO>
   */
  for(bt = bk_threadlist_mininum(B); bt; bt = bk_threadlist_successor(B, bt))
  {
    clockid_t cpu_clock;
    struct timespec ts;

    if (pthread_getcpuclockid(bk_threadnode_threadid(B, bt), &cpu_clock) != 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not get CPU clock id for a thread: %s\n", bk_threadnode_name(B, bt));
      goto error;
    }

    if (clock_gettime(cpu_clock, &ts) != 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not get CPU clock time for thread: %s: %s\n", bk_threadnode_name(B, bt), strerror(errno));
      goto error;
    }

    if (snprintf(stat_buf, sizeof(stat_buf), "%s\t<statistic description=\"%s thread CPU time\">%9.4f</statistic>\n", prefix, bk_threadnode_name(B, bt), BK_TS2F(&ts)) >= (int)sizeof(stat_buf))
    {
      bk_error_printf(B, BK_ERR_WARN, "Could not fit CPU time into a buffer of size: %lu\n", sizeof(stat_buf));
      continue;
    }

    if (bk_vstr_cat(B, 0, &xml_vstr, "%s", stat_buf))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not begin XML statitics string\n");
      goto error;
    }
  }


  if (BK_FLAG_ISSET(bdsl->bdsl_flags, BDSL_FLAG_MUTEXT_INITIALIZED))
  {
    BK_SIMPLE_LOCK(B, &bdsl->bdsl_lock);
    locked = 1;
  }

  while((ret = bdsl_getnext(B, bdsl, &bds, priority, NULL, NULL, 0)) == 1)
  {
    int vptr_ret;
    switch(bds->bds_value_type)
    {
    case DynamicStatsValueTypeInt32:
      STAT_XML_OUTPUT("%d", bds->bds_int32);
      break;
    case DynamicStatsValueTypeUInt32:
      STAT_XML_OUTPUT("%u", bds->bds_uint32);
      break;
    case DynamicStatsValueTypeInt64:
      STAT_XML_OUTPUT("%ld", bds->bds_int64);
      break;
    case DynamicStatsValueTypeUInt64:
      STAT_XML_OUTPUT("%lu", bds->bds_uint64);
      break;
    case DynamicStatsValueTypeFloat:
      STAT_XML_OUTPUT("%f", bds->bds_float);
      break;
    case DynamicStatsValueTypeDouble:
      STAT_XML_OUTPUT("%lf", bds->bds_double);
      break;
    case DynamicStatsValueTypeString:
      if (!(xml_str = bk_string_str2xml(B, bds->bds_string, 0)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not convert string to XML\n");
	goto error;
      }
      STAT_XML_OUTPUT("%s", xml_str);
      free(xml_str);
      xml_str = NULL;
      break;
    default:
      bk_error_printf(B, BK_ERR_ERR,"Unknown type: %d\n", bds->bds_value_type);
      goto error;
      break;
    }
  }

  if (ret < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not extract the fisrt or next stats value\n");
    goto error;
  }

  if (bk_vstr_cat(B, 0, &xml_vstr, "%s", "</statistics>\n"))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not complete XML statitics string\n");
    goto error;
  }

  if (locked)
    BK_SIMPLE_UNLOCK(B, &bdsl->bdsl_lock);
  BK_RETURN(B, xml_vstr.ptr);

 error:
  if (locked)
    BK_SIMPLE_UNLOCK(B, &bdsl->bdsl_lock);

  if (xml_vstr.ptr)
    free(xml_vstr.ptr);

  if (xml_str)
    free(xml_str);

  BK_RETURN(B, NULL);
}



/**
 * Destroy the string created by bk_dynamic_stats_XML_create.
 *
 *	@param B BAKA thread/global state.
 *	@param xml_str The XML string to destroy.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
void
bk_dynamic_stats_XML_destroy(bk_s B, const char *xml_str, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!xml_str)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  free((char *)xml_str);

  BK_VRETURN(B);
}






/**
 * Initialize the basic, needed-for-all-jobs statistics
 *
 *	@param B BAKA thread/global state.
 *	@param stats_list The global statistics list
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_global_dynamic_stats_register(bk_s B, bk_dynamic_stats_h stats_list, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!stats_list)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (bk_dynamic_stat_pid_register(B, stats_list, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not register and set process ID statistic\n");
    goto error;
  }

  if (bk_dynamic_stat_starttime_register(B, stats_list, 0))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not register and set job start time statistic\n");
    goto error;
  }

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}




/**
 * Register PID "statistic". Not really a statistic, but what're you gonna do.
 *
 *	@param B BAKA thread/global state.
 *	@param stats_list The statistics list managing this statistic
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stat_pid_register(bk_s B, bk_dynamic_stats_h stats_list, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *name;
  char *tmp;
  u_int priority;

  if (!stats_list)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  name = BK_GWD(B, "statistic.worker_pid.name", BK_DYNAMIC_STAT_DEFAULT_NAME_PROCESS_ID);
  tmp = BK_GWD(B, "statistic.worker_pid.priority", "0");

  if (bk_string_atou32(B, tmp, (u_int32_t *)&priority, 0) != 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Failed to convert priority string into a u_int\n");
    goto error;
  }

  if (bk_dynamic_stat_register(B, stats_list, name, priority, DynamicStatsValueTypeUInt32, DynamicStatsAccessTypeDirect, NULL, NULL, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not reigster PID statistic (official description: %s)\n", name);
    goto error;
  }

  // This is a fixed value, so we can just set it here.
  if (bk_dynamic_stat_set(B, stats_list, name, 0, getpid()) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not update PID stat\n");
    goto error;
  }

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}




/**
 * Register and set the start time.
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stat_starttime_register(bk_s B, bk_dynamic_stats_h stats_list, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct timeval *tv = NULL;
  char *name;
  char *tmp;
  u_int priority;
  char time_buf[100];

  if (!stats_list)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  name = BK_GWD(B, "statistic.job_start_time.name", BK_DYNAMIC_STAT_DEFAULT_NAME_PROCESS_START);
  tmp = BK_GWD(B, "statistic.job_start_time.priority", "0");

  if (bk_string_atou32(B, tmp, (u_int32_t *)&priority, 0) != 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Failed to convert priority string into a u_int\n");
    goto error;
  }

  if (!(BK_MALLOC(tv)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not malloc start time timeval: %s\n", strerror(errno));
    goto error;
  }

  if (gettimeofday(tv, NULL) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not obtain the current time of day: %s\n", strerror(errno));
    goto error;
  }

  /* Since this is a fixed value we immediately update it */
  if (strftime(time_buf, sizeof(time_buf), BK_STRFTIME_DEFAULT_FMT, gmtime(&tv->tv_sec)) == 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not convert current time to string\n");
    goto error;
  }

  if (bk_dynamic_stat_register(B, stats_list, name, priority, DynamicStatsValueTypeString, DynamicStatsAccessTypeDirect, (void *)tv, starttime_destroy, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not register statistic described: %s\n", name);
    goto error;
  }

  if (bk_dynamic_stat_set(B, stats_list, name, 0, time_buf))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set the start time in the stat\n");
    goto error;
  }

  BK_RETURN(B, 0);

 error:
  if (tv)
    starttime_destroy(B, tv);

  BK_RETURN(B, -1);
}



/**
 * Destroy the starttime
 *
 *	@param B BAKA thread/global state.
 *	@param tv The starttime timeval to destroy
 */
static void
starttime_destroy(bk_s B, void *data)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct timeval *tv = (struct timeval *)data;

  if (!tv)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  free(tv);

  BK_VRETURN(B);
}




static int stats_list_oo_cmp(struct bk_dynamic_stat *a, struct bk_dynamic_stat *b)
{
  return(strcmp(a->bds_name,b->bds_name));
}

static int stats_list_ko_cmp(char *a, struct bk_dynamic_stat *b)
{
  return(strcmp(a,b->bds_name));
}

#ifdef STATS_LIST_IS_HASH
static ht_val stats_list_obj_hash(struct bk_dynamic_stat *a)
{
  return(bk_strhash(a->bds_name, 0));
}

static ht_val stats_list_key_hash(char *a)
{
  return(bk_strhash(a,0));
}
#endif /* STATS_LIST_KEY_HASH */
