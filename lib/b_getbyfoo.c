#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_getbyfoo.c,v 1.1 2001/11/07 00:02:19 jtt Exp $";
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

/**
 * @file
 * This file is full of stuff
 */



/** 
 * Get a protocol number no matter which type string you have. 
 *
 *	@param B BAKA thread/global state.
 *	@param protostr The string containing the protocol name or number.
 *	@param ip Option copyout version of the protocol structure.
 *	@return <i>-1</i> on failure.
 *	@return <br><i>0</i> on success.
 */
int
bk_getprotobyfoo(bk_s B, char *protostr, struct protoent **ip)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct protoent *p, *n=NULL;
  char **s;
  int alias_count=0;
  int ret;
  int num;
  int count;
  
  if (!protostr)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  /* NB: if ip is set then *ip get initialized to *something* immediately */
  if (ip)
  {
    *ip=NULL;
    BK_MALLOC(n);
    if (!n)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate ip: %s\n", strerror(errno));
      goto error;
    }
  }

  
  /* MUTEX_LOCK */
  if (bk_string_atoi(B,protostr,&num,0)==0)
  {
    if (!(p=getprotobynumber(num)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not convert %s to protocol: %s\n", protostr, strerror(errno));
      goto error;
    }
  }
  else
  {
    if (!(p=getprotobyname(protostr)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not convert %s to protocol: %s\n", protostr, strerror(errno));    
      goto error;
    }
  }

  if (ip)
  {
    if (!(n->p_name=strdup(p->p_name)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not dup protocol name: %s\n", strerror(errno));
      goto error;
    }

    if (p->p_aliases)
    {
      for(s=p->p_aliases; *s; s++)
	alias_count++;
    
      if (!(n->p_aliases=calloc((alias_count+1),sizeof(n->p_aliases))))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not allocate proto alias buffer: %s\n", strerror(errno));
	goto error;
      }

      for(count=0; count<alias_count; count++)
      {
	if (!(n->p_aliases[count]=strdup(p->p_aliases[count])))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not duplicate a protocol aliase: %s\n", strerror(errno));
	  goto error;
	}
      }
    }
    n->p_proto=p->p_proto;
  }
  
  /* Sigh have to save this to automatic so we can unlock before return */
  ret=p->p_proto;

  /* MUTEX_UNLOCK */
  
  if (ip) *ip=n;

  BK_RETURN(B,ret);

 error: 
  if (n) bk_protoent_destroy(B,n);
  if (ip) *ip=NULL;
  
  BK_RETURN(B,-1);
}



/**
 * Completely free up a protoent copied by the above function
 *	@param B BAKA thread/global state.
 */
void
bk_protoent_destroy(bk_s B, struct protoent *p)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char **s;

  if (!p)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (p->p_name) free (p->p_name);

  if (p->p_aliases)
  {
    for (s=p->p_aliases; *s; s++)
    {
      free(*s); /* We've already checked if *s exists */
    }
    free(p->p_aliases);
  }
  free(p);
  BK_VRETURN(B);
}


