#if !defined(lint) && !defined(__INSIGHT__)
#include "libbk_compiler.h"
UNUSED static const char libbk__copyright[] = "Copyright (c) 2003";
UNUSED static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright LIBBK++
 *
 * Copyright (c) 2003 The Authors. All rights reserved.
 *
 * This source code is licensed to you under the terms of the file
 * LICENSE.TXT in this release for further details.
 *
 * Mail <projectbaka@baka.org> for further information
 *
 * --Copyright LIBBK--
 */

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

#include <libbk.h>
#include "libbk_internal.h"


/**
 * This is the state which @a bk_gethostbyfoo must preserve in order to
 * successfully invoke the caller's callback.
 */
struct bk_gethostbyfoo_state
{
  struct bk_run *		bgs_run;	///< Run struct
  struct bk_netaddr		bgs_bna;	///< Space for an addr
  struct bk_netinfo *		bgs_bni;	///< bk_netinfo from caller
  struct hostent *		bgs_hostent;	///< Actual hostent info
  bk_gethostbyfoo_callback_f	bgs_callback;	///< Caller's callback
  void *			bgs_args;	///< Caller's argument to @a callback
  bk_flags			bgs_flags;	///< Everyone needs flags
  void *			bgs_event;	///< Timeout event to delay returning result to enforce async in future
};



static int copy_hostent(bk_s B, struct hostent **ih, struct hostent *h);
static void gethostbyfoo_callback(bk_s B, struct bk_run *run, void *args, const struct timeval starttime, bk_flags flags);
static struct bk_gethostbyfoo_state *bgs_create(bk_s B);
static void bgs_destroy(bk_s B, struct bk_gethostbyfoo_state *bgs);




/**
 * Get a protocol number no matter which type string you have.
 *
 *	@param B BAKA thread/global state.
 *	@param protostr The string containing the protocol name or number.
 *	@param iproto Optional copyout version of the protocol structure.
 *	@param bni Optional @a netinfo structure which will have its proto
 *	field filled out on a successful conclusion.
 *	@return <i>-1</i> on failure.
 *	@return <br><i>proto_num</i> on success.
 */
int
bk_getprotobyfoo(bk_s B, char *protostr, struct protoent **iproto, struct bk_netinfo *bni, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct protoent *p, *n = NULL;
  char **s;
  int alias_count = 0;
  int ret;
  int num;
  int count;
  struct protoent dummy;

  if (!protostr)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  /*
   * NB: if iproto is set then *iproto get initialized to *something*
   * immediately
   */
  if (iproto)
  {
    /* If the copyout is set force a lookup */
    BK_FLAG_SET(flags, BK_GETPROTOBYFOO_FORCE_LOOKUP);
    *iproto = NULL;
    if (!BK_CALLOC(n))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate protoent: %s\n", strerror(errno));
      goto error;
    }
  }

  if (BK_STREQ(protostr, BK_AF_LOCAL_STREAM_PROTO_STR))
  {
    p = &dummy;
    memset(p, 0, sizeof(*p));
    p->p_proto = BK_GENERIC_STREAM_PROTO;
    p->p_name = BK_AF_LOCAL_STREAM_PROTO_STR;
  }
  else if (BK_STREQ(protostr, BK_AF_LOCAL_DGRAM_PROTO_STR))
  {
    p = &dummy;
    memset(p, 0, sizeof(*p));
    p->p_proto = BK_GENERIC_DGRAM_PROTO;
    p->p_name = BK_AF_LOCAL_DGRAM_PROTO_STR;
  }
  else if (BK_STRING_ATOI(B,protostr,&num,0) == 0)
  {
    /* This is a number so only do search if forced */
    if (BK_FLAG_ISSET(flags, BK_GETPROTOBYFOO_FORCE_LOOKUP))
    {
      if (!(p = getprotobynumber(num)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not convert %s to protocol: %s\n", protostr, strerror(errno));
	goto error;
      }
    }
    else
    {
      p = &dummy;
      memset(p,0,sizeof(*p));
      p->p_proto = num;
    }
  }
  else
  {
    if (!(p = getprotobyname(protostr)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not convert %s to protocol: %s\n", protostr, strerror(errno));
      goto error;
    }
  }

  if (iproto)
  {
    if (p->p_name && !(n->p_name = strdup(p->p_name)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not dup protocol name: %s\n", strerror(errno));
      goto error;
    }

    if (p->p_aliases)
    {
      for(s = p->p_aliases; *s; s++)
	alias_count++;

      if (!(n->p_aliases = calloc((alias_count+1),sizeof(*(n->p_aliases)))))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not allocate proto alias buffer: %s\n", strerror(errno));
	goto error;
      }

      for(count = 0; count<alias_count; count++)
      {
	if (!(n->p_aliases[count] = strdup(p->p_aliases[count])))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not duplicate a protocol aliase: %s\n", strerror(errno));
	  goto error;
	}
      }
    }
    n->p_proto = p->p_proto;
  }

  /* Sigh have to save this to automatic so we can unlock before return */
  ret = p->p_proto;

  if (bni)
    bk_netinfo_update_protoent(B,bni,p);

  if (iproto)
  {
    *iproto = n;
  }
  else
  {
    /* Look like some forced a lookup without saving, so clean up */
    if (n) bk_protoent_destroy(B, n);
  }

  BK_RETURN(B,ret);

 error:
  if (n) bk_protoent_destroy(B,n);
  if (iproto) *iproto = NULL;

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
    for (s = p->p_aliases; *s; s++)
    {
      free(*s); /* We've already checked if *s exists */
    }
    free(p->p_aliases);
  }
  free(p);
  BK_VRETURN(B);
}



