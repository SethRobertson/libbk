#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_netutils.c,v 1.12 2002/01/24 08:54:43 dupuy Exp $";
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

/**
 * @file 
 * Random network utility functions. This a sort of a catch-all file
 * (though clearly each functino should have somethig to do with network
 * code.
 */

#include <libbk.h>
#include "libbk_internal.h"



#define BK_ENDPT_SPEC_PROTO_SEPARATOR	"://"	///< URL protocol separator
#define BK_ENDPT_SPEC_SERVICE_SEPARATOR	":"	///< URL service separator



/** 
 * All the state which must be saved during hostname determination in order
 * to pass onto bk_net_init()
 */
struct start_service_state
{
  bk_flags			sss_flags;	///< Everyone needs flags.
  struct bk_netinfo *  		sss_lbni;	///< bk_netinfo.
  struct bk_netinfo *  		sss_rbni;	///< bk_netinfo.
  bk_bag_callback_f		sss_callback;	///< User callback.
  void *			sss_args;	///< User args.
  char *			sss_securenets;	///< Securenets info.
  int				sss_backlog;	///< Listen backlog.
  char *			sss_host;	///< Space to save a hostname.
  u_long			sss_timeout;	///< Timeout
};



static struct start_service_state *sss_create(bk_s B);
static void sss_destroy(bk_s B, struct start_service_state *sss);
static void sss_serv_gethost_complete(bk_s B, struct bk_run *run , struct hostent *h, struct bk_netinfo *bni, void *args, bk_gethostbyfoo_state_e state);
static void sss_connect_rgethost_complete(bk_s B, struct bk_run *run, struct hostent *h, struct bk_netinfo *bni, void *args, bk_gethostbyfoo_state_e state);
static void sss_connect_lgethost_complete(bk_s B, struct bk_run *run, struct hostent *h, struct bk_netinfo *bni, void *args, bk_gethostbyfoo_state_e state);




/**
 * Get the length of a sockaddr (not every OS supports sa_len);
 *
 *	@param B BAKA thread/global state.
 *	@param sa @a sockaddr 
 *	@return <i>-1</i> on failure.<br>
 *	@return @a sockaddr <i>length</i> on success.
 */
int
bk_netutils_get_sa_len(bk_s B, struct sockaddr *sa)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int len;

  if (!sa)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

#ifdef HAVE_SOCKADDR_SA_LEN
  len = sa->sa_len;
#else
#ifdef HAVE_SA_LEN_MACRO
  len = SA_LEN(sa);
#else
  switch (sa->sa_family)
  {
  case AF_INET:
    len = sizeof(struct sockaddr_in);
    break;

#ifdef AF_INET6
  case AF_INET6:
    len = sizeof(struct sockaddr_in6);
    break;
#endif /* AF_INET6 */

#ifdef AF_LOCAL
  case AF_LOCAL:
    {
#ifdef SUN_LEN					// POSIX.1g un.h defines this
      len = SUN_LEN((struct sockaddr_un *)sa);
#else
      size_t off = (size_t)((struct sockaddr_un *)0)->sun_path;
      len = off + bk_strnlen(B, ((struct sockaddr_un *)sa)->sun_path,
			     sizeof(struct sockaddr_un) - off);
#endif /* !SUN_LEN */
    }
    break;
#endif /* AF_LOCAL */

  default:
    bk_error_printf(B, BK_ERR_ERR, "Address family %d is not supported\n",
		    sa->sa_family);
    len = -1;
    break;
  }
#endif /* !HAVE_SA_LEN_MACRO */
#endif /* !HAVE_SOCKADDR_SA_LEN */

  BK_RETURN(B, len);
}



