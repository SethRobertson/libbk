#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_getbyfoo.c,v 1.9 2001/11/13 20:42:17 jtt Exp $";
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
 * This file contains all the getbyFOO functions. These functions provide
 * the caller with a "convenient" interface to the standard @a netdb.h
 * functions (Windows people don't have @a netdb.h but you get the
 * idea). The primary value-add is that the standard name or number/addr
 * argument replaced by a string and the function determines dynamically
 * what you have passed it and so makes the correct call. This is
 * particularly useful in UI's where you ask the user for a "hostname/IP
 * address". <em>You</em> do not have to worry about which he enters; you
 * simply pass the string along to @a bk_gethostbyfoo and it takes care
 * of everything.
 */


/**
 * This is the state which @a bk_gethostbyfoo must preserve in order to
 * successfully invoke the caller's callback and set the caller's "copyout"
 * @a struct @a hostent pointer.
 */
struct bk_gethostbyfoo_state
{
  struct hostent **	bgs_user_copyout;	/** Caller's pointer  */
  struct bk_netaddr 	bgs_bna;		/** Space for an addr */
  struct bk_netinfo *	bgs_bni;		/** bk_netinfo from caller */
  struct hostent *	bgs_hostent;		/** Actual hostent info */
  void 			(*bgs_callback)(bk_s B, struct bk_run *run, struct hostent **h, struct bk_netinfo *bni , void *args); /** Caller's callback */
  void *		bgs_args;		/* Caller's argument to @a callback */
  bk_flags		bgs_flags;		/* Everyone needs flags */
};

static int copy_hostent(bk_s B, struct hostent **ih, struct hostent *h);
static void gethostbyfoo_callback(bk_s B, struct bk_run *run, void *args, struct timeval starttime, bk_flags flags);




/** 
 * Get a protocol number no matter which type string you have. 
 *
 *	@param B BAKA thread/global state.
 *	@param protostr The string containing the protocol name or number.
 *	@param ip Optional copyout version of the protocol structure.
 *	@param bni Optional @a netinfo structure which will have its proto
 *	field filled out on a successful conclusion.
 *	@return <i>-1</i> on failure.
 *	@return <br><i>proto_num</i> on success.
 */
int
bk_getprotobyfoo(bk_s B, char *protostr, struct protoent **ip, struct bk_netinfo *bni)
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
    if (!BK_CALLOC(n))
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
      /* MUTEX_UNLOCK */
      bk_error_printf(B, BK_ERR_ERR, "Could not convert %s to protocol: %s\n", protostr, strerror(errno));
      goto error;
    }
  }
  else
  {
    if (!(p=getprotobyname(protostr)))
    {
      /* MUTEX_UNLOCK */
      bk_error_printf(B, BK_ERR_ERR, "Could not convert %s to protocol: %s\n", protostr, strerror(errno));    
      goto error;
    }
  }

  if (ip)
  {
    if (!(n->p_name=strdup(p->p_name)))
    {
      /* MUTEX_UNLOCK */
      bk_error_printf(B, BK_ERR_ERR, "Could not dup protocol name: %s\n", strerror(errno));
      goto error;
    }

    if (p->p_aliases)
    {
      for(s=p->p_aliases; *s; s++)
	alias_count++;
    
      if (!(n->p_aliases=calloc((alias_count+1),sizeof(n->p_aliases))))
      {
	/* MUTEX_UNLOCK */
	bk_error_printf(B, BK_ERR_ERR, "Could not allocate proto alias buffer: %s\n", strerror(errno));
	goto error;
      }

      for(count=0; count<alias_count; count++)
      {
	if (!(n->p_aliases[count]=strdup(p->p_aliases[count])))
	{
	  /* MUTEX_UNLOCK */
	  bk_error_printf(B, BK_ERR_ERR, "Could not duplicate a protocol aliase: %s\n", strerror(errno));
	  goto error;
	}
      }
    }
    n->p_proto=p->p_proto;
  }
  
  /* Sigh have to save this to automatic so we can unlock before return */
  ret=p->p_proto;
  if (bni) bk_netinfo_update_protoent(B,bni,p);

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
 *	@param p The @a protoent structure to destroy.
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
 *	@return <br><i>port_num</i> (in <em>host</em> order) on success.
 */
