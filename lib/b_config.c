#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_config.c,v 1.14 2001/11/05 20:53:06 seth Exp $";
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


#define SET_CONFIG(b,B,c) do { if (!(c)) { (b)=BK_GENERAL_CONFIG(B); } else { (b)=(c); } } while (0)
#define LINELEN 1024
#define CONFIG_MANAGE_FLAG_NEW_KEY	0x1
#define CONFIG_MANAGE_FLAG_NEW_VALUE	0x2



/*
 * General configuration structure
 */
struct bk_config
{
  bk_flags			bc_flags;	/* Everyone needs flags */
  struct bk_config_fileinfo *	bc_bcf;		/* Files of conf data */
  dict_h			bc_kv;		/* Hash of value dlls */
  int				bc_kv_error;	/* clc errno for bc_kv */
};



/*
 * Information about the files parsed when creating this configuration group
 */
struct bk_config_fileinfo
{
  bk_flags	       		bcf_flags;	/* Everyone needs flags */
  char *			bcf_filename;	/* File of data */
  dict_h			bcf_includes;	/* Included files */
  struct bk_config_fileinfo *	bcf_insideof;	/* What file am I inside of */
};



/*
 * Information about a specific key found when parsing the configuration file
 */
struct bk_config_key
{
  char *			bck_key;	/* Key string */
  bk_flags			bck_flags;	/* Everyone needs flags */
  dict_h			bck_values;	/* dll of values of this key */
};



/*
 * A individual value for a key.
 *
 * XXX - Doesn't this need a bk_config_fileinfo ptr as well, to
 * give context to the lineno?  Or is the lineno monotomically increasing?
 */
struct bk_config_value
{
  char *			bcv_value;	/* Value string */
  bk_flags			bcv_flags;	/* Everyone needs flags */
  u_int				bcv_lineno;	/* Where value is in file */
};



#define config_kv_create(o,k,f,a)	ht_create((o),(k),(f),(a))
#define config_kv_destroy(h)		ht_destroy(h)
#define config_kv_insert(h,o)		ht_insert((h),(o))
#define config_kv_insert_uniq(h,n,o)	ht_insert_uniq((h),(n),(o))
#define config_kv_append(h,o)		ht_append((h),(o))
#define config_kv_append_uniq(h,n,o)	ht_append_uniq((h),(n),(o))
#define config_kv_search(h,k)		ht_search((h),(k))
#define config_kv_delete(h,o)		ht_delete((h),(o))
#define config_kv_minimum(h)		ht_minimum(h)
#define config_kv_maximum(h)		ht_maximum(h)
#define config_kv_successor(h,o)	ht_successor((h),(o))
#define config_kv_predecessor(h,o)	ht_predecessor((h),(o))
#define config_kv_iterate(h,d)		ht_iterate((h),(d))
#define config_kv_nextobj(h,i)		ht_nextobj((h),(i))
#define config_kv_iterate_done(h,i)	ht_iterate_done((h),(i))
#define config_kv_error_reason(h,i)	ht_error_reason((h),(i))

static int kv_oo_cmp(void *bck1, void *bck2);
static int kv_ko_cmp(void *a, void *bck2);
static ht_val kv_obj_hash(void *bck);
static ht_val kv_key_hash(void *a);
static struct ht_args kv_args = { 512, 1, kv_obj_hash, kv_key_hash };


#define config_values_create(o,k,f)		dll_create((o),(k),(f))
#define config_values_destroy(h)		dll_destroy(h)
#define config_values_insert(h,o)		dll_insert((h),(o))
#define config_values_insert_uniq(h,n,o)	dll_insert_uniq((h),(n),(o))
#define config_values_append(h,o)		dll_append((h),(o))
#define config_values_append_uniq(h,n,o)	dll_append_uniq((h),(n),(o))
#define config_values_search(h,k)		dll_search((h),(k))
#define config_values_delete(h,o)		dll_delete((h),(o))
#define config_values_minimum(h)		dll_minimum(h)
#define config_values_maximum(h)		dll_maximum(h)
#define config_values_successor(h,o)		dll_successor((h),(o))
#define config_values_predecessor(h,o)		dll_predecessor((h),(o))
#define config_values_iterate(h,d)		dll_iterate((h),(d))
#define config_values_nextobj(h,i)		dll_nextobj((h),(i))
#define config_values_iterate_done(h,i)		dll_iterate_done((h),(i))
#define config_values_error_reason(h,i)		dll_error_reason((h),(i))

