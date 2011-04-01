#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2003-2011";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2003-2011 The Authors. All rights reserved.
 *
 * This source code is licensed to you under the terms of the file
 * LICENSE.TXT in this release for further details.
 *
 * Send e-mail to <projectbaka@baka.org> for further information.
 *
 * - -Copyright BAKA- -
 */

#include <libbk.h>
#include "libbk_internal.h"


/**
 * @file
 * Vault for storage of values by string keys (note a great deal of functionality
 * is provided by #define wrappers in libbk.h)
 */



/**
 * @name Defines: vault_kv_clc
 * Key-value database CLC definitions
 * to hide CLC choice.
 */
// @{
#define vault_create(o,k,f,a)		ht_create((o),(k),(f),(a))
static int vault_oo_cmp(void *bck1, void *bck2);
static int vault_ko_cmp(void *a, void *bck2);
static ht_val vault_obj_hash(void *bck);
static ht_val vault_key_hash(void *a);
// @}





/**
 * Create vault
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state
 *	@param table_entries Optional number of entries in CLC table (0 for default)
 *	@param bucket_entries Optional number of entries in each bucket (0 for default)
 *	@param flags fun for the future
 *	@return <i>NULL</i> on call failure, allocation failure, etc
 *	@return <br><i>allocated vault</i> on success.
 */
bk_vault_t bk_vault_create(bk_s B, int table_entries, int bucket_entries, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  bk_vault_t vault = NULL;
  struct ht_args vault_args = { 512, 1, vault_obj_hash, vault_key_hash };

  if (table_entries)
    vault_args.ht_table_entries = table_entries;

  if (bucket_entries)
    vault_args.ht_bucket_entries = bucket_entries;

  if (!(vault = vault_create(vault_oo_cmp, vault_ko_cmp, DICT_HT_STRICT_HINTS|DICT_UNIQUE_KEYS, &vault_args)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create vault CLC: %s\n", bk_vault_error_reason(NULL, NULL));
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B, vault);
}



/**
 * CLC functions for vault
 */
// @{
static int vault_oo_cmp(void *bck1, void *bck2)
{
  struct bk_vault_node *a = bck1, *b = bck2;
  return(strcmp(a->key,b->key));
}
static int vault_ko_cmp(void *a, void *bck2)
{
  struct bk_vault_node *b = bck2;
  return(strcmp(a,b->key));
}
static ht_val vault_obj_hash(void *bck)
{
  struct bk_vault_node *a = bck;
  return(bk_strhash(a->key, 0));
}
static ht_val vault_key_hash(void *a)
{
  return(bk_strhash(a, 0));
}
// @}