int
bk_getservbyfoo(bk_s B, char *servstr, char *iproto, struct servent **is, struct bk_netinfo *bni)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct servent *s, *n=NULL;
  char **s1;
  int alias_count=0;
  int ret;
  int num;
  int count;
  char *proto;
  char *bni_proto=NULL;
  
  /* If it is possible to extract the protostr from the bni, do so and cache*/
  if (bni && bni->bni_bpi)
    bni_proto=bni->bni_bpi->bpi_protostr;

  /* We must have a servstr and *some* protostr */
  if (!servstr || (!iproto && !bni_proto))
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  /* If both possible protostr's are set, they must be the same */
  if (iproto && bni_proto && !BK_STREQ(iproto,bni_proto))
  {
    bk_error_printf(B, BK_ERR_ERR, "Protocol mismatch (%s != %s)\n", iproto, bni_proto);
    goto error;
  }

  /* 
   * At this point either both are the same or exactly one is set, so the
   * the following correctly determines the protostr.
   */
  if (iproto)
    proto=iproto;
  else
    proto=bni_proto;


  /* NB: if is is set then *is get initialized to *something* immediately */
  if (is)
  {
    *is=NULL;
    if (!BK_CALLOC(n))
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
      /* MUTEX_UNLOCK */
      bk_error_printf(B, BK_ERR_ERR, "Could not convert %s to service: %s\n", servstr, strerror(errno));
      goto error;
    }
  }
  else
  {
    if (!(s=getservbyname(servstr, proto)))
    {
      /* MUTEX_UNLOCK */
      bk_error_printf(B, BK_ERR_ERR, "Could not convert %s to service: %s\n", servstr, strerror(errno));    
      goto error;
    }
  }

  if (is)
  {
    if (!(n->s_name=strdup(s->s_name)))
    {
      /* MUTEX_UNLOCK */
      bk_error_printf(B, BK_ERR_ERR, "Could not dup service name: %s\n", strerror(errno));
      goto error;
    }

    if (s->s_aliases)
    {
      for(s1=s->s_aliases; *s1; s1++)
	alias_count++;
    
      if (!(n->s_aliases=calloc((alias_count+1),sizeof(n->s_aliases))))
      {
	/* MUTEX_UNLOCK */
	bk_error_printf(B, BK_ERR_ERR, "Could not allocate service alias buffer: %s\n", strerror(errno));
	goto error;
      }

      for(count=0; count<alias_count; count++)
      {
	/* MUTEX_UNLOCK */
	if (!(n->s_aliases[count]=strdup(s->s_aliases[count])))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not duplicate a service aliase: %s\n", strerror(errno));
	  goto error;
	}
      }
    }

    if (!(n->s_proto=strdup(s->s_proto)))
    {
      /* MUTEX_UNLOCK */
      bk_error_printf(B, BK_ERR_ERR, "Could not duplicate proto name: %s\n", strerror(errno));
      goto error;
    }
    n->s_port=s->s_port;
  }
  
  if (bni) bk_netinfo_update_servent(B, bni, s);

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
 *	@param s The @a servent structure to destroy
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



