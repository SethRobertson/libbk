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

//#define NO_THREAD_CPU_STAT

struct bk_dynamic_stat_key
{
  const char *				bdsk_name;		///< Statistic name
  long					bdsk_discriminator;	///< Distinguish same-named stats.
};

struct bk_dynamic_stat
{
  struct bk_dynamic_stat_key		bds_key;  		///< Search key
  bk_dynamic_stats_value_type_e		bds_value_type; 	///< Type of the stat value
  bk_dynamic_stats_access_type_e	bds_access_type;	///< How to access the stat value
  u_int					bds_priority;		///< Stat priority
  bk_dynamic_stat_value_u		bds_value;		///< Value of the stat
  void *				bds_opaque;		///< User defined private data
  bk_dynamic_stat_destroy_h		bds_destroy_callback;	///< Callback to destroy opaque data.
  bk_dynamic_stat_update_h		bds_update_callback;	///< Callback for on-demand updates
#ifdef BK_USING_PTHREADS
  pthread_t				bds_tid; 		///< Used to note thread-specific stats.
#endif /* BK_USING_PTHREADS */
  bk_flags				bds_flags;		///< Everyone needs flags
};

#define bds_name		bds_key.bdsk_name
#define bds_discriminator	bds_key.bdsk_discriminator
#define bds_int32		bds_value.bdsv_int32
#define bds_uint32		bds_value.bdsv_uint32
#define bds_int64		bds_value.bdsv_int64
#define bds_uint64		bds_value.bdsv_uint64
#define bds_float		bds_value.bdsv_float
#define bds_double		bds_value.bdsv_double
#define bds_string		bds_value.bdsv_string
#define bds_ptr			bds_value.bdsv_ptr

struct bk_dynamic_stats_list
{
  dict_h			bdsl_list;	///< Data struct containing all the stats
  bk_recursive_lock_h		bdsl_rlock;	///< Lock out other threads (recursive lock)
  bk_flags			bdsl_flags;	///< Everyone needs flags
#define BDSL_FLAG_RLOCK_INITIALIZED	0x1	// Indicates whether the mutex has been initialized.
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
static int stats_list_ko_cmp(struct bk_dynamic_stat_key *a, struct bk_dynamic_stat *b);

static struct bk_dynamic_stat *bds_create(bk_s B, bk_flags flags);
static void bds_destroy(bk_s B, struct bk_dynamic_stat *bds);
static struct bk_dynamic_stats_list*bdsl_create(bk_s B, bk_flags flags);
static void bdsl_destroy(bk_s B, struct bk_dynamic_stats_list *bdsl);
static int bdsl_getnext(bk_s B, struct bk_dynamic_stats_list *bdsl, struct bk_dynamic_stat **bdsp, u_int priority, bk_dynamic_stats_value_type_e *typep, bk_dynamic_stat_value_u *valuep, bk_flags flags);
static int stat_set(bk_s B, struct bk_dynamic_stat *bds, void *data, bk_flags flags);
static int extract_value(bk_s B, struct bk_dynamic_stat *bds, void *buf, u_int len, bk_flags flags);
static int dynamic_stat_get(bk_s B, struct bk_dynamic_stat *bds, bk_dynamic_stat_value_u *valuep, bk_dynamic_stats_value_type_e *typep, int *priorityp, void **opaquep, bk_flags flags);
static int elapsed_time_update(bk_s B, bk_dynamic_stats_h stats_list, bk_dynamic_stat_h dyn_stat, bk_flags flags);
static int virtual_memory_update(bk_s B, bk_dynamic_stats_h stats_list, bk_dynamic_stat_h dyn_stat, bk_flags flags);
static int resident_memory_update(bk_s B, bk_dynamic_stats_h stats_list, bk_dynamic_stat_h dyn_stat, bk_flags flags);
static int total_cpu_update(bk_s B, bk_dynamic_stats_h stats_list, bk_dynamic_stat_h dyn_stat, bk_flags flags);
#ifndef NO_THREAD_CPU_STAT
#ifdef BK_USING_PTHREADS
static int stats_thread_cpu_time_register(bk_s B, bk_dynamic_stats_h stats_list, void *bt, bk_dynamic_stat_h *statp, bk_flags flags);
static void thread_cpu_time_destroy (bk_s B, void *data);
static int thread_cpu_time_update(bk_s B, bk_dynamic_stats_h stats_list, bk_dynamic_stat_h dyn_stat, bk_flags flags);
static int manage_thread_stats(bk_s B, struct bk_dynamic_stats_list *bdsl, bk_flags flags);
#endif /* NO_THREAD_CPU_STAT */
#endif /* BK_USING_PTHREADS */
static struct bk_dynamic_stat *stat_search(bk_s B, struct bk_dynamic_stats_list *bdsl, const char *name, long discriminator);


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
    free((char *)bds->bds_name);

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

