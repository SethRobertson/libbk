#if !defined(lint)
static const char libbk__copyright[] __attribute__((unused)) = "Copyright © 2003-2019";
static const char libbk__contact[] __attribute__((unused)) = "<projectbaka@baka.org>";
#endif /* not lint */
/*
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
#define dllvault_create(o,k,f)		dll_create((o),(k),(f))
#define bstvault_create(o,k,f)		bst_create((o),(k),(f))
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
 *	@param flags Additional CLC flags to pass in
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

  if (!(vault = vault_create(vault_oo_cmp, vault_ko_cmp, DICT_HT_STRICT_HINTS|DICT_UNIQUE_KEYS|flags, &vault_args)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create vault CLC: %s\n", bk_vault_error_reason(NULL, NULL));
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B, vault);
}




/**
 * Create vault
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state
 *	@param flags  Additional CLC creation flags
 *	@return <i>NULL</i> on call failure, allocation failure, etc
 *	@return <br><i>allocated vault</i> on success.
 */
bk_bstvault_t bk_bstvault_create(bk_s B, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  bk_bstvault_t vault = NULL;

  if (!(vault = bstvault_create(vault_oo_cmp, vault_ko_cmp, DICT_UNIQUE_KEYS|DICT_BALANCED_TREE|flags)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create vault CLC: %s\n", bk_bstvault_error_reason(NULL, NULL));
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B, vault);
}




/**
 * Create vault
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA thread/global state
 *	@param flags Additional CLC creation flags
 *	@return <i>NULL</i> on call failure, allocation failure, etc
 *	@return <br><i>allocated vault</i> on success.
 */
bk_dllvault_t bk_dllvault_create(bk_s B, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  bk_dllvault_t vault = NULL;

  if (!(vault = dllvault_create(vault_oo_cmp, vault_ko_cmp, DICT_ORDERED|DICT_UNIQUE_KEYS|flags)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create vault CLC: %s\n", bk_vault_error_reason(NULL, NULL));
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B, vault);
}



/**
 * Insert a pointer into the vault indexed by string.  The "string" is donated
 * to the vault and will only be returned if you use the non-helper routines.
 *
 *	@param V String vault
 *	@param string Donated string/key
 *	@param ptr Pointer to value
 *	@return <i>0</i> on success
 *	@return <i>-1</i> on failure
 */
int bk_dllvault_inserthelp(bk_vault_t V, const char *string, void *ptr)
{
  struct bk_vault_node *node = NULL;

  if (!V || !string)
    return(-1);

  if (!(node = malloc(sizeof(*node))))
    return(-1);

  node->key = string;
  node->value = ptr;

  if (bk_dllvault_insert(V, node) != DICT_OK)
  {
    free(node);
    return(-1);
  }
  return(0);
}



/**
 * Insert a pointer into the vault indexed by string.  The "string" is donated
 * to the vault and will only be returned if you use the non-helper routines.
 *
 *	@param V String vault
 *	@param string Donated string/key
 *	@param ptr Pointer to value
 *	@return <i>0</i> on success
 *	@return <i>-1</i> on failure
 */
int bk_bstvault_inserthelp(bk_vault_t V, const char *string, void *ptr)
{
  struct bk_vault_node *node = NULL;

  if (!V || !string)
    return(-1);

  if (!(node = malloc(sizeof(*node))))
    return(-1);

  node->key = string;
  node->value = ptr;

  if (bk_bstvault_insert(V, node) != DICT_OK)
  {
    free(node);
    return(-1);
  }
  return(0);
}



/**
 * Insert a pointer into the vault indexed by string.  The "string" is donated
 * to the vault and will only be returned if you use the non-helper routines.
 *
 *	@param V String vault
 *	@param string Donated string/key
 *	@param ptr Pointer to value
 *	@return <i>0</i> on success
 *	@return <i>-1</i> on failure
 */
int bk_vault_inserthelp(bk_vault_t V, const char *string, void *ptr)
{
  struct bk_vault_node *node = NULL;

  if (!V || !string)
    return(-1);

  if (!(node = malloc(sizeof(*node))))
    return(-1);

  node->key = string;
  node->value = ptr;

  if (bk_vault_insert(V, node) != DICT_OK)
  {
    free(node);
    return(-1);
  }
  return(0);
}



/**
 * Search for a key, returning the value
 *
 *	@param V String vault
 *	@param string String/key
 *	@return <i>value</i> on success
 *	@return <i>NULL</i> on failure
 */
void *bk_vault_searchhelp(bk_vault_t V, const char *string)
{
  struct bk_vault_node *node = NULL;

  if (!V || !string)
    return(NULL);

  if (!(node = bk_vault_search(V, (dict_key)string)))
    return(NULL);

  return(node->value);
}



/**
 * Search for a key, returning the value
 *
 *	@param V String vault
 *	@param string String/key
 *	@return <i>value</i> on success
 *	@return <i>NULL</i> on failure
 */
void *bk_bstvault_searchhelp(bk_vault_t V, const char *string)
{
  struct bk_vault_node *node = NULL;

  if (!V || !string)
    return(NULL);

  if (!(node = bk_bstvault_search(V, (dict_key)string)))
    return(NULL);

  return(node->value);
}



/**
 * Search for a key, returning the value
 *
 *	@param V String vault
 *	@param string String/key
 *	@return <i>value</i> on success
 *	@return <i>NULL</i> on failure
 */
void *bk_dllvault_searchhelp(bk_vault_t V, const char *string)
{
  struct bk_vault_node *node = NULL;

  if (!V || !string)
    return(NULL);

  if (!(node = bk_dllvault_search(V, (dict_key)string)))
    return(NULL);

  return(node->value);
}



/**
 * Delete the vault node for a key
 *
 *	@param V String vault
 *	@param string Key
 *	@return <i>DICT_OK</i> on success
 *	@return <i>DICT_ERR</i> on failure
 */
int bk_vault_deletehelp(bk_vault_t V, const char *string)
{
  struct bk_vault_node *node = NULL;

  if (!V || !string)
    return(DICT_ERR);

  if (!(node = bk_vault_search(V, (dict_key)string)))
    return(DICT_ERR);

  return(bk_vault_delete(V, node));
}



/**
 * Delete the vault node for a key
 *
 *	@param V String vault
 *	@param string Key
 *	@return <i>DICT_OK</i> on success
 *	@return <i>DICT_ERR</i> on failure
 */
int bk_bstvault_deletehelp(bk_vault_t V, const char *string)
{
  struct bk_vault_node *node = NULL;

  if (!V || !string)
    return(DICT_ERR);

  if (!(node = bk_bstvault_search(V, (dict_key)string)))
    return(DICT_ERR);

  return(bk_bstvault_delete(V, node));
}



/**
 * Delete the vault node for a key
 *
 *	@param V String vault
 *	@param string Key
 *	@return <i>DICT_OK</i> on success
 *	@return <i>DICT_ERR</i> on failure
 */
int bk_dllvault_deletehelp(bk_vault_t V, const char *string)
{
  struct bk_vault_node *node = NULL;

  if (!V || !string)
    return(DICT_ERR);

  if (!(node = bk_dllvault_search(V, (dict_key)string)))
    return(DICT_ERR);

  return(bk_dllvault_delete(V, node));
}



/**
 * Destroy the vault
 *
 *	@param V String vault
 *	@return <i>DICT_OK</i> on success
 *	@return <i>DICT_ERR</i> on failure
 */
int bk_vault_destroyhelp(bk_vault_t V)
{
  struct bk_vault_node *node = NULL;

  if (!V)
    return(DICT_ERR);

  DICT_NUKE(V, ht, node, return(DICT_ERR), free((void *)node->key); free(node->value); free(node));
  return(DICT_OK);
}



/**
 * Destroy the vault
 *
 *	@param V String vault
 *	@return <i>DICT_OK</i> on success
 *	@return <i>DICT_ERR</i> on failure
 */
int bk_bstvault_destroyhelp(bk_vault_t V)
{
  struct bk_vault_node *node = NULL;

  if (!V)
    return(DICT_ERR);

  DICT_NUKE(V, bst, node, return(DICT_ERR), free((void *)node->key); free(node->value); free(node));
  return(DICT_OK);
}



/**
 * Destroy the vault
 *
 *	@param V String vault
 *	@return <i>DICT_OK</i> on success
 *	@return <i>DICT_ERR</i> on failure
 */
int bk_dllvault_destroyhelp(bk_vault_t V)
{
  struct bk_vault_node *node = NULL;

  if (!V)
    return(DICT_ERR);

  DICT_NUKE(V, dll, node, return(DICT_ERR), free((void *)node->key); free(node->value); free(node));
  return(DICT_OK);
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
