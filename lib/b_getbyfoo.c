#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_getbyfoo.c,v 1.2 2001/11/07 00:28:18 jtt Exp $";
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
 *	@return <br><i>proto_num</i> on success.
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



/** 
 * Get a service number no matter which type string you have. 
 *
 *	@param B BAKA thread/global state.
 *	@param servstr The string containing the service name or number.
 *	@param is Option copyout version of the service structure.
 *	@return <i>-1</i> on failure.
 *	@return <br><i>port_num</i> (in <emp>host</emp> order) on success.
 */
int
bk_getservbyfoo(bk_s B, char *servstr, char *proto, struct servent **is)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct servent *s, *n=NULL;
  char **s1;
  int alias_count=0;
  int ret;
  int num;
  int count;
  
  if (!servstr || !proto)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  /* NB: if is is set then *is get initialized to *something* immediately */
  if (is)
  {
    *is=NULL;
    BK_MALLOC(n);
    if (!n)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate is: %s\n", strerror(errno));
      goto error;
    }
  }

  
  /* MUTEX_LOCK */
  if (bk_string_atoi(B,servstr,&num,0)==0)
  {
    if (!(s=getservbyport(num, proto)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not convert %s to service: %s\n", servstr, strerror(errno));
      goto error;
    }
  }
  else
  {
    if (!(s=getservbyname(servstr, proto)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not convert %s to service: %s\n", servstr, strerror(errno));    
      goto error;
    }
  }

  if (is)
  {
    if (!(n->s_name=strdup(s->s_name)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not dup service name: %s\n", strerror(errno));
      goto error;
    }

    if (s->s_aliases)
    {
      for(s1=s->s_aliases; *s1; s1++)
	alias_count++;
    
      if (!(n->s_aliases=calloc((alias_count+1),sizeof(n->s_aliases))))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not allocate service alias buffer: %s\n", strerror(errno));
	goto error;
      }

      for(count=0; count<alias_count; count++)
      {
	if (!(n->s_aliases[count]=strdup(s->s_aliases[count])))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not duplicate a service aliase: %s\n", strerror(errno));
	  goto error;
	}
      }
    }

    if (!(n->s_proto=strdup(s->s_proto)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not duplicate proto name: %s\n", strerror(errno));
      goto error;
    }
    n->s_port=s->s_port;
  }
  
  /* Sigh have to save this to automatic so we can unlock before return */
  ret=s->s_port;


  /* MUTEX_UNLOCK */
  
  if (is) *is=n;

  /* Return the port (in host order) since this is what folks want */
  BK_RETURN(B,ntohs(ret));

 error: 
  if (n) bk_servent_destroy(B,n);
  if (is) *is=NULL;
  
  BK_RETURN(B,-1);
}



/**
 * Completely free up a servent copied by the above function
 *	@param B BAKA thread/global state.
 */
void
bk_servent_destroy(bk_s B, struct servent *s)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char **s1;

  if (!s)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (s->s_name) free (s->s_name);

  if (s->s_aliases)
  {
    for (s1=s->s_aliases; *s1; s1++)
    {
      free(*s1); /* We've already checked if *s exists */
    }
    free(s->s_aliases);
  }

  if (s->s_proto) free (s->s_proto);
  free(s);
  BK_VRETURN(B);
}