static int bcv_oo_cmp(void *bck1, void *bck2);
static int bcv_ko_cmp(void *a, void *bck2);


static struct bk_config_fileinfo *bcf_create(bk_s B, const char *filename, struct bk_config_fileinfo *obcf);
static void bcf_destroy(bk_s B, struct bk_config_fileinfo *bcf);
static int load_config_from_file(bk_s B, struct bk_config *bc, struct bk_config_fileinfo *bcf);
static struct bk_config_key *bck_create(bk_s B, const char *key, bk_flags flags);
static void bck_destroy(bk_s B, struct bk_config_key *bck);
static struct bk_config_value *bcv_create(bk_s B, const char *value, u_int lineno, bk_flags flags);
static void bcv_destroy(bk_s B, struct bk_config_value *bcv);
static int config_manage(bk_s B, struct bk_config *bc, const char *key, const char *value, const char *ovalue, u_int lineno);



/*
 * Initialize the config stuff
 */
struct bk_config *
bk_config_init(bk_s B, const char *filename, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_config *bc=NULL;
  struct bk_config_fileinfo *bcf=NULL;
  int ret=0;

  if (!filename)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }
  
  BK_MALLOC(bc);

  if (!bc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bc: %s\n", strerror(errno));
    BK_RETURN(B, NULL);
  }

  /* Create kv clc */
  if (!(bc->bc_kv=config_kv_create(kv_oo_cmp, kv_ko_cmp, DICT_UNORDERED, &kv_args)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create config values clc\n");
    ret=-1;
    goto done;
  }

  if (!(bcf=bcf_create(B, filename, NULL)))
  {
    bk_error_printf(B, BK_ERR_WARN, "Could not create fileinfo entry for %s\n", filename);
    ret=-1;
    goto done;
  }

  bc->bc_bcf=bcf;

  if (load_config_from_file(B, bc, bcf) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not load config from file: %s\n", filename);
    ret=1;			/* non-fatal error */
    goto done;
  }
  

 done:
  if (ret < 0)			/* if fatal error */
  {
    if (bc) bk_config_destroy(B, bc);
    bc=NULL;
  }

  BK_RETURN(B,bc);
}



/*
 * Destroy a config structure
 */
void
bk_config_destroy(bk_s B, struct bk_config *obc)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_config *bc;

  SET_CONFIG(bc,B,obc);

  if (!bc)
  {
    bk_error_printf(B, BK_ERR_WARN, "Trying to destroy a NULL config struct\n");
    BK_VRETURN(B);
  }

  if (bc->bc_kv)
  {
    struct bk_config_key *bck;
    while((bck=config_kv_minimum(bc->bc_kv)))
    {
      config_kv_delete(bc->bc_kv,bck);
      bck_destroy(B,bck);
    }
    config_kv_destroy(bc->bc_kv);
  }

  /* 
   * Do this before free(3) so that Insight (et al) will not complain about
   * "reference after free"
   */
  if (BK_GENERAL_CONFIG(B)==bc)
  {
    BK_GENERAL_CONFIG(B)=NULL;
  }

  free(bc);

  BK_VRETURN(B);
}



/*
 * Load up the indicated config file from the indicated filename
 */