/**
 * Get a hostent using whatever string you might happen to have. If @a
 * family is not 0, queries will be restricted to that address family,
 * otherwise @a bk_gethostbyfoo will attempt to intuit the address
 * family. @a ih is <em>required</em> in this functions (unlike @a
 * bk_getservbyfoo and @a bk_getprotobyfoo). @a *ih will be pointing at an
 * <em>allocated</em> @a struct @a hostent when query completes. You should
 * free this data with @a bk_destroy_hostent when finished. <br> Since this
 * function may take quite a long time to complete, and we shall at some
 * time in the near future be integrating it with a nonblocking libresolv,
 * you must supply both a @a bk_run structure and a @a callback. @a
 * callback will be called when the answer arrives. If succesfull, @a *ih
 * will be (as mentioned previously) at the @a struct @a hostent; if not,
 * @a *ih will be NULL. <br><em>HACK ALERT:</em> In order to make sure that
 * callers do not abuse this function while it still uses blocking queries,
 * @a *ih is <em>guarenteed</em> to be NULL on the return from @a
 * bk_gethostbyfoo. @a callback will be invoked (and @a *ih set) on the
 * <em>subsequent</em> @a bk_run_once pass. If BK_GETHOSTBYFOO_FLAG_FQDN
 * flags is set, then the fully qualified name is return. This of course
 * really only makes sens on an addr ==> name lookup.
 *
 *	@param B BAKA thread/global state.
 *	@param name String to lookup.
 *	@param family Address family to which to restrict queries.
 *	@param ih Copyout @a struct @a hostent pointer.
 *	@param run @a bk_run structure.
 *	@param callback Function to invoke when answer is arrives.
 *	@param args User args to return to @a callback when invoked.
 *	@param lflags. See code for description of flags.
 *	@returns <i>-1</i> on failure.
 *	@returns <i>0</i> on success.
 */
int
bk_gethostbyfoo(bk_s B, char *name, int family, struct hostent **ih, struct bk_netinfo *bni, struct bk_run *br, void (*callback)(bk_s B, struct bk_run *run, struct hostent **h, struct bk_netinfo *bni, void *args), void *args, bk_flags user_flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int flags=0;					/* 1 == Is an address */
  int len=0;					/* Len. of address by family */
  struct in_addr in_addr;			/* Temp in_addr */
  struct in6_addr in6_addr;			/* Temp in6_addr */
  struct hostent *h=NULL;			/* Result of getbyfoo(3) */
  struct bk_gethostbyfoo_state *bgs=NULL;	/* Saved state for callback */
  struct hostent fake_hostent;			/* Fake hostent */
  char **buf[2];				/* Buf. for addrs of fake */
  char *buf2[400];				/* Buf. for addrs of fake */
  void *addr=NULL;				/*  */
  struct hostent *tmp_h=NULL;			/* Temporary version. */
  

  /* No point to using *this* func. without copyout, so we check ih */
  if (!name || (!ih && !bni) || !callback) 
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  /* Make sure this is initialized right away (see error section) */
  if (ih) *ih=NULL; 

  /* Clear these too. */
  memset(buf,0,sizeof(buf));
  memset(buf2,0,sizeof(buf2));
  memset(&fake_hostent,0,sizeof(fake_hostent));
  fake_hostent.h_addr_list=(char **)buf;
  buf[0]=buf2;

  if (inet_pton(AF_INET, name, &in_addr))
  {
    if (family) 
    {
      if (family != AF_INET)
      {
	bk_error_printf(B, BK_ERR_ERR, "Address family mismatch (%d != %d)\n", family, AF_INET);
	BK_RETURN(B,1);
      }
    }

    family=AF_INET; /* Yes this might be redundant. Leave me alone */
    len=sizeof(struct in_addr);
    BK_FLAG_SET(flags, 0x1);
    addr=&in_addr;
  }
  else if (inet_pton(AF_INET6, name, &in6_addr))
  {
    if (family) 
    {
      if (family != AF_INET6)
      {
	bk_error_printf(B, BK_ERR_ERR, "Address family mismatch (%d != %d)\n", family, AF_INET6);
	BK_RETURN(B,1);
      }
    }

    BK_FLAG_SET(flags, 0x1);
    family=AF_INET6;
    len=sizeof(struct in6_addr);
    addr=&in6_addr;
  }

  /* MUTEX_LOCK */
  if (BK_FLAG_ISCLEAR(user_flags, BK_GETHOSTBYFOO_FLAG_FQDN) && BK_FLAG_ISSET(flags, 0x1))
  {
    if (addr)
    {
      h=&fake_hostent;
      h->h_addrtype=family;
      h->h_length=len;
      memmove(h->h_addr_list[0], addr, len);
    }
    else
    {
      bk_error_printf(B, BK_ERR_ERR, "Converted an address, but addr was NULL. How can this happen?\n");
      goto error;
    }
  }
  else if (BK_FLAG_ISSET(flags, 0x1))
  {
    if (!(h=gethostbyaddr(((family==AF_INET)?(char *)&in_addr:(char *)&in6_addr), len, family)))
    {
      /* MUTEX_UNLOCK */
      bk_error_printf(B, BK_ERR_ERR, "Could not convert %s address: %s\n", (family==AF_INET)?"AF_INET":"AF_INET6",hstrerror(h_errno));
      goto error;
    }
  }
  else
  {
    if (family) 
    {
      h=gethostbyname2(name, family);
    }
    else
    {
      if (!(h=gethostbyname2(name, AF_INET)))
      {
	h=gethostbyname2(name, AF_INET6);
	family=AF_INET6; /* Sure this gets set if h==NULL, so what? :-) */
      }
      else
      {
	family=AF_INET;
      }

      if (!h)
      {
	/* MUTEX_UNLOCK */
	bk_error_printf(B, BK_ERR_ERR, "Could not convert %s hostname: %s\n", (family==AF_INET)?"AF_INET":"AF_INET6",hstrerror(h_errno));
	goto error;
      }
    }
  }

  if (copy_hostent(B,&tmp_h,h)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not copy hostent\n");
    /* MUTEX_UNLOCK */
    goto error;
  }

  /* MUTEX_UNLOCK */

  /*
   * From here on down we are making an attempt to prevent "lazy"
   * programmers from taking advantage of the blocking nature of the above
   * calls, since ultimately we'd like to tie into a non-blocking resolver
   * lib. So here we save the state from this function, set a 0 delta event
   * (ie execute on next select loop) and then call the caller's
   * callback. This forces users of this function to make sure that there
   * code can survive returning to at least one select loop run without the
   * hostname info.
   */
  if (!BK_CALLOC(bgs))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bgs: %s\n", strerror(errno));
    goto error;
    
  }
  bgs->bgs_hostent = tmp_h;
  /* Do *not*  NULL tmp_h here. If there's an error we do want to free this */
  bgs->bgs_user_copyout=ih;
  bgs->bgs_callback=callback;
  bgs->bgs_args=args;
  bgs->bgs_flags=flags;
  bgs->bgs_bni=bni;

  if (bk_run_enqueue_delta(B, br, 0, gethostbyfoo_callback, bgs, NULL, 0)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not enqueue gethostbyfoo callback\n");
    goto error;
  }

  BK_RETURN(B,0);

 error:
  if (tmp_h) bk_destroy_hostent(B, tmp_h);
  if (bgs) free(bgs);
  if (ih) *ih=NULL;
  BK_RETURN(B,-1);
}



