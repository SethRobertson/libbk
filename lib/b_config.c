#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_config.c,v 1.1 2001/07/08 05:08:32 jtt Exp $";
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

#include <libbk.h>
#include "libbk_internal.h"

struct bk_config_fileinfo
{
  bk_flags	       		bcf_flags;	/* Everyone needs flags */
  char *			bcf_filename;	/* File of data */
  u_int				bcf_lineno;	/* Lineno where filename is*/
  dict_h			bcf_includes;	/* Included files */
  struct bk_config_fileinfo *	bcf_insideof;	/* What file am I inside of */
};



struct bk_config
{
  bk_flags			bc_flags;	/* Everyone needs flags */
  struct bk_config_fileinfo *	bc_files;	/* Files of conf data */
  dict_h			bc_values;	/* Hash of value dlls */
};



struct bk_config_key
{
  char *			bck_key;	/* Key string */
  bk_flags			bck_flags;	/* Everyone needs flags */
  dict_h			bck_values;	/* dll of values of this key */
};



struct bk_config_value
{
  char *			bcv_value;	/* Value string */
  bk_flags			bcv_flags;	/* Everyone needs flags */
  u_int				bcv_lineno;	/* Where value is in file */
};