static int
load_config_from_file(bk_s B, struct bk_config *bc, struct bk_config_fileinfo *bcf)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  FILE *fp=NULL;
  int ret=0;
  char line[LINELEN];
  int lineno=0;

  if (!bc || !bcf)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!(fp=fopen(bcf->bcf_filename, "r")))
  {
    bk_error_printf(B, BK_ERR_WARN, "Could not open %s: %s\n", bcf->bcf_filename, strerror(errno));
    ret=-1; 
    goto done;
  }

  while(fgets(line, LINELEN, fp))
  {
    char *key=line, *value;
    struct bk_config_key *bck=NULL;
    struct bk_config_value *bcv=NULL;

    lineno++;

    bk_string_rip(B, line, NULL, 0);		/* Nuke trailing CRLF */

    if (BK_STREQ(line,""))
    {
      /* 
       * Blank lines are perfectly OK -- as are lines with only white space
       * but we'll just issue the warning below for those.
       */
      continue;
    }

    /* 
     * The separator for the key value is ONE SPACE (ie not any span of 
     * white space). This way the value *may* contain leading white space if
     * the user wishes
     */
    if (!(value=strchr(line,' ')))
    {
      bk_error_printf(B, BK_ERR_WARN, "Malformed config line in file %s at line %d\n", bcf->bcf_filename, lineno);
      continue;
    }
    
    *value++='\0'; /* "line" now points at the key and "value" at the value */
    
    if (config_manage(B, bc, line, value, NULL, lineno)<0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not add %s ==> %s to DB\n", key, value);
      ret=-1;
      goto done;
    }
  }

  if (ferror(fp))
  {
    bk_error_printf(B, BK_ERR_ERR, "Error reading %s: %s\n", bcf->bcf_filename, strerror(errno));
    ret=-1;
    goto done;
  }

 done:
  if (fp) fclose(fp);

  BK_RETURN(B, ret);

}



/*
 * Internal management function.
 */