/**
 * Destroy a hostent structure
 *	@param B BAKA thread/global state.
 *	@param h Hostent to destroy.
 */
void
bk_destroy_hostent(bk_s B, struct hostent *h)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char **s;

  if (!h)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (h->h_name) free(h->h_name);
  
  if (h->h_aliases)
  {
    for(s=h->h_aliases; *s; s++)
    {
      free(*s);
    }
    free(h->h_aliases);
  }

  if (h->h_addr_list)
  {
    for (s=h->h_addr_list; *s; s++)
    {
      free(*s);
    }
    free(h->h_addr_list);
  }
  free(h);
  BK_VRETURN(B);
}



/**
 * Copy a struct hostent from static space to allocated space.  <br> NB:
 * <em>this function assumes that the the struct hostent source buffer has
 * ben locked by a higher caller.</em>
 *	@param B BAKA thread/global state.
 *	@param ih Copyout pointer to new hostent.
 *	@param h Hostent source data.
 *	@return <i>0</i> on success.
 *	@return <i>-1</i> on failure.
 */
static int
copy_hostent(bk_s B, struct hostent **ih, struct hostent *h)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct hostent *n=NULL;
  int count,c;
  char **s;

  if (!ih || !h)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!BK_CALLOC(n))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate hostent: %s\n", strerror(errno));
    goto error;
  }

  *ih=NULL;

  if (h->h_name && !(n->h_name=strdup(h->h_name)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup h_name: %s\n", strerror(errno));
    goto error;
  }

  if (h->h_aliases)
  {
    for(count=0,s=h->h_aliases; *s; s++)
      count++;
    if (!(n->h_aliases=calloc(count+1, sizeof(*s))))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not calloc aliases: %s\n", strerror(errno));
      goto error;
    }
  
    for(c=0; c<count;c++)
    {
      if (!(n->h_aliases[c]=strdup(h->h_aliases[c])))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not strdup an alias: %s\n", strerror(errno));
	goto error;
      }
    }
  }
  
  n->h_addrtype = h->h_addrtype;
  n->h_length = h->h_length;

  if (h->h_addr_list)
  {
    if (h->h_addrtype == AF_INET)
    {
      struct in_addr **ia;
      for (count=0,ia=(struct in_addr **)(h->h_addr_list); *ia; ia++)
	count++;
    }
    else if (h->h_addrtype == AF_INET6)
    {
      struct in6_addr **ia;
      for (count=0,ia=(struct in6_addr **)(h->h_addr_list); *ia; ia++)
	count++;
    }
    else
    {
      bk_error_printf(B, BK_ERR_ERR, "Unknown address family: %d\n", h->h_addrtype);
      goto error;
    }

    /*if (!(n->h_addr_list=calloc(count+1,h->h_length)))*/
    if (!(n->h_addr_list=calloc(count+1,sizeof(*n->h_addr_list))))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate addr_list: %s\n", strerror(errno));
      goto error;
    }
    /* We *should* be able to do this one memmove, but this is safer. */
    for(c=0; c<count; c++)
    {
      if (!(n->h_addr_list[c]=malloc(h->h_length)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not allocate space for addr: %s\n", strerror(errno));
	goto error;
      }
      memmove(n->h_addr_list[c],h->h_addr_list[c], h->h_length);
    }
  }

  *ih=n;

  BK_RETURN(B,0);
 error:
  if (n) bk_destroy_hostent(B, n);
  BK_RETURN(B,-1);
  
}