/**
 * Get a service number no matter which type string you have. This function
 * is really blocking (in the presence of NIS)
 *
 *	@param B BAKA thread/global state.
 *	@param servstr The string containing the service name or number.
 *	@param iproto Optional string specifying the protocol to use. If
 *	unset and @a bni is not NULL, we will attempt to recover the
 *	protocol string from there. If that fails we default to looking up
 *	the string version of IPPROTO_TCP.
 *	@param is Option copyout version of the service structure.
 *	@return <i>-1</i> on failure.
 *	@return <br><i>port_num</i> (in <em>network</em> order) on success.
 */
int
bk_getservbyfoo(bk_s B, char *servstr, char *iproto, struct servent **is, struct bk_netinfo *bni, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct servent *s, *n = NULL;
  char **s1;
  int ret = 0;
  int alias_count = 0;
  int num;					///< Port number (*host* order)
  int count;
  char *proto = NULL;
  char *bni_proto = NULL;
  struct servent dummy;
  struct protoent *lproto = NULL;
  char defproto[10];

  /* If it is possible to extract the protostr from the bni, do so and cache*/
  if (bni && bni->bni_bpi)
    bni_proto = bni->bni_bpi->bpi_protostr;

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
    proto = iproto;
  else
    proto = bni_proto;

  if (!proto)
  {
    snprintf(defproto,sizeof(defproto),"%d", IPPROTO_TCP);
    proto = defproto;
  }


  /* NB: if is is set then *is get initialized to *something* immediately */
  if (is)
  {
    *is = NULL;
    BK_FLAG_SET(flags, BK_GETSERVBYFOO_FORCE_LOOKUP);
    if (!BK_CALLOC(n))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate is: %s\n", strerror(errno));
      goto error;
    }
  }

  /*
   * ARGGHH!! First you have to resolve the protobyfoo. Furthermore you
   * have to go all the way as it were.
   */
  if (bk_getprotobyfoo(B, proto, &lproto, bni, BK_GETPROTOBYFOO_FORCE_LOOKUP) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not convert proto string: %s\n", proto);
    goto error;
  }

  /*
   * If we found the protocol use it's official name. If not just stick
   * with whatever we have
   */
  if (lproto)
  {
    proto = lproto->p_name;
  }

  if (BK_STRING_ATOI(B, servstr, &num, 0) == 0)
  {
    /* This a a number so only do seach if forced */
    if (BK_FLAG_ISSET(flags, BK_GETSERVBYFOO_FORCE_LOOKUP))
    {
      if (!(s = getservbyport(num, proto)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not convert %s to service: %s\n", servstr, strerror(errno));
	goto error;
      }
    }
    else
    {
      s = &dummy;
      memset(s,0,sizeof(*s));
      s->s_port = htons(num);
      s->s_proto = proto;
    }
  }
  else
  {
    if (!(s = getservbyname(servstr, proto)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not convert %s to service: %s\n", servstr, strerror(errno));
      goto error;
    }
  }

  if (is)
  {
    if (!(n->s_name = strdup(s->s_name)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not dup service name: %s\n", strerror(errno));
      goto error;
    }

    if (s->s_aliases)
    {
      for(s1 = s->s_aliases; *s1; s1++)
	alias_count++;

      if (!(n->s_aliases = calloc((alias_count+1),sizeof(*n->s_aliases))))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not allocate service alias buffer: %s\n", strerror(errno));
	goto error;
      }

      for(count = 0; count<alias_count; count++)
      {
	if (!(n->s_aliases[count] = strdup(s->s_aliases[count])))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not duplicate a service aliase: %s\n", strerror(errno));
	  goto error;
	}
      }
    }

    if (!(n->s_proto = strdup(s->s_proto)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not duplicate proto name: %s\n", strerror(errno));
      goto error;
    }
    n->s_port = s->s_port;
  }

  if (bni) bk_netinfo_update_servent(B, bni, s);

  /* Sigh have to save this to automatic so we can unlock before return */
  ret = s->s_port;

  if (is)
  {
    *is = n;
  }
  else
  {
    /* Look like some forced a lookup without saving, so clean up */
    if (n) bk_servent_destroy(B,n);
  }

  if (lproto) bk_protoent_destroy(B,lproto);

  BK_RETURN(B, ret);

 error:
  if (lproto) bk_protoent_destroy(B,lproto);
  if (n) bk_servent_destroy(B,n);
  if (is) *is = NULL;

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
    for (s1 = s->s_aliases; *s1; s1++)
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
 * family. When the callback is executed, it will be passed an
 * <em>allocated</em> @a struct @a hostent (if successful, NULL if
 * not).  You should free this data with @a bk_destroy_hostent when
 * finished.
 *
 * <br> Since this function may take quite a long time to complete,
 * and we shall at some time in the near future be integrating it with
 * a nonblocking libresolv, you must supply both a @a bk_run structure
 * and a @a callback. @a callback will be called when the answer
 * arrives.
 *
 * <br><em>HACK ALERT:</em> In order to make sure that callers do not
 * abuse this function while it still uses blocking queries, we ensure
 * that @a callback will be invoked on the
 * <em>subsequent</em> @a bk_run_once pass. If
 * BK_GETHOSTBYFOO_FLAG_FQDN flags is set, then the fully qualified
 * name is return. This of course really only makes sens on an addr
 * ==> name lookup.
 *
 * On success the caller gets back an opaque handle which is useful
 * <em>only</em> as an argument to @a
 * bk_gethostbyfoo_abort(). This function should be called iff
 * an error occurs which makes the callback for gethostbyfoo a bad
 * idea.
 *
 *
 *	@param B BAKA thread/global state.
 *	@param name String to lookup.
 *	@param family Address family to which to restrict queries.
 *	@param bni @a bk_netinfo structure for netutils assistance
 *	@param run @a bk_run structure.
 *	@param callback Function to invoke when answer is arrives.
 *	@param args User args to return to @a callback when invoked.
 *	@param lflags. See code for description of flags.
 *	@returns <i>NULL</i> on failure.
 *	@returns opaque <i>gethostbyfoo info</i> on success.
 */
void *
bk_gethostbyfoo(bk_s B, char *name, int family, struct bk_netinfo *bni, struct bk_run *run, bk_gethostbyfoo_callback_f callback, void *args, bk_flags user_flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int flags = 0;					/* 1 == Is an address */
  int len = 0;					/* Len. of address by family */
  struct in_addr in_addr;			/* Temp in_addr */
  struct in6_addr in6_addr;			/* Temp in6_addr */
  struct hostent *h = NULL;			/* Result of getbyfoo(3) */
  struct bk_gethostbyfoo_state *bgs = NULL;	/* Saved state for callback */
  struct hostent fake_hostent;			/* Fake hostent */
  char **buf[2];				/* Buf. for addrs of fake */
  char *buf2[400];				/* Buf. for addrs of fake */
  void *addr = NULL;				/* Temp addr buf for "fake" hostname creation */
  struct hostent *tmp_h = NULL;			/* Temporary version. */
  struct in_addr inaddr_any;			/* Pretty self explanatory */

  inaddr_any.s_addr = INADDR_ANY;

  if (!name || !callback)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    goto error;
  }

  /* Clear these too. */
  memset(buf,0,sizeof(buf));
  memset(buf2,0,sizeof(buf2));
  memset(&fake_hostent,0,sizeof(fake_hostent));
  fake_hostent.h_addr_list = (char **)buf;
  buf[0] = buf2;

  // first check if we are trying to deal with ANY address
  if (BK_STREQ(name, BK_ADDR_ANY))
  {
    // if family unspecified, assume AF_INET (not like we handle anything else)
    if (!family)
      family = AF_INET;

    switch (family)
    {
    case AF_INET:
      len = sizeof(struct in_addr);
      BK_FLAG_SET(flags,0x1);
      addr = &inaddr_any;
      break;
    case AF_INET6:
      /*
       * <WARNING>
       * This is *bogus* AF_INET6 doesn't define an "any address"
       * only an "any sockaddr" which is not what we want. Well we should
       * hack something together here and return it, but for the moment
       * we just call it unsupported.
       * </WARNING>
       */
      /* Intentional fallthrough */
    default:
      bk_error_printf(B, BK_ERR_ERR, "ANY address is not (yet?) supported in address family %d\n", family);
      goto error;
      break;
    }
  }
  else if (inet_aton(name, &in_addr))
  {
    if (family)
    {
      if (family != AF_INET)
      {
	bk_error_printf(B, BK_ERR_ERR, "Address family mismatch (%d != %d)\n", family, AF_INET);
	goto error;
      }
    }
    family = AF_INET; /* Yes this might be redundant. Leave me alone */
    len = sizeof(struct in_addr);
    BK_FLAG_SET(flags, 0x1);
    addr = &in_addr;
  }
#ifdef HAVE_INET6
  else if (inet_pton(AF_INET6, name, &in6_addr) > 0)
  {
    if (family)
    {
      if (family != AF_INET6)
      {
	bk_error_printf(B, BK_ERR_ERR, "Address family mismatch (%d != %d)\n", family, AF_INET6);
	goto error;
      }
    }

    BK_FLAG_SET(flags, 0x1);
    family = AF_INET6;
    len = sizeof(struct in6_addr);
    addr = &in6_addr;
  }
#endif /* HAVE_INET6 */

  if (BK_FLAG_ISCLEAR(user_flags, BK_GETHOSTBYFOO_FLAG_FQDN) && BK_FLAG_ISSET(flags, 0x1))
  {
    if (addr)
    {
      h = &fake_hostent;
      h->h_addrtype = family;
      h->h_length = len;
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
    if (!(h = gethostbyaddr(((family==AF_INET)?(char *)&in_addr:(char *)&in6_addr), len, family)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not convert %s address: %s\n", (family==AF_INET)?"AF_INET":"AF_INET6",hstrerror(h_errno));
      goto error;
    }
  }
  else
  {
    if (family)
    {
#ifdef HAVE_INET6
      // <BUG ID="881">Switch to RFC 2553 API functions like getaddrinfo and getnameinfo here and everywhere</BUG>
      h = BK_GETHOSTBYNAME2(name, family);
#else
      if (family == AF_INET)
	h = gethostbyname(name);
      else
	h = NULL;
#endif
    }
    else
    {
      if (!(h = gethostbyname(name)))
      {
#ifdef HAVE_INET6
	h = BK_GETHOSTBYNAME2(name, AF_INET6);
	family = AF_INET6; /* Sure this gets set if h==NULL, so what? :-) */
#endif
      }
      else
      {
	family = AF_INET;
      }

      if (!h)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not convert %s hostname: %s\n", (family==AF_INET)?"AF_INET":"AF_INET6",hstrerror(h_errno));
	goto error;
      }
    }
  }

  if (!h)
  {
    bk_error_printf(B, BK_ERR_ERR, "Hostname lookup failed: %s.\n", hstrerror(h_errno));
    goto error;
  }

  if (copy_hostent(B,&tmp_h,h) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not copy hostent\n");
    goto error;
  }

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
  if (!(bgs = bgs_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bgs: %s\n", strerror(errno));
    goto error;
  }
  /*
   * Do *not* pass off anything here, enqueue_delta can still fail and will
   * have to free stuff and callback the user
   */
  bgs->bgs_hostent = tmp_h;
  bgs->bgs_callback = callback;
  bgs->bgs_args = args;
  bgs->bgs_flags = flags;
  bgs->bgs_bni = bni;
  bgs->bgs_run = run;

  if (bk_run_enqueue_delta(B, run, 0, gethostbyfoo_callback, bgs, &bgs->bgs_event, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not enqueue gethostbyfoo callback\n");
    goto error;
  }

  BK_RETURN(B,bgs);

 error:
  /* Callback *might* not be set */
  if (callback)
  {
    (*callback)(B, run, tmp_h, bni, args, BkGetHostByFooStateErr);
  }
  if (tmp_h) bk_destroy_hostent(B, tmp_h);
  if (bgs) bgs_destroy(B,bgs);
  BK_RETURN(B,NULL);
}



/**
 * Create a bgs
 *	@param B BAKA thread/global state.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a bk_gethostbyfoo_state on success.
 */
static struct bk_gethostbyfoo_state *
bgs_create(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_gethostbyfoo_state *bgs = NULL;

  if (!(BK_CALLOC(bgs)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bgs: %s\n", strerror(errno));
    goto error;
  }

  BK_RETURN(B, bgs);

 error:
  if (bgs) bgs_destroy(B, bgs);
  BK_RETURN(B, NULL);
}



/**
 * Destroy a @a bk_gethostbyfoo_state.
 *	@param B BAKA thread/global state.
 *	@param bgs @a bk_gethostbyfoo_state to destroy.
 */
static void
bgs_destroy(bk_s B, struct bk_gethostbyfoo_state *bgs)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bgs)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (bgs->bgs_hostent)
    bk_destroy_hostent(B, bgs->bgs_hostent);

  if (bgs->bgs_event)
    bk_run_dequeue(B, bgs->bgs_run, bgs->bgs_event, 0);

  free(bgs);

  BK_VRETURN(B);
}



/**
 * Public interface for bgs_destroy
 *
 *	@param B BAKA thread/global state.
 *	@param bgs @a bk_gethostbyfoo_state to destroy
 */
void
bk_gethostbyfoo_abort(bk_s B, void *opaque)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_gethostbyfoo_state *bgs;

  if (!(bgs = opaque))
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  bgs_destroy(B, bgs);
  BK_VRETURN(B);
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

  if (h->h_name) free((char *)h->h_name);

  if (h->h_aliases)
  {
    for(s = h->h_aliases; *s; s++)
    {
      free(*s);
    }
    free(h->h_aliases);
  }

  if (h->h_addr_list)
  {
    for (s = h->h_addr_list; *s; s++)
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
  struct hostent *n = NULL;
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

  *ih = NULL;

  if (h->h_name && !(n->h_name = strdup(h->h_name)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup h_name: %s\n", strerror(errno));
    goto error;
  }

  if (h->h_aliases)
  {
    for(count = 0,s = h->h_aliases; *s; s++)
      count++;
    if (!(n->h_aliases = calloc(count+1, sizeof(*s))))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not calloc aliases: %s\n", strerror(errno));
      goto error;
    }

    for(c = 0; c<count;c++)
    {
      if (!(n->h_aliases[c] = strdup(h->h_aliases[c])))
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
      for (count = 1,ia = (struct in_addr **)(h->h_addr_list); *ia; ia++)
	count++;
    }
#ifdef HAVE_INET6
    else if (h->h_addrtype == AF_INET6)
    {
      struct in6_addr **ia;
      for (count = 0,ia = (struct in6_addr **)(h->h_addr_list); *ia; ia++)
	count++;
    }
#endif
    else
    {
      bk_error_printf(B, BK_ERR_ERR, "Unknown address family: %d\n", h->h_addrtype);
      goto error;
    }

    if (!(n->h_addr_list = calloc(count+1, sizeof(*n->h_addr_list))))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate addr_list: %s\n", strerror(errno));
      goto error;
    }

    /* We *should* be able to do this one memmove, but this is safer. */
    for(c = 0; c<count; c++)
    {
      if (h->h_addr_list[c])
      {
	if (!(n->h_addr_list[c] = malloc(h->h_length)))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not allocate space for addr: %s\n", strerror(errno));
	  goto error;
	}
	memmove(n->h_addr_list[c], h->h_addr_list[c], h->h_length);
      }
      else
	n->h_addr_list[c] = NULL;
    }
  }

  *ih = n;

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
gethostbyfoo_callback(bk_s B, struct bk_run *run, void *args, const struct timeval starttime, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_gethostbyfoo_state *bgs = args;
  bk_gethostbyfoo_state_e state = BkGetHostByFooStateOk;

  if (!run || !bgs)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  /* First null out event so we don't try to dequeue it later */
  bgs->bgs_event = NULL;

  if (bgs->bgs_bni && bgs->bgs_hostent)
  {
    if (bk_netinfo_update_hostent(B, bgs->bgs_bni, bgs->bgs_hostent) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not update netinfo with hostent\n");
      state = BkGetHostByFooStateNetinfoErr;
    }
  }

  /* We're insisting on a callback */
  (*bgs->bgs_callback)(B, run, bgs->bgs_hostent, bgs->bgs_bni, bgs->bgs_args, state);

  bgs_destroy(B,bgs);
  BK_VRETURN(B);
}



/**
 * Convert an ethernet address from strings to struct either
 *
 *	@param B BAKA thread/global state.
 *	@param ether_addr_str String version of ether address
 *	@param ether_addr C/O ether address
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_ether_aton(bk_s B, const char *ether_addr_str, struct ether_addr *ether_addr, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char **ether_toks = NULL;
  int cnt;

  if (!ether_addr_str || !ether_addr)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!(ether_toks = bk_string_tokenize_split(B, ether_addr_str, 0, ":", NULL, NULL, NULL, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not tokenize the ether address\n");
    goto error;
  }

  for(cnt = 0; cnt < 6; cnt++)
  {
    u_int32_t element = 0;
    char hex_char[2];
    u_int32_t cnt2;
    u_int32_t len = strlen(ether_toks[cnt]);

    if (len > 2)
    {
      bk_error_printf(B, BK_ERR_ERR, "Element '%s' of ethernet address is invalid\n", ether_toks[cnt]);
      goto error;
    }

    hex_char[1] = '\0';
    for(cnt2 = 0; cnt2 < len; cnt2++)
    {
      int hex;
      hex_char[0] = ether_toks[cnt][cnt2];
      if (bk_string_atou32(B, hex_char, &hex, BK_STRING_ATOI_FLAG_HEX) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not convert ether address element to a value\n");
	goto error;
      }
      element = ((element<<4) | (hex&0xf))&0xff;
    }
    element &= 0xff; // Just be certain
    ether_addr->ether_addr_octet[cnt] = element;
  }

  bk_string_tokenize_destroy(B, ether_toks);
  BK_RETURN(B, 0);

 error:
  if (ether_toks)
    bk_string_tokenize_destroy(B, ether_toks);
  BK_RETURN(B, -1);
}



/**
 * Convert an ethernet address to a string. All elements will be 2 characters long
 *
 *	@param B BAKA thread/global state.
 *	@param ether_addr The ethernet address to convert
 *	@param ether_addr_string C/O address as string. You must allocate space (18 chars).
 *	@param flags BK_ETHER_NTOA_FLAG_UPPER: produce resuts in upper case
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_ether_ntoa(bk_s B, struct ether_addr *ether_addr, char *ether_addr_str, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int ret;

  if (!ether_addr || !ether_addr_str)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (BK_FLAG_ISSET(flags, BK_ETHER_NTOA_FLAG_UPPER))
  {
    ret = snprintf(ether_addr_str, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
		   ether_addr->ether_addr_octet[0],
		   ether_addr->ether_addr_octet[1],
		   ether_addr->ether_addr_octet[2],
		   ether_addr->ether_addr_octet[3],
		   ether_addr->ether_addr_octet[4],
		   ether_addr->ether_addr_octet[5]);
  }
  else
  {
    ret = snprintf(ether_addr_str, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
		   ether_addr->ether_addr_octet[0],
		   ether_addr->ether_addr_octet[1],
		   ether_addr->ether_addr_octet[2],
		   ether_addr->ether_addr_octet[3],
		   ether_addr->ether_addr_octet[4],
		   ether_addr->ether_addr_octet[5]);
  }

  if (ret != 17)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not convert ethernet address to string: %s\n", strerror(errno));
    goto error;
  }

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);
}