static int
config_manage(bk_s B, struct bk_config *bc, const char *key, const char *value, const char *ovalue, u_int lineno)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_config_key *bck=NULL;
  struct bk_config_value *bcv=NULL;
  int flags=0;

  if (!bc || !key || (!value && !ovalue))
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  if (!(bck=config_kv_search(bc->bc_kv,(char *)key)))
  {
    if (!(bck=bck_create(B, key,0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create key struct for %s\n", key);
      goto error;
    }

    BK_FLAG_SET(flags, CONFIG_MANAGE_FLAG_NEW_KEY);

    if (config_kv_insert(bc->bc_kv, bck) != DICT_OK)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not insert key %s into config clc (continuing)\n", key);
      goto error;
    }
  }

  if (!ovalue)
  {
    /* Add a new value to key */
    if (!(bcv=bcv_create(B, value, lineno, 0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create value struct for %s (continuing)\n", value);
      goto error;
    }

    BK_FLAG_SET(flags, CONFIG_MANAGE_FLAG_NEW_VALUE);

    if (config_values_append(bck->bck_values,bcv) != DICT_OK)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not insert %s in values clc\n", value);
      goto error;
    }
  }
  else
  {
    if (!(bcv=config_values_search(bck->bck_values, (char *)ovalue)))
    {
      /* 
       * jtt thinks failure to locate something you want to delete is just
       * a warning and *not* and erorr.
       */
	 
      bk_error_printf(B, BK_ERR_WARN, "Could not locate value: %s in key: %s\n", value, key);
      goto error;
    }

    if (value)
    {
      /* Replace ovalue with value. If strdup fails restore ovalue */
      char *hold;
      hold=bcv->bcv_value;
      if (!(bcv->bcv_value=strdup(value)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not strdup new value: %s (restoring %s)\n", value, ovalue);
	bcv->bcv_value=hold;
      }
      else
      {
	/* OK to free now */
	if (hold) free (hold); /* hold should *always* be nonnull, but check*/
      }
    }
    else
    {
      /* Delete value */
      config_values_delete(bck->bck_values, bcv);
      bcv_destroy(B, bcv);
      if (!(config_values_minimum(bck->bck_values)))
      {
	/* That was the last value in the key. Get rid the key */
	config_kv_delete(bc->bc_kv, bck);
	bck_destroy(B, bck);
      }
    }
  }

  BK_RETURN(B, 0);

 error:
  /* Destroy the value first to make sure you don't double free */
  if (BK_FLAG_ISSET(flags, CONFIG_MANAGE_FLAG_NEW_VALUE) && bcv)
  {
    config_values_delete(bck->bck_values, bcv);
    bcv_destroy(B, bcv);
  }

  if (BK_FLAG_ISSET(flags, CONFIG_MANAGE_FLAG_NEW_KEY) && bck)
  {
    config_kv_delete(bc->bc_kv, bck);
    bck_destroy(B, bck);
  }
  BK_RETURN(B, -1);
}



/*
 * Retrieve a value based on the key.
 * If ovalue is NULL, then get first value, else get successor of ovalue
 */
char *
bk_config_getnext(bk_s B, struct bk_config *ibc, const char *key, const char *ovalue)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_config *bc;
  struct bk_config_key *bck;
  struct bk_config_value *bcv=NULL;
  
  if (!key)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  SET_CONFIG(bc, B, ibc);

  if (!(bck=config_kv_search(bc->bc_kv, (char *)key)))
  {
    bk_error_printf(B, BK_ERR_WARN, "Could not locate key: %s\n", key);
    goto done;
  }

  if (!ovalue)
  {
    if (!(bcv=config_values_minimum(bck->bck_values)))
    {
      /* 
       * An empty key (which can (only) happen if all its values have been
       * deleted since we do not delete the key structure) should be 
       * portrayed as an non-existent key.
       */
      bk_error_printf(B, BK_ERR_WARN, "Could not locate key: %s\n", key);
      goto done;
    }
  }
  else
  {
    if (!(bcv=config_values_search(bck->bck_values,(char *)ovalue)))
    {
      bk_error_printf(B, BK_ERR_WARN, "Could not locate %s as a value of %s in order to get its successor\n", ovalue, key);
      goto done;
    }
    else
    {
      if (!(bcv=config_values_successor(bck->bck_values,bcv)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not locate successor of %s in key %s\n", ovalue, key);
      }
    }
  }
 done:
  if (bcv) BK_RETURN(B, bcv->bcv_value);
  BK_RETURN(B, NULL);
}



/*
 * Delete an entire key
 */
int 
bk_config_delete_key(bk_s B, struct bk_config *ibc, const char *key)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_config *bc;
  struct bk_config_key *bck;
  int ret=0;

  if (!key)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  SET_CONFIG(bc, B, ibc);

  if (!(bck=config_kv_search(bc->bc_kv, (char *)key)))
  {
    bk_error_printf(B, BK_ERR_WARN, "Attempt do delete nonexistent key: %s\n", key);
    goto done;
  }

  config_kv_delete(bc->bc_kv, bck);
  bck_destroy(B, bck);

 done: 
  BK_RETURN(B, ret);
}



/*
 * Create a fileinfo structure for filename 
 */
static struct bk_config_fileinfo *
bcf_create(bk_s B, const char *filename, struct bk_config_fileinfo *obcf)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  static struct bk_config_fileinfo *bcf=NULL;

  if (!filename)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  BK_MALLOC(bcf);

  if (!bcf)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bcf: %s\n", strerror(errno));
    goto error;
  }
  
  if (!(bcf->bcf_filename=strdup(filename)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup filename: %s\n", strerror(errno));
    goto error;
  }

  if ((bcf->bcf_insideof=obcf))
  {
    if (dll_insert(obcf->bcf_includes, bcf) != DICT_OK)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not insert \"%s\" as include file of \"%s\"\n", filename, obcf->bcf_filename);
      goto error;
    }
  }

  if (!(bcf->bcf_includes=dll_create(NULL,NULL,0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create include file dll for bcf \"%s\"\n", filename);
    goto error;
  }

  BK_RETURN(B, bcf);

 error:
  if (bcf) bcf_destroy(B, bcf);
  BK_RETURN(B, NULL);
}



