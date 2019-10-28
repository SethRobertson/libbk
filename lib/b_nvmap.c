#if !defined(lint)
static const char libbk__copyright[] __attribute__((unused)) = "Copyright © 2002-2019";
static const char libbk__contact[] __attribute__((unused)) = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2002-2019 The Authors. All rights reserved.
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
 * Function for conveniently dealing mapping string names to fixed values.
 */

#include <libbk.h>
#include "libbk_internal.h"


/**
 * Convert the name to a value.
 *
 * THREADS: MT-SAFE (assuming different or const nvmap)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param name The name to convert
 *	@return <i>-1</i> on failure.<br>
 *	@return value on success.
 */
int
bk_nvmap_name2value(bk_s B, struct bk_name_value_map *nvmap, const char *name)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_name_value_map *n;

  if (!nvmap || !name)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  for(n = nvmap; n->bnvm_name; n++)
  {
    if (BK_STREQ(n->bnvm_name, name))
    {
      BK_RETURN(B,n->bnvm_val);
    }
  }
  BK_RETURN(B,-1);
}




/**
 * Convert a value to a name.
 *
 * THREADS: MT-SAFE (assuming different or const nvmap)
 * THREADS: REENTRANT (otherwise)
 *
 *	@param B BAKA thread/global state.
 *	@param val The value to match.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>const string</i> on success.
 */
const char *
bk_nvmap_value2name(bk_s B, struct bk_name_value_map *nvmap, int val)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_name_value_map *n;

  if (!nvmap)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  for(n = nvmap; n->bnvm_name; n++)
  {
    if (n->bnvm_val == val)
    {
      BK_RETURN(B,n->bnvm_name);
    }
  }
  BK_RETURN(B,NULL);
}




/**
 * Convert a string of flags to a singal value based on the a nvmap. @a
 * output_value is a copy out so that the function can return a useful
 * exit code.
 *
 *	@param B BAKA thread/global state.
 *	@param nvmap The name/value map to use
 *	@param flags_str The string of human-readable flags
 *	@param separator The separator between the human-readable flags
 *	@param output_flags The output value.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_flags2value(bk_s B, struct bk_name_value_map *nvmap, const char *flags_str, const char *separator, bk_flags *output_flagsp, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char **tokenp;
  char **tokens = NULL;
  bk_flags output_flags = 0;

  if (!nvmap || !flags_str || !output_flagsp)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  *output_flagsp = 0;

  if (!separator)
    separator = ",";

  if (!(tokens = bk_string_tokenize_split(B, flags_str, 0, separator, NULL, NULL, NULL, 0)) <  0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not tokenize flags string: %s (using '%s' as the separator)\n", separator, flags_str);
    goto error;

  }

  for(tokenp = tokens; *tokenp; tokenp++)
  {
    int new_flag;
    if ((new_flag = bk_nvmap_name2value(B, nvmap, *tokenp)) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not translate to a flag value: %s\n", *tokenp);
      goto error;
    }
    BK_FLAG_SET(output_flags, (bk_flags)new_flag);
  }

  bk_string_tokenize_destroy(B, tokens);
  tokens = NULL;

  *output_flagsp = output_flags;

  BK_RETURN(B, 0);

 error:
  if (tokens)
    bk_string_tokenize_destroy(B, tokens);

  BK_RETURN(B, -1);
}



/**
 * Convert a flags value into a singal string of human-reable strings
 * separated by @a separator. CALLER MUST FREE.
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
char *
bk_value2flags(bk_s B, struct bk_name_value_map *nvmap, bk_flags input_flags, const char *separator, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *output_string = NULL;
  char *tmp;
  struct bk_name_value_map *bnvm;

  if (!nvmap)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!separator)
    separator = ",";

  for(bnvm = nvmap; bnvm->bnvm_name; bnvm++)
  {
    if (BK_FLAG_ISSET(input_flags, bnvm->bnvm_val))
    {
      if (!(tmp = bk_string_alloc_sprintf(B, 0, 0, "%s%s%s", output_string?output_string:"", separator, bnvm->bnvm_name)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not update output string\n");
	goto error;
      }
    }
    else
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not convert flag %u to a string\n", (u_int)bnvm->bnvm_val);
      goto error;
    }
    if (output_string)
      free(output_string);
    output_string = tmp;
  }

  BK_RETURN(B, output_string);

 error:
  if (output_string)
    free(output_string);

  BK_RETURN(B, NULL);
}