/**
 * The @a gethostbyfoo internal callback right now this does very
 * little but set the caller's pointer at the allocated data and call the
 * caller's callback (that's a lot of 'call's buddy), but eventually this
 * code will run when the answer has arrived. It will then need to decode
 * the answer and allocate the caller's hostent (and then call the callback
 * :-))
 *
 *	@param B BAKA thread/global state.
 *	@param run bk_run structure pointer.
 *	@param args The state stored in @a bk_gethostbyfoo.
 *	@param starttime The start of the current @a select(2) run.
 *	@param flags Random flags.
 */
static void
gethostbyfoo_callback(bk_s B, struct bk_run *run, void *args, struct timeval starttime, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_gethostbyfoo_state *bgs;
  if (!run || !(bgs=args))
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }
  
  /* Finally associate the user's pointer with the hostent data */
  if (bgs->bgs_user_copyout)
  {
    *bgs->bgs_user_copyout=bgs->bgs_hostent;
  }

  if (bgs->bgs_bni && bgs->bgs_hostent)
  {
    if (bk_netinfo_update_hostent(B, bgs->bgs_bni, bgs->bgs_hostent)<0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not update netinfo with hostent\n");
    }

  }

  if (bgs->bgs_callback)
  {
    /* 
     * In *theory* the user doesn't have to have a callback, he can just
     * "poll" his data structures waiting for the info to show up. Rather
     * stupid in general, but might be reasonable in "quick hacks".
     */
    (*bgs->bgs_callback)(B, run, bgs->bgs_user_copyout, bgs->bgs_bni, bgs->bgs_args);
  }

  if (!bgs->bgs_user_copyout)
  {
    /* The user didn't save the info to a direct copyout. We must free */
    bk_error_printf(B, BK_ERR_WARN, "The caller didn't save the hostent info in any way ?\n");
    bk_destroy_hostent(B, bgs->bgs_hostent);
  }

  free(bgs);
  BK_VRETURN(B);
}