/**
 * Parse a BAKA URL endpoint specifier. Copy out host string, service string
 * and protocol string in *allocated* memory (use free(2) to free). If no
 * entry is found for one of the copyout values the supplied default (if
 * any) is returned, otherwise NULL. Hostspecifiers/BAKA endpoints appear very much like
 * URL's. [PROTO://][HOST][:SERVICE]. You will note that in theory the empty
 * string is legal and this function will not *complain* if it finds no
 * data. It is assumed that the caller will have specified at least a
 * default host string (like BK_ADDR_ANY) or default service. NB If the
 * host string is empty @a hoststr will be *NULL* (not empty).
 * NB: This is a @a friendly function not public.
 *
 *	@param B BAKA thread/global state.
 *	@param url Host specifier ("url" is shorter :-))
 *	@param hoststr Copy out host string.
 *	@param defhoststr Default host string.
 *	@param serivcestr Copy out service string.
 *	@param defservicestr Default service string.
 *	@param protostr Copy out protocol string.
 *	@param defprotostr Default protocol string.
 *	@return <i>-1</i> with @a hoststr, @a servicestr, and @a protostr
 *	set to NULL on failure.<br>
 *	@return <i>0</i> with @a hoststr, @a servicestr, and @a protostr
 *	appropriately set on success.
 */
