#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_config.c,v 1.2 2001/07/09 07:08:17 jtt Exp $";
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

static int kv_oo_cmp(struct bk_config_key *bck1, struct bk_config_key *bck2);
static int kv_ko_cmp(char *a, struct bk_config_key *bck2);
static ht_val kv_obj_hash(struct bk_config_key *bck);
static ht_val kv_key_hash(char *a);
static int load_config_from_file(bk_s B, char *filename);


struct bk_config_fileinfo
{
  bk_flags	       		bcf_flags;	/* Everyone needs flags */
  char *			bcf_filename;	/* File of data */
  u_int				bcf_lineno;	/* Lineno where filename is*/
  dict_h			bcf_includes;	/* Included files */
  struct bk_config_fileinfo *	bcf_insideof;	/* What file am I inside of */
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


static int kv_oo_cmp(struct bk_config_key *bck1, struct bk_config_key *bck2)
{
  return(strcmp(bck1->bck_key, bck2->bck_key));
} 

static int kv_ko_cmp(char *a, struct bk_config_key *bck2)
{
  return(strcmp(a, bck2->bck_key));
} 

static ht_val kv_obj_hash(struct bk_config_key *bck)
{
  return(bk_strhash(bck->bck_key));
}

static ht_val kv_key_hash(char *a)
{
  return(bk_strhash(a));
}

static struct ht_args kv_args = { 512, 1, kv_obj_hash, kv_key_hash };


#define config_kv_create(o,k,f,a)	ht_create((o),(k),(f),(a))
#define config_kv_destroy(h)		ht_destroy(h)
#define config_kv_insert(h,o)		ht_insert((h),(o))
#define config_kv_insert_uniq(h,n,o)	ht_insert((h),(n),(o))
#define config_kv_search(h,k)		ht_search((h),(k))
#define config_kv_delete(h,o)		ht_delete((h),(o))
#define config_kv_minimum(h)		ht_minimum(h)
#define config_kv_maximum(h)		ht_maximum(h)
#define config_kv_successor(h,o)	ht_successor((h),(o))
#define config_kv_predecessor(h,o)	ht_predecessor((h),(o))
#define config_kv_iterate(h,d)		ht_iterate((h),(d))
#define config_kv_nextobj(h)		ht_nextobj(h)
#define config_kv_error_reason(h,i)	ht_error_reason((h),(i))



/*
 * Initialize the config stuff
 */
int
bk_config_init(bk_s B, char *filename, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_config *bc=NULL;
  struct bk_config_fileinfo *bcf=NULL;
  int ret=0;
  

  if (!filename)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  /* gc++ sucks */
  if (!(bc=(struct bk_config *)malloc(sizeof(*bc))))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bc: %s\n", strerror(errno));
    BK_RETURN(B, -1);
  }
  memset(bc,0,sizeof(*bc));

  /* 
   * Must link this in to B as early as possible for bk_config_destroy to work
   * correctly
   */
  BK_GENERAL_CONFIG(B)=bc;  

  /* Create kv clc */
  if (!(bc->bc_kv=config_kv_create(kv_oo_cmp, kv_ko_cmp, DICT_UNORDERED, &kv_args)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create config values clc\n");
    ret=-1;
    goto done;
  }

  if (load_config_from_file(B,filename) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not load config from file: %s\n", filename);
    ret=-1;
    goto done;
  }
  

 done:
  if (ret != 0)
  {
    bk_config_destroy(B);
  }

  BK_RETURN(B, ret);
}



/*
 * Destroy a config structure
 */
void
bk_config_destroy(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_config *bc;

  if (!(bc=BK_GENERAL_CONFIG(B)))
  {
    bk_error_printf(B, BK_ERR_WARN, "Trying to destroy a NULL config struct\n");
    BK_VRETURN(B);
  }

  BK_GENERAL_CONFIG(B)=NULL;

  free(bc);

  BK_VRETURN(B);
}



static int
load_config_from_file(bk_s B, char *filename)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  BK_RETURN(B, 0);
}
