#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_netutils.c,v 1.4 2001/11/20 20:07:14 jtt Exp $";
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
 * Random network utility functions. This a sort of a catch-all file
 * (though clearly each functino should have somethig to do with network
 * code.
 */

/** 
 * All the state which must be saved during hostname determination in order
 * to pass onto bk_net_init()
 */
struct start_service_state
{
  bk_flags			sss_flags;	///< Everyone needs flags.
  struct bk_run *		sss_run;	///< Baka run structure.
  struct bk_netinfo *  		sss_bni;	///< bk_netinfo.
  bk_bag_callback_t		sss_callback;	///< User callback.
  void *			sss_args;	///< User args.
  char *			sss_securenets;	///< Securenets info.
  int				sss_backlog;	///< Listen backlog.
};



static struct start_service_state *sss_create(bk_s B);
static void sss_destroy(bk_s B, struct start_service_state *sss);
static void sss_gethost_complete(bk_s B, struct bk_run *run , struct hostent **h, struct bk_netinfo *bni, void *args);




/**
 * Get the length of a sockaddr (not every OS supports sa_len);
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

  switch (sa->sa_family)
  {
  case AF_INET:
    len=sizeof(struct sockaddr_in);
    break;
  case AF_INET6:
    len=sizeof(struct sockaddr_in6);
    break;
  case AF_LOCAL:
    len=bk_strnlen(B, ((struct sockaddr_un *)sa)->sun_path, sizeof(struct sockaddr_un));
    break;
  default:
    bk_error_printf(B, BK_ERR_ERR, "Address family %d is not suppored\n", sa->sa_family);
    goto error;
    break;
  }

  BK_RETURN(B,len);

 error:
  BK_RETURN(B,-1);


}


#define BK_ENDPT_SPEC_PROTO_SEPARATOR	"://"
#define BK_ENDPT_SPEC_SERVICE_SEPARATOR	"/"

/**
 * Parse a BAKA endpoint specifier. Copy out host string, service string
 * and protocol string in *allocated* memory (use free(2) to free). If no
 * entry is found for one of the copyout values the supplied default (if
 * any) is return, otherwise NULL, Hostspecifiers appear very much like
 * URL's. [PROTO://][HOST][/SERVICE]. You will note that in theory the empty
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
  char *url=NULL;


  if (!urlstr || !hoststr || !servicestr || !protostr)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  /* Init. copyout args (so we don't free random stuff in error case) */
  *hoststr=NULL;
  *servicestr=NULL;
  *protostr=NULL;

  /* Make modifiable copy */
  if (!(url=strdup(urlstr)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup urlstr: %s\n", strerror(errno));
    goto error;
  }

  proto=url;

  /* Look for protocol specifier and set host to point *after* it. */
  if ((protoend=strstr(proto, BK_ENDPT_SPEC_PROTO_SEPARATOR)))
  {
    *protoend='\0';
    host=protoend+strlen(BK_ENDPT_SPEC_PROTO_SEPARATOR);
  }
  else
  {
    proto=NULL;
    host=url;
  }

  /* If not found or empty, use default */
  if (!proto || BK_STREQ(proto,""))
  {
    proto=defprotostr;
  }

  /* Copyout proto */
  if (proto && !(*protostr=strdup(proto)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup protostr: %s\n", strerror(errno));
    goto error;
  }

  /* Find service info */
  if ((service=strstr(host, BK_ENDPT_SPEC_SERVICE_SEPARATOR)))
  {
    *service++='\0';
  }

  /* If not found, or empty use default */
  if (!service || BK_STREQ(service,""))
  {
    service=defservicestr;
  }

  /* Copy service if available. */
  if (service && !(*servicestr=strdup(service)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup service: %s\n", strerror(errno));
    goto error;
  }

  /* Host should not be pointing to right thing */

  /* If host exists (which it *always* should) but is empty, use default */
  if (host && BK_STREQ(host, ""))
  {
    host=defhoststr;
  }

  /* Copy host if available. */
  if (host && !(*hoststr=strdup(host)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup host: %s\n", strerror(errno));
    goto error;
  }

  BK_RETURN(B,0);

 error:
  if (url) free(url);
  if (*hoststr) free (*hoststr);
  *hoststr=NULL;
  if (*servicestr) free (*servicestr);
  *servicestr=NULL;
  if (*protostr) free (*protostr);
  *protostr=NULL;
  BK_RETURN(B,-1);
}



/**
 * Make a service with a user friendly string based interface. 
 *	@param B BAKA thread/global state.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_netutils_start_service(bk_s B, struct bk_run *run, char *url, char *defhoststr, char *defservstr, char *defprotostr, char *securenets, bk_bag_callback_t callback, void *args, int backlog, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *hoststr=NULL;
  char *servstr=NULL;
  char *protostr=NULL;
  struct bk_netinfo *bni=NULL;
  struct start_service_state *sss=NULL;

  if (!run || !url || !callback)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (bk_parse_endpt_spec(B, url, &hoststr, defhoststr?defhoststr:BK_ADDR_ANY, &servstr, defservstr, &protostr, defprotostr?defprotostr:"tcp")<0)
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

  if (!(bni=bk_netinfo_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create bk_netinfo\n");
    goto error;
  }

  if (bk_getprotobyfoo(B,protostr, NULL, bni, 0)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set the protocol information\n");
    goto error;
  }

  if (bk_getservbyfoo(B, servstr, bni->bni_bpi->bpi_protostr, NULL, bni, 0)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set the service information\n");
    goto error;
  }

  if (!(sss=sss_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create sss\n");
    goto error;
  }

  sss->sss_flags=flags;
  sss->sss_run=run;
  sss->sss_bni=bni;
  sss->sss_callback=callback;
  sss->sss_args=args;
  sss->sss_securenets=securenets;
  sss->sss_backlog=backlog;

  if (bk_gethostbyfoo(B, hoststr, 0, NULL, bni, run, sss_gethost_complete, sss, 0)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not begin the hostname lookup process\n");
    goto error;
  }
  
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
 * Continue trying to set up a service following hostname determination.
 *	@param B BAKA thread/global state.
 */
static void
sss_gethost_complete(bk_s B, struct bk_run *run , struct hostent **h, struct bk_netinfo *bni, void *args)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct start_service_state *sss;

  /* h is supposed to be NULL */
  if (!run || h || !bni || !(sss=args))
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (bk_net_init(B, run, sss->sss_bni, NULL, 0, sss->sss_flags, sss->sss_callback, sss->sss_args, sss->sss_backlog)<0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not open service\n");
    goto error;
  }
  
  sss_destroy(B, sss);
  BK_VRETURN(B);

 error:  
  sss_destroy(B, sss);
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
  
  free(sss);
  BK_VRETURN(B);
}