  if (!(bdsl->bdsl_list = stats_list_create((dict_function)stats_list_oo_cmp, (dict_function)stats_list_ko_cmp, DICT_UNIQUE_KEYS /* | DICT_BALANCED_TREE */ /*, &stats_list_args */)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create dynamic statistics list: %s\n", stats_list_error_reason(NULL, NULL));
    goto error;
  }

  if (!(bdsl->bdsl_rlock = bk_recursive_lock_create(B, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create dynamic statistics recursive lock: %s\n", strerror(errno));
    goto error;
  }

  BK_FLAG_SET(bdsl->bdsl_flags, BDSL_FLAG_RLOCK_INITIALIZED);

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

  if (BK_FLAG_ISSET(bdsl->bdsl_flags, BDSL_FLAG_RLOCK_INITIALIZED) &&
      (bk_recursive_lock_grab(B, bdsl->bdsl_rlock, 0) < 0))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not obtain recursive lock. Proceding anyway (dangerous)\n");
  }

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
  if (BK_FLAG_ISSET(bdsl->bdsl_flags, BDSL_FLAG_RLOCK_INITIALIZED))
  {
    if (bk_recursive_lock_release(B, bdsl->bdsl_rlock, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not release recursive lock. Proceding anyway (dangerous)\n");
    }
    bk_recursive_lock_destroy(B, bdsl->bdsl_rlock);
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



#define STATS_LIST_LOCK(bdsl,locked)						\
do										\
{										\
  if (BK_FLAG_ISSET(bdsl->bdsl_flags, BDSL_FLAG_RLOCK_INITIALIZED))		\
  {										\
    if (bk_recursive_lock_grab(B, (bdsl)->bdsl_rlock, 0) < 0)			\
    {										\
      bk_error_printf(B, BK_ERR_ERR, "Could not grab recursive lock\n");	\
      goto error;								\
    }										\
    locked = 1;									\
  }										\
} while(0)



#define STATS_LIST_UNLOCK(bdsl,locked)						\
do										\
{										\
  if ((locked) && (bk_recursive_lock_release(B, (bdsl)->bdsl_rlock, 0) < 0))	\
  {										\
    bk_error_printf(B, BK_ERR_ERR, "Could not release recursive lock\n");	\
    goto error;									\
  }										\
  locked = 0;									\
} while(0)



/**
 * Helper function which basically absorbs a lot of repeated code in the
 * stat registration functions. This function determines the
 * name/description of the stat, the priority of the stat, and whether the
 * stat is even desired.
 *
 *	@param B BAKA thread/global state.
 *	@param ignore_key The bk_conf key indicating whether the stat is desired.
 *	@param name_key The bk_conf key for the stat name/description
 *	@param default_name The default stat name/description.
 *	@param priority_key The bk_conf key for the stat priority
 *	@param default_priority The default priority value (NB: in string format).
 *	@param namep C/O of the computed name/description.
 *	@param priorityp C/O of the computed priority
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> if the stat is not desired.
 *	@return <i>1</i> if the stat is desired.
 */
int
bk_dynamic_stat_preregistration(bk_s B, const char *ignore_key, const char *name_key, const char *default_name, const char *priority_key, const char *default_priority, const char **namep, u_int *priorityp, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if ((namep && !default_name) || (!namep && default_name) ||
      (priorityp && !default_priority) || (!priorityp && default_priority))
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (ignore_key && !BK_STREQ(BK_GWD(B, ignore_key, "false"),"false"))
  {
    // If the ignore key exists and is *not* set to "false", the stat is not desired
    BK_RETURN(B, 0);
  }

  if (namep)
  {
    if (name_key)
    {
      *namep = BK_GWD(B, (char *)name_key, (char *)default_name);
    }
    else
    {
      *namep = default_name;
    }
  }

  if (priorityp && (bk_string_atou32(B, BK_GWD(B, priority_key, default_priority), (u_int32_t *)priorityp, 0) != 0))
  {
    bk_error_printf(B, BK_ERR_ERR, "Failed to convert priority string into a u_int\n");
    goto error;
  }

  BK_RETURN(B, 1);

 error:
  BK_RETURN(B, -1);
}



/**
 * Register a stat.
 *
 *	@param B BAKA thread/global state.
 *	@param stats_list The list stats to which to add this new stat
 *	@param name  The name of the stat (for lookup).
 *	@param discriminator Distinguish same-named stats.
 *	@param priority The priority of the stat.
 *	@param value_type The storage type of the value.
 *	@param access_type How to access the value (ie directly or indirectly)
 *	@param update_callback Optional callback for demaind updates
 *	@param opaque Optional user defined data
 *	@param destroy_callback Callback used to free @opaque (optional obviously)
 *	@param statp Optional C/O of newly created stat.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stat_register_simple(bk_s B, bk_dynamic_stats_h stats_list, const char *name, long discriminator, u_int priority, bk_dynamic_stats_value_type_e value_type, bk_dynamic_stats_access_type_e access_type, bk_dynamic_stat_update_h update_callback, void *opaque, bk_dynamic_stat_destroy_h destroy_callback, bk_dynamic_stat_h *statp, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  struct bk_dynamic_stat *bds = NULL;
  struct bk_dynamic_stat *obds = NULL;
  int locked = 0;

  if (!bdsl || !name)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (statp)
    *statp = NULL;

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

  bds->bds_discriminator = discriminator;
  bds->bds_value_type = value_type;
  bds->bds_access_type = access_type;
  bds->bds_priority = priority;
  bds->bds_opaque = opaque;
  bds->bds_update_callback = update_callback;
  bds->bds_destroy_callback = destroy_callback;

  STATS_LIST_LOCK(bdsl, locked);

  /*
   * Insert stat. If an identical stat already exists and the caller wished
   * idempotency, then check if the value and access type are identical. If
   * so, destroy the incoming opaque and return 0.
   */

  if (stats_list_insert_uniq(bdsl->bdsl_list, bds, (dict_obj *)&obds) != DICT_OK)
  {
    if (BK_FLAG_ISCLEAR(flags, BK_DYNAMIC_STAT_REGISTER_FLAG_IDEMPOTENT))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not insert bds in bdsl: %s\n", stats_list_error_reason(bdsl->bdsl_list, NULL));
      goto error;
    }

    if ((bds->bds_value_type == obds->bds_value_type) &&
	(bds->bds_access_type == obds->bds_access_type))
    {
      if (opaque && destroy_callback)
      {
	(*destroy_callback)(B, opaque);
      }
      BK_RETURN(B, 0);
    }
  }

  if (statp)
    *statp = bds;

  bds = NULL;

  STATS_LIST_UNLOCK(bdsl, locked);

  BK_RETURN(B, 0);

 error:
  if (bds)
    bds_destroy(B, bds);

  STATS_LIST_UNLOCK(bdsl, locked);

  BK_RETURN(B, -1);
}



/**
 * Long version of registration which does handles preregistration and
 * registration in one call. NB: There is a potential problem with the
 * @flags argument in this function since it needs to (potentially) supply
 * flags to two other bk_dynamic_stat functions. But we ignore this problem
 * for the moment seeing as neither of those two said functions current
 * take any flags.
 *
 * NB: It is entirely possibly for this function to succeed but *statp to
 * NULL. This is the case where the stat being registered is not wanted, as
 * determined from the conf file. Therefore it is necessary to test the
 * value of *statp before using it.
 *
 *	@param B BAKA thread/global state.
 *	@param stats_list The list stats to which to add this new stat
 *	@param ignore_key The bk_conf key indicating whether the stat is desired.
 *	@param name_key The bk_conf key for the stat name/description
 *	@param default_name The default stat name/description.
 *	@param namep Optional C/O of the name that is actually in use.
 *	@param discriminator A value that allows multiple stats with the same name to exist.
 *	@param priority_key The bk_conf key for the stat priority
 *	@param default_priority The default priority value (NB: in string format).
 *	@param priorityp Optional C/O of the priority that is actually in use.
 *	@param value_type The storage type of the value.
 *	@param access_type How to access the value (ie directly or indirectly)
 *	@param update_callback Optional callback for demaind updates
 *	@param opaque Optional user defined data
 *	@param destroy_callback Callback used to free @opaque (optional obviously)
 *	@param statp Optional C/O of newly created stat.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stat_register(bk_s B, bk_dynamic_stats_h stats_list, const char *ignore_key, const char *name_key, const char *default_name, const char **namep, long discriminator, const char *priority_key, const char *default_priority, u_int *priorityp, bk_dynamic_stats_value_type_e value_type, bk_dynamic_stats_access_type_e access_type, bk_dynamic_stat_update_h update_callback, void *opaque, bk_dynamic_stat_destroy_h destroy_callback, bk_dynamic_stat_h *statp, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  const char *name;
  u_int priority;
  int wanted;
  int locked = 0;

  if (!bdsl)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (namep)
    *namep = NULL;

  if (priorityp)
    *priorityp = 0;

  if (statp)
    *statp = NULL;

  STATS_LIST_LOCK(bdsl, locked);

  if ((wanted = bk_dynamic_stat_preregistration(B, ignore_key, name_key, default_name, priority_key, default_priority, &name, &priority, 0)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not perform stat preregistration tasks\n");
    goto error;
  }

  if (namep)
    *namep = name;

  if (priorityp)
    *priorityp = priority;

  if (!wanted)
  {
    BK_RETURN(B, 0);
  }

  if (bk_dynamic_stat_register_simple(B, bdsl, name, discriminator, priority, value_type, access_type, update_callback, opaque, destroy_callback, statp, flags) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not register stat with description: %s:%ld\n", name, discriminator);
    goto error;
  }

  STATS_LIST_UNLOCK(bdsl, locked);
  BK_RETURN(B, 0);

 error:
  STATS_LIST_UNLOCK(bdsl, locked);
  BK_RETURN(B, -1);
}



/**
 * Register a stat where the first value is known at the time of
 * registration. This is of course just a convenience function.
 *
 *	@param B BAKA thread/global state.
 *	@param stats_list The list stats to which to add this new stat
 *	@param name  The name of the stat (for lookup).
 *	@param discriminator Distinguish same-named stats.
 *	@param value_type The storage type of the value.
 *	@param access_type How to access the value (ie directly or indirectly)
 *	@param update_callback Optional callback for demaind updates
 *	@param opaque Optional user defined data
 *	@param destroy_callback Callback used to free @opaque (optional obviously)
 *	@param flags Flags for future use.
 *	@param statp Optional C/O of newly created stat.
 *	@param value The value to set (variable typed argument)
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stat_register_with_value_simple(bk_s B, bk_dynamic_stats_h stats_list, const char *name, long discriminator, u_int priority, bk_dynamic_stats_value_type_e value_type, bk_dynamic_stats_access_type_e access_type, bk_dynamic_stat_update_h update_callback, void *opaque, bk_dynamic_stat_destroy_h destroy_callback, bk_dynamic_stat_h *statp, bk_flags flags, ...)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  bk_dynamic_stat_h bds;
  int locked = 0;
  bk_dynamic_stat_value_u bdsv;
  va_list ap;

  if (!bdsl || !name)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (statp)
    *statp = NULL;

  STATS_LIST_LOCK(bdsl, locked);

  if (bk_dynamic_stat_register_simple(B, bdsl, name, discriminator, priority, value_type, access_type, update_callback, opaque, destroy_callback, statp, flags) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not register dynamic stat described as: %s:%ld\n", name, discriminator);
    goto error;
  }

  va_start(ap, flags);

  if (access_type == DynamicStatsAccessTypeDirect)
  {
    switch(value_type)
    {
    case DynamicStatsValueTypeInt32:
      bdsv.bdsv_int32 = va_arg(ap, int32_t);
      break;
    case DynamicStatsValueTypeUInt32:
      bdsv.bdsv_uint32 = va_arg(ap, u_int32_t);
      break;
    case DynamicStatsValueTypeInt64:
      bdsv.bdsv_int64 = va_arg(ap, int64_t);
      break;
    case DynamicStatsValueTypeUInt64:
      bdsv.bdsv_uint64 = va_arg(ap, u_int64_t);
      break;
    case DynamicStatsValueTypeFloat:
      bdsv.bdsv_float = va_arg(ap, double);
      break;
    case DynamicStatsValueTypeDouble:
      bdsv.bdsv_double = va_arg(ap, double);
      break;
    case DynamicStatsValueTypeString:
      bdsv.bdsv_string = va_arg(ap, char *);
      break;
    default:
      bk_error_printf(B, BK_ERR_ERR,"Unknown value type: %d\n", value_type);
      va_end(ap);
      goto error;
      break;
    }
  }
  else
  {
    bdsv.bdsv_ptr = va_arg(ap, void *);
  }

  va_end(ap);

  if (bk_dynamic_stat_get(B, bdsl, name, discriminator, &bds, NULL, NULL, NULL, NULL, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not obtain stat structure that we just created\n");
    goto error;
  }

  if (stat_set(B, bds, &bdsv, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set value\n");
    goto error;
  }

  STATS_LIST_UNLOCK(bdsl, locked);
  BK_RETURN(B, 0);

 error:
  STATS_LIST_UNLOCK(bdsl, locked);
  BK_RETURN(B, -1);
}




/**
 * Register a stat where the first value is known at the time of
 * registration. This is of course just a convenience function.
 *
 *	@param B BAKA thread/global state.
 *	@param stats_list The list stats to which to add this new stat
 *	@param name_key The bk_conf key for the stat name/description
 *	@param default_name The default stat name/description.
 *	@param namep C/O of the computed name/description.
 *	@param discriminator Distinguish same-named stats.
 *	@param priority_key The bk_conf key for the stat priority
 *	@param default_priority The default priority value (NB: in string format).
 *	@param priorityp C/O of the computed priority
 *	@param value_type The storage type of the value.
 *	@param access_type How to access the value (ie directly or indirectly)
 *	@param update_callback Optional callback for demaind updates
 *	@param opaque Optional user defined data
 *	@param destroy_callback Callback used to free @opaque (optional obviously)
 *	@param statp Optional C/O of newly created stat.
 *	@param flags Flags for future use.
 *	@param value The value to set (variable typed argument)
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new <i>stat</i> on success.
 */
int
bk_dynamic_stat_register_with_value(bk_s B, bk_dynamic_stats_h stats_list, const char *ignore_key, const char *name_key, const char *default_name, const char **namep, long discriminator, const char *priority_key, const char *default_priority, u_int *priorityp, bk_dynamic_stats_value_type_e value_type, bk_dynamic_stats_access_type_e access_type, bk_dynamic_stat_update_h update_callback, void *opaque, bk_dynamic_stat_destroy_h destroy_callback, bk_dynamic_stat_h *statp, bk_flags flags, ...)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  bk_dynamic_stat_h bds;
  const char *name;
  u_int priority;
  int wanted;
  int locked = 0;
  bk_dynamic_stat_value_u bdsv;
  va_list ap;

  if (!bdsl) // bk_dynamic_stat_preregistration checks the rest of the arguments
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (namep)
    *namep = NULL;

  if (priorityp)
    *priorityp = 0;

  if (statp)
    *statp = NULL;

  STATS_LIST_LOCK(bdsl, locked);

  if ((wanted = bk_dynamic_stat_preregistration(B, ignore_key, name_key, default_name, priority_key, default_priority, &name, &priority, 0)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not perform stat preregistration tasks\n");
    goto error;
  }

  if (namep)
    *namep = name;

  if (priorityp)
    *priorityp = priority;

 if (!wanted)
  {
    BK_RETURN(B, 0);
  }

 if (bk_dynamic_stat_register_simple(B, bdsl, name, discriminator, priority, value_type, access_type, update_callback, opaque, destroy_callback, statp, flags) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not register stat dscribed as: %s:%ld\n", name, discriminator);
    goto error;
  }

  va_start(ap, flags);

  if (access_type == DynamicStatsAccessTypeDirect)
  {
    switch(value_type)
    {
    case DynamicStatsValueTypeInt32:
      bdsv.bdsv_int32 = va_arg(ap, int32_t);
      break;
    case DynamicStatsValueTypeUInt32:
      bdsv.bdsv_uint32 = va_arg(ap, u_int32_t);
      break;
    case DynamicStatsValueTypeInt64:
      bdsv.bdsv_int64 = va_arg(ap, int64_t);
      break;
    case DynamicStatsValueTypeUInt64:
      bdsv.bdsv_uint64 = va_arg(ap, u_int64_t);
      break;
    case DynamicStatsValueTypeFloat:
      bdsv.bdsv_float = va_arg(ap, double);
      break;
    case DynamicStatsValueTypeDouble:
      bdsv.bdsv_double = va_arg(ap, double);
      break;
    case DynamicStatsValueTypeString:
      bdsv.bdsv_string = va_arg(ap, char *);
      break;
    default:
      bk_error_printf(B, BK_ERR_ERR,"Unknown value type: %d\n", value_type);
      va_end(ap);
      goto error;
      break;
    }
  }
  else
  {
    bdsv.bdsv_ptr = va_arg(ap, void *);
  }

  va_end(ap);

  if (bk_dynamic_stat_get(B, bdsl, name, discriminator, &bds, NULL, NULL, NULL, NULL, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not obtain stat structure that we just created\n");
    goto error;
  }

  if (stat_set(B, bds, &bdsv, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set value\n");
    goto error;
  }

  STATS_LIST_UNLOCK(bdsl, locked);
  BK_RETURN(B, 0);

 error:
  STATS_LIST_UNLOCK(bdsl, locked);
  BK_RETURN(B, -1);
}


/**
 * Deregister a stat
 *
 *	@param B BAKA thread/global state.
 *	@param stats_list The stats list from which to deregister.
 *	@param name The name of the stat.
 *	@param discriminator Distinguish same-named stats.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stat_deregister(bk_s B, bk_dynamic_stats_h stats_list, const char *name, long discriminator, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  struct bk_dynamic_stat *bds = NULL;
  int locked = 0;

  if (!bdsl || !name)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  STATS_LIST_LOCK(bdsl, locked);

  if (!(bds = stat_search(B, bdsl, name, discriminator)))
  {
    bk_error_printf(B, BK_ERR_WARN, "Could not find a statistic described as: %s:%ld\n", name, discriminator);
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

  STATS_LIST_UNLOCK(bdsl, locked);

  BK_RETURN(B, 0);

 error:
  STATS_LIST_UNLOCK(bdsl, locked);

  BK_RETURN(B, -1);
}



#define EXTRACT_BDS_VALUE(bds, buf, len, value)						\
do											\
{											\
  if ((len) < sizeof(typeof(value)))							\
  {											\
    bk_error_printf(B, BK_ERR_ERR, "Insufficent space to copy variable of type\n");	\
    goto error;										\
  }											\
											\
  if ((bds)->bds_access_type == DynamicStatsAccessTypeDirect)				\
  {											\
    *((typeof(value) *)(buf)) = (value);						\
  }											\
  else											\
  {											\
    *((typeof(value) *)(buf)) = *((typeof(value) *)((bds)->bds_ptr));			\
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
    EXTRACT_BDS_VALUE(bds, buf, len, bds->bds_int32);
    break;
  case DynamicStatsValueTypeUInt32:
    EXTRACT_BDS_VALUE(bds, buf, len, bds->bds_uint32);
    break;
  case DynamicStatsValueTypeInt64:
    EXTRACT_BDS_VALUE(bds, buf, len, bds->bds_int64);
    break;
  case DynamicStatsValueTypeUInt64:
    EXTRACT_BDS_VALUE(bds, buf, len, bds->bds_uint64);
    break;
  case DynamicStatsValueTypeFloat:
    EXTRACT_BDS_VALUE(bds, buf, len, bds->bds_float);
    break;
  case DynamicStatsValueTypeDouble:
    EXTRACT_BDS_VALUE(bds, buf, len, bds->bds_double);
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
 * Get stats info.
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
static int
dynamic_stat_get(bk_s B, struct bk_dynamic_stat *bds, bk_dynamic_stat_value_u *valuep, bk_dynamic_stats_value_type_e *typep, int *priorityp, void **opaquep, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bds)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (priorityp)
    *priorityp = bds->bds_priority;

  if (typep)
    *typep = bds->bds_value_type;

  if (opaquep)
    *opaquep = bds->bds_opaque;

  if (valuep)
  {
    if (extract_value(B, bds, valuep, sizeof(*valuep), 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not extract value from statistic described as: %s:%ld\n", bds->bds_name, bds->bds_discriminator);
      goto error;
    }
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
 *	@param stats_list The stats list.
 *	@param name The name/description of the stat.
 *	@param discriminator Distinguish same-named stats.
 *	@param stat Optional C/O of the bk_dynamic_stat_h object
 *	@param valuep Optional C/O of the value.
 *	@param typep Optional C/O of the value type.
 *	@param priorityp Optional C/O of the priority.
 *	@param opaquep Optional C/O of the opaque pointer.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stat_get(bk_s B, bk_dynamic_stats_h stats_list, const char *name, long discriminator, bk_dynamic_stat_h *statp, bk_dynamic_stat_value_u *valuep, bk_dynamic_stats_value_type_e *typep, int *priorityp, void **opaquep, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  struct bk_dynamic_stat *bds;
  int locked = 0;
  int ret;

  if (!bdsl || !name)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  STATS_LIST_LOCK(bdsl, locked);

  if (!(bds = stat_search(B, bdsl, name, discriminator)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not locate a statisitic described as: %s:%ld\n", name, discriminator);
    goto error;
  }

  if (statp)
    *statp = bds;

  ret = dynamic_stat_get(B, bds, valuep, typep, priorityp, opaquep, flags);

  STATS_LIST_UNLOCK(bdsl, locked);

  BK_RETURN(B, ret);

 error:
  STATS_LIST_UNLOCK(bdsl, locked);

  BK_RETURN(B, -1);
}



/**
 * Set the value of a dynamic stat (internal version)
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
stat_set(bk_s B, struct bk_dynamic_stat *bds, void *data, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bds || !data)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (bds->bds_access_type == DynamicStatsAccessTypeDirect)
  {
    switch(bds->bds_value_type)
    {
    case DynamicStatsValueTypeInt32:
      bds->bds_int32 = *(int32_t*)data;
      break;
    case DynamicStatsValueTypeUInt32:
      bds->bds_uint32 = *(u_int32_t*)data;
      break;
    case DynamicStatsValueTypeInt64:
      bds->bds_int64 = *(int64_t*)data;
      break;
    case DynamicStatsValueTypeUInt64:
      bds->bds_uint64 = *(u_int64_t*)data;
      break;
    case DynamicStatsValueTypeFloat:
      bds->bds_float = *(float*)data;
      break;
    case DynamicStatsValueTypeDouble:
      bds->bds_double = *(double*)data;
      break;
    case DynamicStatsValueTypeString:
      {
	if (bds->bds_string)
	  free(bds->bds_string);
	if (*(char **)data)
	{
	  if (!(bds->bds_string = strdup(*(char **)data)))
	  {
	    bk_error_printf(B, BK_ERR_ERR, "Could not copy string: %s\n", strerror(errno));
	    goto error;
	  }
	}
	else
	{
	  bds->bds_string = NULL;
	}
      }
      break;
    default:
      bk_error_printf(B, BK_ERR_ERR,"Unknown value type: %d\n", bds->bds_value_type);
      break;
    }
  }
  else
  {
    bds->bds_ptr = *(void **)data;
  }
  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}



/**
 * Set the value of a dynamic stat
 *
 *	@param B BAKA thread/global state.
 *	@param stats_list The stats list.
 *	@param name The name of the statistic to upate.
 *	@param discriminator Distinguish same named stats.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stat_set(bk_s B, bk_dynamic_stats_h stats_list, const char *name, long discriminator, bk_flags flags, ...)
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

  STATS_LIST_LOCK(bdsl, locked);

  if (!(bds = stat_search(B, bdsl, name, discriminator)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not locate statistics decribed as: %s:%ld\n", name, discriminator);
    goto error;
  }

  if (bds->bds_access_type == DynamicStatsAccessTypeIndirect)
  {
    bk_error_printf(B, BK_ERR_ERR, "Indirect stats should not use %s\n", __FUNCTION__);
    goto error;
  }

  va_start(ap, flags);

  if (bds->bds_access_type == DynamicStatsAccessTypeDirect)
  {
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
	if (bds->bds_string)
	  free(bds->bds_string);
	if (s)
	{
	  if (!(bds->bds_string = strdup(s)))
	  {
	    bk_error_printf(B, BK_ERR_ERR, "Could not copy string: %s\n", strerror(errno));
	    goto error;
	  }
	}
	else
	{
	  bds->bds_string = NULL;
	}
      }
      break;
    default:
      bk_error_printf(B, BK_ERR_ERR, "Unknown statistics value type: %d\n", bds->bds_value_type);
      goto error;
      break;
    }
  }
  else
  {
    bds->bds_ptr = va_arg(ap, void *);
  }

  va_end(ap);

  STATS_LIST_UNLOCK(bdsl, locked);

  BK_RETURN(B, 0);

 error:
  STATS_LIST_UNLOCK(bdsl, locked);

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
 *	@param discriminator Distinguish same-named stats.
 *	@param value_type The type of the stat value
 *	@param access-type The access type of the stat
 *	@param priority The priority of the stat
 *	@param update_callback The callback for demand updates
 *	@param opaque The user opaque data.
 *	@param destroy_callback The callback that destroys @a opaque
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stat_attr_update(bk_s B, bk_dynamic_stats_h stats_list, const char *name, long discriminator, bk_dynamic_stats_value_type_e value_type, bk_dynamic_stats_access_type_e access_type, int priority, bk_dynamic_stat_update_h update_callback, void *opaque, bk_dynamic_stat_destroy_h destroy_callback, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  struct bk_dynamic_stat *bds;

  if (!bdsl || !name)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!(bds = stat_search(B, bdsl, name, discriminator)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not find a statistic described as: %s:%ld\n", name, discriminator);
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
 *	@param discriminator Distinguish same-named stats.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stat_increment(bk_s B, bk_dynamic_stats_h *stats_list, const char *name, long discriminator, bk_flags flags, ...)
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

  STATS_LIST_LOCK(bdsl, locked);

  if (!(bds = stat_search(B, bdsl, name, discriminator)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not locate a statistics described as %s:%ld\n", name, discriminator);
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

  STATS_LIST_UNLOCK(bdsl, locked);

  BK_RETURN(B, 0);

 error:
  STATS_LIST_UNLOCK(bdsl, locked);

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
  int locked = 0;
  int ret;

  if (!bdsl)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  STATS_LIST_LOCK(bdsl, locked);

  ret = bdsl_getnext(B, bdsl, bdsp, priority, typep, valuep, flags);

  STATS_LIST_UNLOCK(bdsl, locked);

  BK_RETURN(B, ret);

 error:
  STATS_LIST_UNLOCK(bdsl, locked);
  BK_RETURN(B, -1);
}



#define STAT_XML_OUTPUT(bds, fmt, value)							\
do												\
{												\
  typeof(value) __value;									\
												\
  if (((bds)->bds_access_type == DynamicStatsAccessTypeIndirect) && !bds->bds_ptr)		\
  {												\
    const char *__null_value;									\
    if ((bds)->bds_value_type == DynamicStatsValueTypeString)					\
    {												\
      __null_value = "NULL";									\
    }												\
    else if (((bds)->bds_value_type == DynamicStatsValueTypeFloat) ||				\
	     ((bds)->bds_value_type == DynamicStatsValueTypeDouble))				\
    {												\
      __null_value = "0.0";									\
    }												\
    else											\
    {												\
      __null_value = "0";									\
    }												\
												\
    if ((bds)->bds_discriminator == 0)								\
    {												\
      vptr_ret = bk_vstr_cat(B, 0, &xml_vstr,							\
			     "%s\t<statistic description=\"%s\">%s</statistic>\n",		\
			     prefix, (bds)->bds_name, __null_value);				\
    }												\
    else											\
    {												\
      vptr_ret = bk_vstr_cat(B, 0, &xml_vstr,							\
			     "%s\t<statistic description=\"%s\" discriminator=\"%ld\">%s"	\
			     "</statistic>\n",							\
			     prefix, (bds)->bds_name, (bds)->bds_discriminator,			\
			     __null_value);							\
    }												\
  }												\
  else												\
  {												\
    if ((bds)->bds_access_type == DynamicStatsAccessTypeDirect)					\
    {												\
      __value = (value);									\
    }												\
    else											\
    {												\
      __value = *(typeof(value) *)((bds)->bds_ptr);						\
    }												\
												\
    if ((bds)->bds_discriminator == 0)								\
    {												\
      vptr_ret = bk_vstr_cat(B, 0, &xml_vstr,							\
			     "%s\t<statistic description=\"%s\">"fmt"</statistic>\n",		\
			     prefix, (bds)->bds_name, __value);					\
    }												\
    else											\
    {												\
      vptr_ret = bk_vstr_cat(B, 0, &xml_vstr,							\
			     "%s\t<statistic description=\"%s\" discriminator=\"%ld\">"fmt	\
			     "</statistic>\n",							\
			     prefix, (bds)->bds_name, (bds)->bds_discriminator, __value);	\
    }												\
  }												\
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

  if (!bdsl || !prefix)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

#ifdef BK_USING_PTHREADS
#ifndef NO_THREAD_CPU_STAT
  if (manage_thread_stats(B, bdsl, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not manage thread stats\n");
    goto error;
  }
#endif /* NO_THREAD_CPU_STAT */
#endif /* BK_USING_PTHREADS */

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

  STATS_LIST_LOCK(bdsl, locked);

  if (bk_dynamic_stats_demand_update(B, bdsl, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not run the statistics demand update\n");
    goto error;
  }

  while((ret = bdsl_getnext(B, bdsl, &bds, priority, NULL, NULL, 0)) == 1)
  {
    int vptr_ret;
    switch(bds->bds_value_type)
    {
    case DynamicStatsValueTypeInt32:
      STAT_XML_OUTPUT(bds, "%d", bds->bds_int32);
      break;
    case DynamicStatsValueTypeUInt32:
      STAT_XML_OUTPUT(bds, "%u", bds->bds_uint32);
      break;
    case DynamicStatsValueTypeInt64:
      STAT_XML_OUTPUT(bds, "%ld", bds->bds_int64);
      break;
    case DynamicStatsValueTypeUInt64:
      STAT_XML_OUTPUT(bds, "%lu", bds->bds_uint64);
      break;
    case DynamicStatsValueTypeFloat:
      STAT_XML_OUTPUT(bds, "%.4f", bds->bds_float);
      break;
    case DynamicStatsValueTypeDouble:
      STAT_XML_OUTPUT(bds, "%.4lf", bds->bds_double);
      break;
    case DynamicStatsValueTypeString:
      if (!bds->bds_string && !(bds->bds_string = strdup("")))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not strdup emptry string: %s\n", strerror(errno));
	goto error;
      }
      if (!(xml_str = bk_string_str2xml(B, bds->bds_string, 0)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not convert string to XML\n");
	goto error;
      }
      STAT_XML_OUTPUT(bds, "%s", xml_str);
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

  STATS_LIST_UNLOCK(bdsl, locked);

  BK_RETURN(B, xml_vstr.ptr);

 error:
  STATS_LIST_UNLOCK(bdsl, locked);

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
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  int locked = 0;

  if (!bdsl)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  STATS_LIST_LOCK(bdsl, locked);

  if (bk_dynamic_stat_pid_register(B, stats_list, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not register and set process ID statistic\n");
    goto error;
  }

  if (bk_dynamic_stat_start_time_register(B, stats_list, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not register and set process start time statistic\n");
    goto error;
  }

  if (bk_dynamic_stat_elapsed_time_register(B, stats_list, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not register the process start time statistic\n");
    goto error;
  }

  if (bk_dynamic_stat_virtual_memory_register(B, stats_list, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not register the process total virtual memory statistic\n");
    goto error;
  }

  if (bk_dynamic_stat_resident_memory_register(B, stats_list, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not register the process total virtual memory statistic\n");
    goto error;
  }

  if (bk_dynamic_stat_total_cpu_time_register(B, stats_list, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not register the process total virtual memory statistic\n");
    goto error;
  }

  STATS_LIST_UNLOCK(bdsl, locked);

  BK_RETURN(B, 0);

 error:
  STATS_LIST_UNLOCK(bdsl, locked);

  BK_RETURN(B, -1);
}




/**
 * Demand update all.
 *
 *	@param B BAKA thread/global state.
 *	@param stats_list The statistic list to update
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stats_demand_update(bk_s B, bk_dynamic_stats_h stats_list, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  struct bk_dynamic_stat *bds;
  int locked = 0;

  if (!bdsl)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  STATS_LIST_LOCK(bdsl, locked);
  for(bds = stats_list_minimum(bdsl->bdsl_list);
      bds;
      bds = stats_list_successor(bdsl->bdsl_list, bds))
  {
    if (bds->bds_update_callback && ((*bds->bds_update_callback)(B, bdsl, bds, 0) < 0))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not update statsitic described by: %s\n", bds->bds_name);
      goto error;
    }
  }

  STATS_LIST_UNLOCK(bdsl, locked);
  BK_RETURN(B, 0);

 error:
  STATS_LIST_UNLOCK(bdsl, locked);
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
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  int locked = 0;

  if (!stats_list)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  STATS_LIST_LOCK(bdsl, locked);

  if (bk_dynamic_stat_register_with_value(B, stats_list, BK_DYNAMIC_STAT_IGNORE_KEY_PID, BK_DYNAMIC_STAT_NAME_KEY_PID, BK_DYNAMIC_STAT_DEFAULT_NAME_PID, NULL, 0, BK_DYNAMIC_STAT_PRIORITY_KEY_PID, BK_DYNAMIC_STAT_DEFAULT_PRIORITY_PID, NULL, DynamicStatsValueTypeUInt32, DynamicStatsAccessTypeDirect, NULL, NULL, NULL, 0, getpid()) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not reigster PID statistic");
    goto error;
  }

  STATS_LIST_UNLOCK(bdsl, locked);
  BK_RETURN(B, 0);

 error:
  STATS_LIST_UNLOCK(bdsl, locked);
  BK_RETURN(B, -1);
}




/**
 * Register and set the start time.
 *
 *	@param B BAKA thread/global state.
 *	@param stats_list The list of stats.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stat_start_time_register(bk_s B, bk_dynamic_stats_h stats_list, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  char time_buf[100];
  time_t start_time;
  int locked = 0;

  if (!stats_list)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  STATS_LIST_LOCK(bdsl, locked);

  if ((start_time = BK_GENERAL_START_TIME(B)) == (time_t)-1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Start time is not set\n");
    goto error;
  }

  /* Since this is a fixed value we immediately update it */
  if (strftime(time_buf, sizeof(time_buf), BK_STRFTIME_DEFAULT_FMT, gmtime(&start_time)) == 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not convert current time to string\n");
    goto error;
  }

  if (bk_dynamic_stat_register_with_value(B, stats_list, BK_DYNAMIC_STAT_IGNORE_KEY_START_TIME, BK_DYNAMIC_STAT_NAME_KEY_START_TIME, BK_DYNAMIC_STAT_DEFAULT_NAME_START_TIME, NULL, 0, BK_DYNAMIC_STAT_PRIORITY_KEY_START_TIME, BK_DYNAMIC_STAT_DEFAULT_PRIORITY_START_TIME, NULL, DynamicStatsValueTypeString, DynamicStatsAccessTypeDirect, NULL, NULL, NULL, NULL, 0, time_buf) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not register start time statistic");
    goto error;
  }

  STATS_LIST_UNLOCK(bdsl, locked);

  BK_RETURN(B, 0);

 error:
  STATS_LIST_UNLOCK(bdsl, locked);

  BK_RETURN(B, -1);
}



/**
 * Elapsed time register
 *
 *	@param B BAKA thread/global state.
 *	@param stats_list The list of stats.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stat_elapsed_time_register(bk_s B, bk_dynamic_stats_h stats_list, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  bk_dynamic_stats_value_type_e timet_stats_type;
  int locked = 0;

  if (!bdsl)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (sizeof(time_t) == sizeof(u_int64_t))
  {
    timet_stats_type = DynamicStatsValueTypeUInt64;
  }
  else
  {
    timet_stats_type = DynamicStatsValueTypeUInt32;
  }

  STATS_LIST_LOCK(bdsl, locked);

  if (bk_dynamic_stat_register(B, stats_list, BK_DYNAMIC_STAT_IGNORE_KEY_ELAPSED_TIME, BK_DYNAMIC_STAT_NAME_KEY_ELAPSED_TIME, BK_DYNAMIC_STAT_DEFAULT_NAME_ELAPSED_TIME, NULL, 0, BK_DYNAMIC_STAT_PRIORITY_KEY_ELAPSED_TIME, BK_DYNAMIC_STAT_DEFAULT_PRIORITY_ELAPSED_TIME, NULL, timet_stats_type, DynamicStatsAccessTypeDirect, elapsed_time_update, NULL, NULL, NULL, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not register elapsed time statistic\n");
    goto error;
  }

  STATS_LIST_UNLOCK(bdsl, locked);
  BK_RETURN(B, 0);

  error:
  STATS_LIST_UNLOCK(bdsl, locked);
  BK_RETURN(B, -1);
}



/**
 * Demand update function for total elapsed (real) time
 *
 *	@param B BAKA thread/global state.
 *	@param dyn_stat The elapsed time stat struct
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
elapsed_time_update(bk_s B, bk_dynamic_stats_h stats_list, bk_dynamic_stat_h dyn_stat, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stat *bds = (struct bk_dynamic_stat *)dyn_stat;
  time_t start_time;
  time_t elapsed_time;
  time_t timenow;

  if (!bds)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if ((start_time = BK_GENERAL_START_TIME(B)) == (time_t)-1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Start time is not set\n");
    goto error;
  }

  if (time(&timenow) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not obtain the current time: %s\n", strerror(errno));
    goto error;
  }

  elapsed_time = timenow - start_time;

  if (stat_set(B, bds, &elapsed_time, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set the elapsed time\n");
    goto error;
  }

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}



/**
 * Register the virutal memory stat
 *
 *	@param B BAKA thread/global state.
 *	@param stats_list The stats list.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stat_virtual_memory_register(bk_s B, bk_dynamic_stats_h stats_list, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  bk_dynamic_stats_value_type_e vsize_type;
  int locked = 0;
  struct bk_procinfo *bpi; // This is used only for the sizeof(bpi->vsize) operation

  if (!bdsl)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (sizeof(bpi->bpi_vsize) == sizeof(u_int32_t))
  {
    vsize_type = DynamicStatsValueTypeUInt32;
  }
  else
  {
    vsize_type = DynamicStatsValueTypeUInt64;
  }

  STATS_LIST_LOCK(bdsl, locked);

  if (bk_dynamic_stat_register(B, stats_list, BK_DYNAMIC_STAT_IGNORE_KEY_VIRT_MEM, BK_DYNAMIC_STAT_NAME_KEY_VIRT_MEM, BK_DYNAMIC_STAT_DEFAULT_NAME_VIRT_MEM, NULL, 0, BK_DYNAMIC_STAT_PRIORITY_KEY_VIRT_MEM, BK_DYNAMIC_STAT_DEFAULT_PRIORITY_VIRT_MEM, NULL, vsize_type, DynamicStatsAccessTypeDirect, virtual_memory_update, NULL, NULL, NULL, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not register virtual memory statistic\n");
    goto error;
  }

  STATS_LIST_UNLOCK(bdsl, locked);
  BK_RETURN(B, 0);

  error:
  STATS_LIST_UNLOCK(bdsl, locked);
  BK_RETURN(B, -1);
}




/**
 * Demand update function for virtual memory size
 *
 *	@param B BAKA thread/global state.
 *	@param dyn_stat The elapsed time stat struct
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
virtual_memory_update(bk_s B, bk_dynamic_stats_h stats_list, bk_dynamic_stat_h dyn_stat, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  struct bk_dynamic_stat *bds = (struct bk_dynamic_stat *)dyn_stat;
  dict_h procinfo_list;
  struct bk_procinfo *bpi = NULL;
  pid_t pid = getpid();

  if (!bdsl || !bds)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!(procinfo_list = bk_procinfo_create(B, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create procinfo struct for virt mem\n");
    goto error;
  }

  for(bpi = procinfo_list_minimum(procinfo_list);
      bpi;
      bpi = procinfo_list_successor(procinfo_list, bpi))
  {
    if (bpi->bpi_pid == pid)
      break;
  }

  if (!bpi)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not locate the current process in the proclist\n");
    goto error;
  }

  if (stat_set(B, dyn_stat, &bpi->bpi_vsize, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set the virtual memory size stat\n");
    goto error;
  }

  bk_procinfo_destroy(B, procinfo_list);
  procinfo_list = NULL;

  BK_RETURN(B, 0);

 error:
  if (procinfo_list)
    bk_procinfo_destroy(B, procinfo_list);
  BK_RETURN(B, -1);
}



/**
 * Register the virutal memory stat
 *
 *	@param B BAKA thread/global state.
 *	@param stats_list The stats list.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stat_resident_memory_register(bk_s B, bk_dynamic_stats_h stats_list, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  int locked = 0;
  bk_dynamic_stats_value_type_e vsize_type;
  struct bk_procinfo *bpi; // This is used only for the sizeof(bpi->vsize) operation

  if (!bdsl)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (sizeof(bpi->bpi_vsize) == sizeof(u_int32_t))
  {
    vsize_type = DynamicStatsValueTypeUInt32;
  }
  else
  {
    vsize_type = DynamicStatsValueTypeUInt64;
  }

  STATS_LIST_LOCK(bdsl, locked);

  if (bk_dynamic_stat_register(B, stats_list, BK_DYNAMIC_STAT_IGNORE_KEY_RSS_SZ, BK_DYNAMIC_STAT_NAME_KEY_RSS_SZ, BK_DYNAMIC_STAT_DEFAULT_NAME_RSS_SZ, NULL, 0, BK_DYNAMIC_STAT_PRIORITY_KEY_RSS_SZ, BK_DYNAMIC_STAT_DEFAULT_PRIORITY_RSS_SZ, NULL, vsize_type, DynamicStatsAccessTypeDirect, resident_memory_update, NULL, NULL, NULL, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not register RSS statistic\n");
    goto error;
  }

  STATS_LIST_UNLOCK(bdsl, locked);
  BK_RETURN(B, 0);

  error:
  STATS_LIST_UNLOCK(bdsl, locked);
  BK_RETURN(B, -1);
}




/**
 * Demand update function for resident memory size
 *
 *	@param B BAKA thread/global state.
 *	@param dyn_stat The elapsed time stat struct
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
resident_memory_update(bk_s B, bk_dynamic_stats_h stats_list, bk_dynamic_stat_h dyn_stat, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  struct bk_dynamic_stat *bds = (struct bk_dynamic_stat *)dyn_stat;
  dict_h procinfo_list;
  struct bk_procinfo *bpi = NULL;
  pid_t pid = getpid();

  if (!bdsl || !bds)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!(procinfo_list = bk_procinfo_create(B, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create procinfo struct for rss size\n");
    goto error;
  }

  for(bpi = procinfo_list_minimum(procinfo_list);
      bpi;
      bpi = procinfo_list_successor(procinfo_list, bpi))
  {
    if (bpi->bpi_pid == pid)
      break;
  }

  if (!bpi)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not locate the current process in the proclist\n");
    goto error;
  }

  if (stat_set(B, dyn_stat, &bpi->bpi_rss, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set the resident memory size stat\n");
    goto error;
  }

  bk_procinfo_destroy(B, procinfo_list);
  procinfo_list = NULL;

  BK_RETURN(B, 0);

 error:
  if (procinfo_list)
    bk_procinfo_destroy(B, procinfo_list);
  BK_RETURN(B, -1);
}



/**
 * Register the total cpu state
 *
 *	@param B BAKA thread/global state.
 *	@param stats_list The stats list.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stat_total_cpu_time_register(bk_s B, bk_dynamic_stats_h stats_list, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  int locked = 0;

  if (!bdsl)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  STATS_LIST_LOCK(bdsl, locked);

  if (bk_dynamic_stat_register(B, stats_list, BK_DYNAMIC_STAT_IGNORE_KEY_CPU_TIME, BK_DYNAMIC_STAT_NAME_KEY_CPU_TIME, BK_DYNAMIC_STAT_DEFAULT_NAME_CPU_TIME, NULL, 0, BK_DYNAMIC_STAT_PRIORITY_KEY_CPU_TIME, BK_DYNAMIC_STAT_DEFAULT_PRIORITY_CPU_TIME, NULL, DynamicStatsValueTypeFloat, DynamicStatsAccessTypeDirect, total_cpu_update, NULL, NULL, NULL, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not register total CPU statistic\n");
    goto error;
  }

  STATS_LIST_UNLOCK(bdsl, locked);
  BK_RETURN(B, 0);

 error:
  STATS_LIST_UNLOCK(bdsl, locked);
  BK_RETURN(B, -1);
}



/**
 * Demand update function for total_cpu
 *
 *	@param B BAKA thread/global state.
 *	@param stats_list The complete list of stats
 *	@param dyn_stat The elapsed time stat struct
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
total_cpu_update(bk_s B, bk_dynamic_stats_h stats_list, bk_dynamic_stat_h dyn_stat, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  struct bk_dynamic_stat *bds = (struct bk_dynamic_stat *)dyn_stat;
  struct timespec ts;
  float cpu_time;

  if (!bdsl || !bds)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) != 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not obtain the total CPU time: %s\n", strerror(errno));
    goto error;
  }

  cpu_time = BK_TS2F(&ts);

  if (stat_set(B, bds, &cpu_time, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set the total CPU time\n");
    goto error;
  }

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}



#ifdef BK_USING_PTHREADS
#ifndef NO_THREAD_CPU_STAT
/**
 * Register the per-thread CPU time. Unfortunately these cannot be
 * controlled by the conf file as easily as other stats for many reasons,
 * so for the moment we automatically include these. Perhaps at a later
 * date we can add a conf key that prevents any of these stats from being
 * registered and/or a conf key to set the priority (for all).
 *
 *	@param B BAKA thread/global state.
 *	@param stats_list The list of stats
 *	@param bt The thread node.
 *	@param bdsp C/O of newly created stat (if appropriate).
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
stats_thread_cpu_time_register(bk_s B, bk_dynamic_stats_h stats_list, void *bt, bk_dynamic_stat_h *statp, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  clockid_t *cpu_clockidp = NULL;
  char *name;
  int err;

  if (!bdsl || !bt || !statp)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  *statp = NULL;

  if (!(name = bk_string_alloc_sprintf(B, 0, 0, "%s CPU time", bk_threadnode_name(B, bt))))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create statistic name for per-thread CPU time: %s\n", bk_threadnode_name(B, bt));
    goto error;
  }

  /*
   * Since there can only be one CPU time stat per thread, the discriminator is superfluous.
   */
  if (stat_search(B, bdsl, name, 0))
  {
    // This stat is already registered. No work to be done.
    free(name);
    BK_RETURN(B, 0);
  }

  if (!(BK_MALLOC(cpu_clockidp)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not malloc: %s\n", strerror(errno));
    goto error;
  }

  if ((err = pthread_getcpuclockid(bk_threadnode_threadid(B, bt), cpu_clockidp)) != 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not get CPU clock id for a thread: %s: %s\n", name, strerror(err));
    goto error;
  }

  if (bk_dynamic_stat_register_simple(B, stats_list, name, 0, 0, DynamicStatsValueTypeFloat, DynamicStatsAccessTypeDirect, thread_cpu_time_update, cpu_clockidp, thread_cpu_time_destroy, statp, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not register CPU time for thread: %s\n", name);
    goto error;
  }

  free(name);
  name = NULL;

  cpu_clockidp = NULL;

  BK_RETURN(B, 0);

 error:
  if (cpu_clockidp)
    free(cpu_clockidp);

  if (name)
    free(name);
  BK_RETURN(B, -1);
}




/**
 * Destroy the private data of a per-thread CPU time statistic
 *
 *	@param B BAKA thread/global state.
 *	@param data The data to destroy
 */
static void
thread_cpu_time_destroy (bk_s B, void *data)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!data)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  free(data);
  BK_VRETURN(B);
}




/**
 * Update a per-thread CPU time statistic.
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
thread_cpu_time_update(bk_s B, bk_dynamic_stats_h stats_list, bk_dynamic_stat_h dyn_stat, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  struct bk_dynamic_stat *bds = (struct bk_dynamic_stat *)dyn_stat;
  struct timespec ts;
  float cpu_time;

  if (!bdsl || !bds)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (clock_gettime(*((clockid_t *)bds->bds_opaque), &ts) != 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not obtain the CPU time for thread: %s: %s\n", bds->bds_name, strerror(errno));
    goto error;
  }

  cpu_time = BK_TS2F(&ts);

  if (stat_set(B, bds, &cpu_time, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set the CPU time for thread: %s\n", bds->bds_name);
    goto error;
  }

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}



/**
 * Manage thread-specific stats.
 *
 *	@param B BAKA thread/global state.
 *	@param bdsl The stats list.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
manage_thread_stats(bk_s B, struct bk_dynamic_stats_list *bdsl, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int threadlist_locked = 0;
  pthread_t null_tid;
  void *bt;
  struct bk_dynamic_stat *bds;

  if (!bdsl)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  /*
   * Yes, I know that a pthread_t is currently an unsigned long, but
   * according the spec that's just an implementation detail that might
   * change in the future. Although the chance of such a change can
   * probably be charitably described as "God help them if they try", we
   * must accept that there is the slightest possibility that "they" will.
   */
  memset(&null_tid, (char)0, sizeof(null_tid));

  /*
   * Read all the thread CPU clocks and print their usage
   * <TODO> These should really be pure statistics which are updated as per normal</TODO>
   */
  if (bk_threadlist_lock(B) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not obtain thread list lock\n");
    goto error;
  }

  threadlist_locked = 1;

  for(bds = stats_list_minimum(bdsl->bdsl_list);
      bds;
      bds = stats_list_successor(bdsl->bdsl_list, bds))
  {
    int found_tid = 0;
    if (pthread_equal(bds->bds_tid, null_tid))
      continue;

    for(bt = bk_threadlist_minimum(B); bt; bt = bk_threadlist_successor(B, bt))
    {
      if (pthread_equal(bk_threadnode_threadid(B, bt), bds->bds_tid))
      {
	found_tid = 1;
	break;
      }
    }
    if (!found_tid)
      bk_dynamic_stat_deregister(B, bdsl, bds->bds_name, bds->bds_discriminator, 0);
  }

  for(bt = bk_threadlist_minimum(B); bt; bt = bk_threadlist_successor(B, bt))
  {
    // NB: This call only actually registers each thread once.
    if (stats_thread_cpu_time_register(B, bdsl, bt, (bk_dynamic_stat_h *)&bds, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_WARN, "Could not register CPU time stat for thread: %s\n", bk_threadnode_name(B, bt));
      continue;
    }

    if (bds && (bk_dynamic_stat_set_threadid(B, bds, bk_threadnode_threadid(B, bt), 0) < 0))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not set the threadid in thread CPU stat\n");
      goto error;
    }
  }

  /*
   * Unset threadlist_locked before attempting to unlock the thread list so
   * that if the unlock fails the error section won't try to unlock it
   * again.
   */
  threadlist_locked = 0;

  if (bk_threadlist_unlock(B))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not release thread list lock\n");
    goto error;
  }

  BK_RETURN(B, 0);

 error:
  if (threadlist_locked)
    bk_threadlist_unlock(B);

  BK_RETURN(B, -1);
}
#endif /* NO_THREAD_CPU_STAT */
#endif /* BK_USING_PTHREADS */



/**
 * Some processes which use the indirect-update method, also want to
 * destroy that memory while still maintaining valid statistic memory. In
 * this case, the process can use this function to convert from indirect to
 * direct.
 *
 *	@param B BAKA thread/global state.
 *	@param stats_list The statistics list
 *	@param name The name/description of the stat
 *	@param discriminator Distinguish same-named stats.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stat_convert(bk_s B, bk_dynamic_stats_h stats_list, const char *name, long discriminator, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stats_list *bdsl = (struct bk_dynamic_stats_list *)stats_list;
  struct bk_dynamic_stat *bds;
  void *tptr;

  if (!bdsl || !name)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!(bds = stat_search(B, bdsl, name, discriminator)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not find the stat described as %s:%ld\n", name, discriminator);
    goto error;
  }

  if (bds->bds_access_type == DynamicStatsAccessTypeDirect)
  {
    bk_error_printf(B, BK_ERR_ERR, "Conversion only works for stats using the indirect access method\n");
    goto error;
  }

  tptr = bds->bds_ptr;
  bds->bds_access_type = DynamicStatsAccessTypeDirect;

  switch(bds->bds_value_type)
  {
  case DynamicStatsValueTypeInt32:
    bds->bds_int32 = *(int32_t *)tptr;
    break;
  case DynamicStatsValueTypeUInt32:
    bds->bds_uint32 = *(u_int32_t *)tptr;
    break;
  case DynamicStatsValueTypeInt64:
    bds->bds_int64 = *(int64_t *)tptr;
    break;
  case DynamicStatsValueTypeUInt64:
    bds->bds_uint64 = *(u_int64_t *)tptr;
    break;
  case DynamicStatsValueTypeFloat:
    bds->bds_float = *(float *)tptr;
    break;
  case DynamicStatsValueTypeDouble:
    bds->bds_double = *(double *)tptr;
    break;
  case DynamicStatsValueTypeString:
    bk_error_printf(B, BK_ERR_ERR, "Converting a string value type should never be possible\n");
    goto error;
    break;
  default:
    bk_error_printf(B, BK_ERR_ERR,"Unknown bds->bds_value_type: %d\n", bds->bds_value_type);
    break;
  }

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}




/**
 * Search for stat.
 *
 *	@param B BAKA thread/global state.
 *	@param bdsl The stats list to search
 *	@param name Stat name
 *	@param discriminator Distinguish same-named stats.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>bds</i> on success.
 */
static struct bk_dynamic_stat *
stat_search(bk_s B, struct bk_dynamic_stats_list *bdsl, const char *name, long discriminator)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stat_key bdsk;

  if (!bdsl || !name)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  bdsk.bdsk_name = name;
  bdsk.bdsk_discriminator = discriminator;

  BK_RETURN(B, stats_list_search(bdsl->bdsl_list, &bdsk));

}

static int stats_list_oo_cmp(struct bk_dynamic_stat *a, struct bk_dynamic_stat *b)
{
  return(strcmp(a->bds_name,b->bds_name) || (a->bds_discriminator != b->bds_discriminator));
}

static int stats_list_ko_cmp(struct bk_dynamic_stat_key *a, struct bk_dynamic_stat *b)
{
  return(strcmp(a->bdsk_name,b->bds_name) || (a->bdsk_discriminator != b->bds_discriminator));
}


/**
 * Print a stat node. Intended for use with bst.c debugging (ergo no
 * baka'isms and the assumption that stderr is ours to print to).
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 */
void
bk_dynamic_stat_bst_print(dict_obj dyn_stat)
{
  struct bk_dynamic_stat *bds = (struct bk_dynamic_stat *)dyn_stat;

  if (!bds)
  {
    fprintf(stderr, "bk_dynamic_stat_bst_print: Illegal arguments\n");
    return;
  }

  fprintf(stderr, "%s:%ld\n", bds->bds_name, bds->bds_discriminator);
  return;
}


#ifdef  BK_USING_PTHREADS
/**
 * Set the thread id. The reason this is a separate function and not just
 * another argument to the various constructor functions is simply that
 * this makes it easier to compile *without* BK_USING_PTHREADS
 *
 *	@param B BAKA thread/global state.
 *	@param dstat The statistic to update
 *	@param tid The thread id.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_dynamic_stat_set_threadid(bk_s B, bk_dynamic_stat_h dstat, pthread_t tid, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_dynamic_stat *bds = (struct bk_dynamic_stat *)dstat;

  if (!bds)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  bds->bds_tid = tid;

  BK_RETURN(B, 0);
}

#else /* BK_USING_PTHREADS */
int
bk_dynamic_stat_set_threadid(bk_s B, bk_dynamic_stat_h dstat, u_long tid, bk_flags flags)
{
  return(0);
}
#endif /* BK_USING_PTHREADS */