/*
 * Destroy a fileinfo 
 */
static void
bcf_destroy(bk_s B, struct bk_config_fileinfo *bcf)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  
  if (!bcf)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (bcf->bcf_insideof) dll_delete(bcf->bcf_insideof->bcf_includes,bcf);
  if (bcf->bcf_filename) free(bcf->bcf_filename);
  free(bcf);
  BK_VRETURN(B);
}



/*
 * Create a key struct
 */
static struct bk_config_key *
bck_create(bk_s B, const char *key, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_config_key *bck=NULL;

  if (!key)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }
  
  BK_MALLOC(bck);
  
  if (!bck)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bck: %s\n", strerror(errno));
    goto error;
  }

  if (!(bck->bck_key=strdup(key)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup key (%s): %s\n", key, strerror(errno));
    goto error;
  }

  bck->bck_flags=flags;
  
  if (!(bck->bck_values=config_values_create(bcv_oo_cmp,bcv_ko_cmp,DICT_UNORDERED)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create values clc\n");
    goto error;
  }
  
  BK_RETURN(B,bck);

 error:
  if (bck) bck_destroy(B, bck);
  BK_RETURN(B,NULL);
}



/*
 * Destroy a key structure 
 */
static void
bck_destroy(bk_s B, struct bk_config_key *bck)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_config_value *bcv;

  if (!bck)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }
  
  if (bck->bck_key) free (bck->bck_key);

  /* This CLC header can be NULL if there was an error during bck creation */
  if (bck->bck_values)
  {
    while((bcv=config_values_minimum(bck->bck_values)))
    {
      config_values_delete(bck->bck_values, bcv);
      bcv_destroy(B, bcv);
    }
    config_values_destroy(bck->bck_values);
  }

  free(bck);
  
  BK_VRETURN(B);
}



/*
 * Create a values struct
 */
static struct bk_config_value *
bcv_create(bk_s B, const char *value, u_int lineno, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_config_value *bcv=NULL;

  if (!value)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B,NULL);
  }

  BK_MALLOC(bcv);
  if (!bcv)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bcv: %s\n", strerror(errno));
    goto error;
  }

  if (!(bcv->bcv_value=strdup(value)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup %s: %s\n", value, strerror(errno));
    goto error;
  }

  bcv->bcv_lineno=lineno;
  bcv->bcv_flags=flags;

  BK_RETURN(B, bcv);
  
 error:
  if (bcv) bcv_destroy(B, bcv);
  BK_RETURN(B,NULL);
}



/*
 * Destroy a bcv
 */
static void
bcv_destroy(bk_s B, struct bk_config_value *bcv)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bcv)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }
  
  if (bcv->bcv_value) free (bcv->bcv_value);
  free(bcv);
  
  BK_VRETURN(B);
}



/*
 * CLC key-side comparison routines
 */
static int kv_oo_cmp(void *bck1, void *bck2)
{
  return(strcmp(((struct bk_config_key *)bck1)->bck_key, ((struct bk_config_key *)bck2)->bck_key));
} 
static int kv_ko_cmp(void *a, void *bck)
{
  return(strcmp((char *)a, ((struct bk_config_key *)bck)->bck_key));
} 
static ht_val kv_obj_hash(void *bck)
{
  return(bk_strhash(((struct bk_config_key *)bck)->bck_key, BK_STRHASH_NOMODULUS));
}
static ht_val kv_key_hash(void *a)
{
  return(bk_strhash((char *)a, BK_STRHASH_NOMODULUS));
}



/*
 * CLC value-side comparison routines
 */
static int bcv_oo_cmp(void *bcv1, void *bcv2)
{
  return(((struct bk_config_value *)bcv1)->bcv_value - ((struct bk_config_value *)bcv2)->bcv_value);
} 
static int bcv_ko_cmp(void *a, void *bcv)
{
  return((char *)a - ((struct bk_config_value *)bcv)->bcv_value);
} 