int
bk_parse_endpt_spec(bk_s B, char *urlstr, char **hoststr, char *defhoststr, char **servicestr,  char *defservicestr, char **protostr, char *defprotostr)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *host, *service, *proto;	/* Temporary versions */
  char *protoend;
  char *url = NULL;

  if (!urlstr || !hoststr || !servicestr || !protostr)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  /* Init. copyout args (so we don't free random stuff in error case) */
  *hoststr = NULL;
  *servicestr = NULL;
  *protostr = NULL;

  /* Make modifiable copy */
  if (!(url = strdup(urlstr)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup urlstr: %s\n", strerror(errno));
    goto error;
  }

  proto = url;

  /* Look for protocol specifier and set host to point *after* it. */
  if ((protoend = strstr(proto, BK_ENDPT_SPEC_PROTO_SEPARATOR)))
  {
    *protoend = '\0';
    host = protoend + strlen(BK_ENDPT_SPEC_PROTO_SEPARATOR);
  }
  else
  {
    proto = NULL;
    host = url;
  }

  /* If not found or empty, use default */
  if (!proto || BK_STREQ(proto,""))
  {
    proto = defprotostr;
  }

  /* Copyout proto */
  if (proto && !(*protostr = strdup(proto)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup protostr: %s\n", strerror(errno));
    goto error;
  }

  /* Find service info */
  if ((service = strstr(host, BK_ENDPT_SPEC_SERVICE_SEPARATOR)))
  {
    *service++ = '\0';
  }

  /* If not found, or empty use default */
  if (!service || BK_STREQ(service,""))
  {
    service = defservicestr;
  }

  /* Copy service if available. */
  if (service && !(*servicestr = strdup(service)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup service: %s\n", strerror(errno));
    goto error;
  }

  /* Host should be pointing to right thing */

  /* If host exists (which it *always* should) but is empty, use default */
  if (host && BK_STREQ(host, ""))
  {
    host = defhoststr;
  }

  /* Copy host if available. */
  if (host && !(*hoststr = strdup(host)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup host: %s\n", strerror(errno));
    goto error;
  }

  if (url) free (url);

  BK_RETURN(B,0);

 error:
  if (url) free(url);
  if (*hoststr) free (*hoststr);
  *hoststr = NULL;
  if (*servicestr) free (*servicestr);
  *servicestr = NULL;
  if (*protostr) free (*protostr);
  *protostr = NULL;
  BK_RETURN(B,-1);
}




/**
 * Parse a "default" url. This is nothing more than a convience wrapper
 * around @a bk_parse_endpt_spec which parses out a url with no defaults
 * (an example would be the default url itself)
 *
 *	@param url Host specifier ("url" is shorter :-))
 *	@param hoststr Copy out host string.
 *	@param serivcestr Copy out service string.
 *	@param protostr Copy out protocol string.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_parse_endpt_no_defaults(bk_s B, char *urlstr, char **hostname, char **servistr, char **protostr)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!urlstr || !hostname || !servistr || !protostr)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  BK_RETURN(B,bk_parse_endpt_spec(B, urlstr, hostname, NULL, servistr, NULL, protostr, NULL));
}



/**
 * Make a service with a user friendly string based interface (verbose
 * format). The def* arguments will replace unfound elements of the url and
 * so should be defined when possible or you risk having an incompletely
 * specified local side.
 *
 *
 *	@param B BAKA thread/global state.
 *	@param run The @a bk_run structure.
 *	@param url The local endpoint specification (may be NULL).
 *	@param defhostsstr Host string to use if host part of url is not found. (may be NULL).
 *	@param defservstr Service string to use if service part of url is not found. (may be NULL).
 *	@param protostr Protocol string to use if protocol part of url is not found. (may be NULL).
 *	@param sercurenets Address based security specification.
 *	@param callback Function to call when start is complete.
 *	@param args User args for @a callback.
 *	@param backlog Server @a listen(2) backlog
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_netutils_start_service_verbose(bk_s B, struct bk_run *run, char *url, char *defhoststr, char *defservstr, char *defprotostr, char *securenets, bk_bag_callback_f callback, void *args, int backlog, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *hoststr = NULL;
  char *servstr = NULL;
  char *protostr = NULL;
  struct bk_netinfo *bni = NULL;
  struct start_service_state *sss = NULL;
  void *ghbf_info;

  if (!run || !callback)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  /* Conver NULL to a real empty url */
  if (!url)
    url="";

  if (bk_parse_endpt_spec(B, url, &hoststr, defhoststr?defhoststr:BK_ADDR_ANY, &servstr, defservstr?defservstr:"0", &protostr, defprotostr?defprotostr:"tcp")<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not convert endpoint specifier\n");
    goto error;
  }

  if (securenets)
  {
    bk_error_printf(B, BK_ERR_WARN, "Securenets are not yet implemented, Caveat emptor.\n");
  }
  
  if (!hoststr || !servstr || !protostr)
  {
    bk_error_printf(B, BK_ERR_ERR, "Must specify all three of host/service/protocol\n");
    goto error;
  }

  if (!(bni = bk_netinfo_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create bk_netinfo\n");
    goto error;
  }

  if (bk_getservbyfoo(B, servstr, protostr, NULL, bni, 0)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set the service information\n");
    goto error;
  }

  if (!(sss = sss_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create sss\n");
    goto error;
  }

  sss->sss_flags = flags;
  sss->sss_lbni = bni;
  bni = NULL;
  sss->sss_callback = callback;
  callback = NULL;
  sss->sss_args = args;
  sss->sss_securenets = securenets;
  securenets = NULL;
  sss->sss_backlog = backlog;

  if (!(ghbf_info = bk_gethostbyfoo(B, hoststr, 0, sss->sss_lbni, run, sss_serv_gethost_complete, sss, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not begin the hostname lookup process\n");
    /* sss is destroyed */
    sss = NULL;
    goto error;
  }

  // <TODO>Return ghbf_info to the user somehow here point once there is a start_service_preservice_cancel--sss is not helpful.</TODO>

  if (hoststr) free(hoststr);
  if (servstr) free(servstr);
  if (protostr) free(protostr);
  BK_RETURN(B,0);
  
 error:
  if (hoststr) free(hoststr);
  if (servstr) free(servstr);
  if (protostr) free(protostr);
  if (bni) bk_netinfo_destroy(B, bni);
  if (sss) sss_destroy(B, sss);
  BK_RETURN(B,-1);
  
}



/**
 * Start service in short format.
 *	@param B BAKA thread/global state.
 *	@param url The local endpoint specification (may be NULL).
 *	@param defurl The <em>default</em> local endpoint specification (may be NULL).
 *	@param callback Function to call when start is complete.
 *	@param args User args for @a callback.
 *	@param backlog Server @a listen(2) backlog
 *	@param flags Flags for future use.
 *	@param run The @a bk_run structure.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_netutils_start_service(bk_s B, struct bk_run *run, char *url, char *defurl, bk_bag_callback_f callback, void *args, int backlog, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *defhoststr = NULL;
  char *defservstr = NULL;
  char *defprotostr = NULL;
  int ret;
  
  if (!run || !(url || defurl))
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!url)
    url="";

  if (!defurl)
    defurl="";

  if (((ret=bk_parse_endpt_no_defaults(B, defurl, &defhoststr, &defservstr, &defprotostr)) < 0) ||
      ((ret=bk_netutils_start_service_verbose(B, run, url, defhoststr, defservstr, defprotostr, NULL, callback, args, backlog, flags)) < 0))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not start service\n");
  }
  
  if (defhoststr) free(defhoststr);
  if (defservstr) free(defservstr);
  if (defprotostr) free(defprotostr);
  
  BK_RETURN(B,ret);
}



/**
 * Continue trying to set up a service following hostname determination.
 *	@param B BAKA thread/global state.
 *	@param run The @a bk_run structure.
 *	@param h filled out @a hostent
 *	@param bni Caller's @a bk_netinfo
 *	@param args @a my state.
 *	@param state State of the @a bk_gethostbyfoo call
 */
static void
sss_serv_gethost_complete(bk_s B, struct bk_run *run , struct hostent *h, struct bk_netinfo *bni, void *args, bk_gethostbyfoo_state_e state)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct start_service_state *sss=args;

  if (!run || !h || !bni || !sss)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }

  switch (state)
  {
  case BkGetHostByFooStateOk:
    break;
  case BkGetHostByFooStateErr:
    bk_error_printf(B, BK_ERR_ERR, "System error detected while determining server hostname\n");
    goto error;
    break;
  case BkGetHostByFooStateNetinfoErr:
    bk_error_printf(B, BK_ERR_ERR, "Error in updating (required) bni while determining server hostname\n");
    goto error;
    break;
  default:
    bk_error_printf(B, BK_ERR_ERR, "Unknown bk_gethostbyfoo state\n");
    break;
  }

  // <TODO> pass securenets here </TODO>
  if (bk_net_init(B, run, sss->sss_lbni, NULL, 0, sss->sss_flags, sss->sss_callback, sss->sss_args, sss->sss_backlog) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not open service\n");
    goto error;
  }
  
  sss_destroy(B, sss);
  BK_VRETURN(B);

 error:  
  if (sss)
  {
    /* This *really* sucks, we have to call back the user */
    if (sss->sss_callback)
    {
      (*(sss->sss_callback))(B, sss->sss_args, -1, NULL, NULL,bk_net_init_sys_error(B,errno));
      sss->sss_callback=NULL;
    }
    sss_destroy(B,sss);
  }
  BK_VRETURN(B);
}



/**
 * Create a start_service_state structure.
 *	@param B BAKA thread/global state.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new sss on success.
 */
static struct start_service_state *
sss_create(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct start_service_state *sss;
  
  if (!(BK_CALLOC(sss)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate sss: %s\n", strerror(errno));
    goto error;
  }
  
  BK_RETURN(B,sss);

 error:
  BK_RETURN(B,NULL);
}



/**
 * Destroy a @a start_service_state
 *	@param B BAKA thread/global state.
 *	@param sss The @a start_service_state to destroy.
 */
static void
sss_destroy(bk_s B, struct start_service_state *sss)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!sss)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }
  
  if (sss->sss_host) free(sss->sss_host);
  if (sss->sss_lbni) bk_netinfo_destroy(B,sss->sss_lbni);
  if (sss->sss_rbni) bk_netinfo_destroy(B,sss->sss_rbni);
  free(sss);

  BK_VRETURN(B);
}




/**
 * Start up a connection with a user friendly interface.
 *
 *
 *	@param B BAKA thread/global state.
 *	@param run The @a bk_run structure.
 *	@param rurl The remoe endpoint specification (may be NULL).
 *	@param rdefhostsstr Remote host string to use if host part of url is not found. (may be NULL).
 *	@param rdefservstr Remote service string to use if service part of url is not found. (may be NULL).
 *	@param rprotostr Remote protocol string to use if protocol part of url is not found. (may be NULL).
 *	@param lurl The local endpoint specification (may be NULL).
 *	@param ldefhostsstr Local host string to use if host part of url is not found. (may be NULL).
 *	@param ldefservstr Local service string to use if service part of url is not found. (may be NULL).
 *	@param lprotostr Local protocol string to use if protocol part of url is not found. (may be NULL).
 *	@param sercurenets Address based security specification.
 *	@param timeout Abort connection after @a timeout seconds.
 *	@param callback Function to call when start is complete.
 *	@param args User args for @a callback.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_netutils_make_conn_verbose(bk_s B, struct bk_run *run,
			      char *rurl, char *defrhost, char *defrserv,
			      char *lurl, char *deflhost, char *deflserv,
			      char *defproto, u_long timeout, bk_bag_callback_f callback, void *args, bk_flags flags )
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *rhoststr = NULL;
  char *rservstr = NULL;
  char *rprotostr = NULL;
  char *lhoststr = NULL;
  char *lservstr = NULL;
  char *lprotostr = NULL;
  struct bk_netinfo *rbni = NULL;
  struct bk_netinfo *lbni = NULL;
  struct start_service_state *sss = NULL;
  void *ghbf_info;


  if (!run || !rurl || !callback)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    goto error;
  }

  /* Parse out the remote "URL" */
  if (bk_parse_endpt_spec(B, rurl, &rhoststr, defrhost, &rservstr, defrserv, &rprotostr, defproto?defproto:"tcp")<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not convert remote endpoint specify\n");
    goto error;
  }

  /* If anything is unset, die */
  if (!rhoststr || !rservstr || !rprotostr)
  {
    bk_error_printf(B, BK_ERR_ERR, "Must specify all three of remote host/service/protocol\n");
    goto error;
  }
  
  if (!deflhost) deflhost = BK_ADDR_ANY;
  if (!deflserv) deflserv = "0";
  if (!defproto) defproto = "tcp";

  /* Parse out the local side "URL" */
  if (bk_parse_endpt_spec(B, lurl?lurl:"", &lhoststr, deflhost?deflhost:BK_ADDR_ANY, &lservstr, deflserv, &lprotostr, defproto)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not convert remote endpoint specify\n");
    goto error;
  }

  /* If the protocol is unset, use the remote protocol */
  if (!lprotostr && !(lprotostr = strdup(rprotostr)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup protocol from remote: %s\n", strerror(errno));
    goto error;
  }

  /* Die on unset things */
  if (!lhoststr || !lservstr || !lprotostr)
  {
    bk_error_printf(B, BK_ERR_ERR, "Must specify all three of remote host/service/protocol\n");
    goto error;
  }

  /* Create remote netinfo */
  if (!(rbni = bk_netinfo_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create remote bni: %s\n", strerror(errno));
    goto error;
  }

  /* Create local netinfo */
  if (!(lbni = bk_netinfo_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create local bni: %s\n", strerror(errno));
    goto error;
  }

  /* Set remote service information */
  if (bk_getservbyfoo(B, rservstr, rprotostr, NULL, rbni, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set the remote service information\n");
    goto error;
  }

  /* Set local service information */
  if (lservstr && bk_getservbyfoo(B, lservstr, lprotostr, NULL, lbni, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set the local service information\n");
    goto error;
  }

  /* If remote and local protocol info aren't the same, die */
  if (rbni->bni_bpi->bpi_proto != lbni->bni_bpi->bpi_proto)
  {
    bk_error_printf(B, BK_ERR_ERR, "Local and remote protocol specifications must match ([local proto num]%d != [remote proto num]%d\n", rbni->bni_bpi->bpi_proto, lbni->bni_bpi->bpi_proto);
    goto error;
  }

  if (!(sss = sss_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate sss: %s\n", strerror(errno));
    goto error;
  }

  /* 
   * In the following "pass off" means that the sss struct and the
   * callbacks for which it saves state are assuming control of these
   * puppies and will free them if required,
   */
  sss->sss_flags = flags;
  sss->sss_rbni = rbni;				/* Pass off rbni */
  rbni = NULL;
  sss->sss_lbni = lbni;				/* Pass off lbni */
  lbni = NULL;
  sss->sss_callback = callback;			/* Pass off callback */
  /* 
   * From here on we do *not* want to manually call callback on error since
   * it will have already been called.
   */
  callback = NULL;
  sss->sss_args = args;
  sss->sss_host = lhoststr;			/* Pass off lhoststr */
  lhoststr=NULL;
  sss->sss_timeout = timeout;

  if (!(ghbf_info = bk_gethostbyfoo(B, rhoststr, 0, sss->sss_rbni, run, sss_connect_rgethost_complete, sss, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not start search for remote hostname\n");
    /* sss is destroyed */
    sss = NULL;
    goto error;
  }

  // <TODO>ghbf_info needs to be returned, though some other structure (other than sss) to user at some point, so that make_conn_preconn_cancel can do something, when it is written</TODO>
    
 done:
  if (rhoststr) free(rhoststr);
  if (rservstr) free(rservstr);
  if (rprotostr) free(rprotostr);
  if (lhoststr) free(lhoststr);
  if (lservstr) free(lservstr);
  if (lprotostr) free(lprotostr);
  BK_RETURN(B,0);
  
 error:
  if (callback)
  {
    (*(callback))(B, args, -1, NULL, NULL, bk_net_init_sys_error(B,errno));
  }
  if (rhoststr) free(rhoststr);
  if (rservstr) free(rservstr);
  if (rprotostr) free(rprotostr);
  if (lhoststr) free(lhoststr);
  if (lservstr) free(lservstr);
  if (lprotostr) free(lprotostr);
  if (lbni) bk_netinfo_destroy(B, lbni);
  if (rbni) bk_netinfo_destroy(B, rbni);
  if (sss) sss_destroy(B,sss);
  BK_RETURN(B,-1);
}



/**
 * Start service in short format.
 *	@param B BAKA thread/global state.
 *	@param url The local endpoint specification (may be NULL).
 *	@param defurl The <em>default</em> local endpoint specification (may be NULL).
 *	@param timeout Abort connection after @a timeout seconds.
 *	@param callback Function to call when start is complete.
 *	@param args User args for @a callback.
 *	@param flags Flags for future use.
 *	@param run The @a bk_run structure.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_netutils_make_conn(bk_s B, struct bk_run *run, char *url, char *defurl, u_long timeout, bk_bag_callback_f callback, void *args, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *defhoststr = NULL;
  char *defservstr = NULL;
  char *defprotostr = NULL;
  int ret;
  
  if (!run || !(url || defurl))
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (!url)
    url="";

  if (!defurl)
    defurl="";

  if (((ret=bk_parse_endpt_no_defaults(B, defurl, &defhoststr, &defservstr, &defprotostr)) < 0) ||
      ((ret=bk_netutils_make_conn_verbose(B, run, url, defhoststr, defservstr, defprotostr, NULL, NULL, NULL, timeout, callback, args, flags)) < 0))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not start connection\n");
  }
  
  if (defhoststr) free(defhoststr);
  if (defservstr) free(defservstr);
  if (defprotostr) free(defprotostr);
  
  BK_RETURN(B,ret);
}



/**
 * Finish up the first the hostname search
 *
 *	@param B BAKA thread/global state.
 *	@param run The @a bk_run structure.
 *	@param h filled out @a hostent
 *	@param bni Caller's @a bk_netinfo
 *	@param args @a my state.
 *	@param state State of the @a bk_gethostbyfoo call
 *	@param B BAKA thread/global state.
 */
static void
sss_connect_rgethost_complete(bk_s B, struct bk_run *run, struct hostent *h, struct bk_netinfo *bni, void *args, bk_gethostbyfoo_state_e state)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct start_service_state *sss = args;

  if (!run || !h || !bni || !sss)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    goto error;
  }

  switch (state)
  {
  case BkGetHostByFooStateOk:
    break;
  case BkGetHostByFooStateErr:
    bk_error_printf(B, BK_ERR_ERR, "System error detected while determining remote hostname\n");
    goto error;
    break;
  case BkGetHostByFooStateNetinfoErr:
    bk_error_printf(B, BK_ERR_ERR, "Error in updating (required) bni while determining remote hostname\n");
    goto error;
    break;
  default:
    bk_error_printf(B, BK_ERR_ERR, "Unknown bk_gethostbyfoo state\n");
    break;
  }

  if (!bk_gethostbyfoo(B, sss->sss_host, 0, sss->sss_lbni, run, sss_connect_lgethost_complete, sss, 0))
  {
    bk_error_printf(B, BK_ERR_ERR, "gethostbyfoo failed\n");
    /* All callbacks have occured */
    sss->sss_callback=NULL;
    goto error;
  }

  BK_VRETURN(B);
  
 error:
  if (sss)
  {
    /* This *really* sucks, we have to call back the user */
    if (sss->sss_callback)
    {
      (*(sss->sss_callback))(B, sss->sss_args, -1, NULL, NULL, bk_net_init_sys_error(B,errno));
      sss->sss_callback=NULL;
    }

   sss_destroy(B,sss);
  }
  BK_VRETURN(B);
}




/**
 * Finish up the second the hostname search
 *
 *	@param B BAKA thread/global state.
 *	@param run The @a bk_run structure.
 *	@param h filled out @a hostent
 *	@param bni Caller's @a bk_netinfo
 *	@param args @a my state.
 *	@param state State of the @a bk_gethostbyfoo call
 *	@param B BAKA thread/global state.
 */
static void
sss_connect_lgethost_complete(bk_s B, struct bk_run *run, struct hostent *h, struct bk_netinfo *bni, void *args, bk_gethostbyfoo_state_e state)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct start_service_state *sss = args;

  if (!run || !h || !bni || !sss)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    goto error;
  }

  switch (state)
  {
  case BkGetHostByFooStateOk:
    break;
  case BkGetHostByFooStateErr:
    bk_error_printf(B, BK_ERR_ERR, "System error detected while determining local hostname\n");
    goto error;
    break;
  case BkGetHostByFooStateNetinfoErr:
    bk_error_printf(B, BK_ERR_ERR, "Error in updating (required) bni while determining local hostname\n");
    goto error;
    break;
  default:
    bk_error_printf(B, BK_ERR_ERR, "Unknown bk_gethostbyfoo state\n");
    break;
  }

  if (bk_net_init(B, run, sss->sss_lbni, sss->sss_rbni, sss->sss_timeout, sss->sss_flags, sss->sss_callback, sss->sss_args, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not start connection\n");
    if (sss) sss_destroy(B,sss);
    // Intentionally not goto error
    BK_VRETURN(B);
  }

  sss_destroy(B,sss);
  BK_VRETURN(B);

 error:  
  if (sss)
  {
    /* This *really* sucks, we have to call back the user */
    if (sss->sss_callback)
    {
      (*(sss->sss_callback))(B, sss->sss_args, -1, NULL, NULL, bk_net_init_sys_error(B,errno));
      sss->sss_callback=NULL;
    }
    sss_destroy(B,sss);
  }
  BK_VRETURN(B);
}



