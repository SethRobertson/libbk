#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: test_getbyfoo.c,v 1.8 2001/11/16 22:26:16 jtt Exp $";
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


#define ERRORQUEUE_DEPTH 64			/* Default depth */



/*
 * Information of international importance to everyone
 * which cannot be passed around.
 */
struct global_structure
{
  bk_flags	gs_flags;
  char *	gs_query;
} Global;

#define TESTGETBYFOO_FLAG_QUERY_HOST		0x1
#define TESTGETBYFOO_FLAG_QUERY_SERV		0x2
#define TESTGETBYFOO_FLAG_QUERY_PROTO		0x4
#define TESTGETBYFOO_FLAG_FQDN			0x8
#define TESTGETBYFOO_FLAG_NO_COPYOUT		0x10

int proginit(bk_s B);
void progrun(bk_s B);
static void host_callback(bk_s B, struct bk_run *run, struct hostent **hp, struct bk_netinfo *bni, void *args);



int
main(int argc, char **argv, char **envp)
{
  bk_s B = NULL;				/* Baka general structure */
  BK_ENTRY(B, __FUNCTION__, __FILE__, "SIMPLE");
  char c;
  int getopterr=0;
  extern char *optarg;
  extern int optind;
  poptContext optCon=NULL;
  const struct poptOption optionsTable[] = 
  {
    {"debug", 'd', POPT_ARG_NONE, NULL, 'd', "Turn on debugging", NULL },
    {"proto", 'p', POPT_ARG_STRING, &Global.gs_query, 'p', "Query protocols", "protostr" },
    {"hosts", 'h', POPT_ARG_STRING, &Global.gs_query, 'h', "Query hosts", "hoststr" },
    {"serv", 's', POPT_ARG_STRING, &Global.gs_query, 's', "Query services", "servstr" },
    {"fdqn", 'f', POPT_ARG_NONE, NULL, 'f', "Get FDQN", NULL },
    {"seatbelts", 'S', POPT_ARG_NONE, NULL, 'S', "Seatbelts off", NULL },
    {"no-copyout", 'n', POPT_ARG_NONE, NULL, 'n', "Do not use copyout arguments", NULL },
    POPT_AUTOHELP
    POPT_TABLEEND
  };

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD("BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, BK_ERR_ERR, 0)))
  {
    fprintf(stderr,"Could not perform basic initialization\n");
    exit(254);
  }
  bk_fun_reentry(B);

  memset(&Global,0, sizeof(Global));
  optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, 0);

  while ((c = poptGetNextOpt(optCon)) >= 0)
  {
    switch (c)
    {
    case 'd':
      bk_debug_setconfig(B,BK_GENERAL_DEBUG(B), BK_GENERAL_CONFIG(B),BK_GENERAL_PROGRAM(B));
      bk_error_config(B, BK_GENERAL_ERROR(B), 0, stderr, 0, BK_ERR_DEBUG, BK_ERROR_CONFIG_FH|BK_ERROR_CONFIG_HILO_PIVOT);
      bk_error_dump(B,stderr,NULL,BK_ERR_DEBUG,BK_ERR_ERR,0);
      bk_general_debug_config(B, stderr, BK_ERR_NONE, 0);
      bk_debug_printf(B, "Debugging on\n");
      break;
    case 'p':
      BK_FLAG_SET(Global.gs_flags, TESTGETBYFOO_FLAG_QUERY_PROTO);
      break;
    case 'h':
      BK_FLAG_SET(Global.gs_flags, TESTGETBYFOO_FLAG_QUERY_HOST);
      break;
    case 's':
      BK_FLAG_SET(Global.gs_flags, TESTGETBYFOO_FLAG_QUERY_SERV);
      break;
    case 'f':
      BK_FLAG_SET(Global.gs_flags, TESTGETBYFOO_FLAG_FQDN);
      break;
    case 'n':
      BK_FLAG_SET(Global.gs_flags, TESTGETBYFOO_FLAG_NO_COPYOUT);
      break;
    case 'S':
      BK_FLAG_CLEAR(BK_GENERAL_FLAGS(B), BK_BGFLAGS_FUNON);
      break;
    default:
      getopterr++;
      break;
    }
  }


  if (c < -1 || getopterr)
  {
    if (c < -1)
    {
      fprintf(stderr, "%s\n", poptStrerror(c));
    }
    poptPrintUsage(optCon, stderr, 0);
    bk_exit(B,254);
  }
    
  if (proginit(B) < 0)
  {
    bk_die(B,254,stderr,"Could not perform program initialization\n",0);
  }

  progrun(B);
  bk_exit(B,0);
  abort();
}



/*
 * Initialization
 */
int proginit(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");

  BK_RETURN(B, 0);
}



/*
 * Normal processing
 */
void progrun(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
  const char *person = "World";
  struct protoent *p=NULL;
  struct servent *s=NULL;
  char **s1;
  struct bk_run *run;
  struct bk_netinfo *bni=NULL;

  if (!(bni=bk_netinfo_create(B)))
  {
    fprintf(stderr,"Could not allocate bni\n");
    exit(1);
  }

  if (BK_FLAG_ISSET(Global.gs_flags, TESTGETBYFOO_FLAG_QUERY_PROTO))
  {
    if (bk_getprotobyfoo(B, Global.gs_query, 
			 BK_FLAG_ISCLEAR(Global.gs_flags, TESTGETBYFOO_FLAG_NO_COPYOUT)?&p:NULL, 
			 bni,BK_GETPROTOBYFOO_FORCE_LOOKUP)<0)
    {
      fprintf(stderr, "Failed to query protocols\n");
      exit(1);
    }

    if (BK_FLAG_ISCLEAR(Global.gs_flags, TESTGETBYFOO_FLAG_NO_COPYOUT))
    {
      printf("Name: %s\n", p->p_name);
      printf("Number: %d\n", p->p_proto);
      printf("Aliases: ");
      if (p->p_aliases)
      {
	for(s1=p->p_aliases; *s1; s1++)
	{
	  printf("%s ", *s1);
	}
	printf("\n");
      }
      else
      {
	printf("NONE\n");
      }
    
      bk_protoent_destroy(B,p);
    }
    else
    {
      printf("Name: %s\n", bni->bni_bpi->bpi_protostr);
      printf("Number: %d\n", bni->bni_bpi->bpi_proto);
      printf("Aliases: N/A\n");
    }
  }
  else if (BK_FLAG_ISSET(Global.gs_flags, TESTGETBYFOO_FLAG_QUERY_SERV))
  {
    if (bk_getservbyfoo(B, Global.gs_query, "tcp", 
			(BK_FLAG_ISCLEAR(Global.gs_flags, TESTGETBYFOO_FLAG_NO_COPYOUT)?&s:NULL),
			bni, BK_GETSERVBYFOO_FORCE_LOOKUP)<0)
    {
      fprintf(stderr, "Failed to query services\n");
      exit(1);
    }

    if (BK_FLAG_ISCLEAR(Global.gs_flags, TESTGETBYFOO_FLAG_NO_COPYOUT))
    {
      printf("Name: %s\n", s->s_name);
      printf("Number: %u\n", ntohs(s->s_port));
      printf("Protocol: %s\n", s->s_proto);
      printf("Aliases: ");
      if (s->s_aliases)
      {
	for(s1=s->s_aliases; *s1; s1++)
	{
	  printf("%s ", *s1);
	}
	printf("\n");
      }
      else
      {
	printf("NONE\n");
      }
    
      bk_servent_destroy(B,s);
    }
    else
    { 
      printf("Name: %s\n", bni->bni_bsi->bsi_servstr);
      printf("Number: %u\n", ntohs(bni->bni_bsi->bsi_port));
      printf("Protocol: tcp\n");
      printf("Aliases: N/A\n");
    }
  }
  else if (BK_FLAG_ISSET(Global.gs_flags, TESTGETBYFOO_FLAG_QUERY_HOST))
  {
    struct hostent **h;
    struct hostent dummy={NULL, NULL, 2, 4, NULL};

    if (!(run=bk_run_init(B,0)))
    {
      fprintf(stderr,"Could not initialize bk_run\n");
      exit(1);
    }
    if(!(h=malloc(sizeof(*h))))
    {
      fprintf(stderr,"Could not allocate h: %s\n", strerror(errno));
      exit(1);
    }
    *h=&dummy;
    if (*h && BK_FLAG_ISCLEAR(Global.gs_flags, TESTGETBYFOO_FLAG_NO_COPYOUT))
    {
      printf("h is now %p\n", h);
    }

    if (bk_gethostbyfoo(B, Global.gs_query, 0, 
			BK_FLAG_ISCLEAR(Global.gs_flags, TESTGETBYFOO_FLAG_NO_COPYOUT)?h:NULL, 
			bni, run, host_callback, NULL,
			((BK_FLAG_ISSET(Global.gs_flags,TESTGETBYFOO_FLAG_FQDN))?BK_GETHOSTBYFOO_FLAG_FQDN:0))<0)
    {
      fprintf(stderr,"Could not \"initiate\" gethostbyfoo call\n");
      exit(1);
    }
    if (!*h && BK_FLAG_ISCLEAR(Global.gs_flags, TESTGETBYFOO_FLAG_NO_COPYOUT))
    {
      printf("h is properly NULL\n");
    }
    else
    {
      printf("Regretteby h is %p\n", p);
    }
    bk_run_run(B,run,0);
  }
  
  if (bni)
  {
    printf("bni wound up: %s\n", bni->bni_pretty);
    bk_netinfo_destroy(B,bni);
  }
  BK_VRETURN(B);
}


static void
host_callback(bk_s B, struct bk_run *run, struct hostent **hp, struct bk_netinfo *bni, void *args)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
  struct hostent *h;
  char **s;
  struct bk_netaddr *bna;

  if ((!hp && !bni) || !run || args) /* no args are expected */
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (BK_FLAG_ISCLEAR(Global.gs_flags, TESTGETBYFOO_FLAG_NO_COPYOUT))
  {
    if (!(h=*hp))
    {
      printf("It appears that we had an error\n");
    }
    else
    {
      printf("Name: %s\n", h->h_name);
    
      if (h->h_aliases)
      {
	printf ("Aliases: ");
	for(s=h->h_aliases; *s; s++)
	{
	  printf("%s ", *s);
	}
	printf("\n");
      }
      printf("Addrtype: %d\nLength: %d\n", h->h_addrtype, h->h_length);
    
      printf("Addresses: ");
      if (h->h_addrtype == AF_INET)
      {
	struct in_addr **ia;

	for(ia=(struct in_addr **)(h->h_addr_list); *ia; ia++)
	{
	  char s1[100];
	  printf("%s ", inet_ntop(h->h_addrtype, *ia, s1, 100));
	}
      }
      else if (h->h_addrtype == AF_INET6)
      {
	struct in6_addr **ia;

	printf("Addresses: ");
	for(ia=(struct in6_addr **)(h->h_addr_list); *ia; ia++)
	{
	  char s1[100];
	  printf("%s ", inet_ntop(h->h_addrtype, *ia, s1, 100));
	}
      }
      printf("\n");

      if (!(bna=netinfo_addrs_minimum(bni->bni_addrs)))
      {
	fprintf(stderr, "No address was located in bni");
      }
      else
      {
	bk_netinfo_set_primary_address(B, bni, bna);
	printf("Callback bni is: %s\n", bni->bni_pretty);
      }
      bk_destroy_hostent(B, h);
    }
  }
  else
  {
    int run_once=0;

    for(bna=netinfo_addrs_minimum(bni->bni_addrs);
	bna;
	bna=netinfo_addrs_successor(bni->bni_addrs,bna))
    {
      if (!run_once)
      {
	printf("Addrtype: %d\n", bk_netaddr_nat2af(B, bna->bna_type));
	printf("Length: %d\n", bna->bna_len);
	printf("Address: ");
	bk_netinfo_set_primary_address(B, bni, bna);
	run_once++;
      }
      printf("%s ", bna->bna_pretty);
    }
    printf("\n");
  }
  
  bk_run_set_run_over(B, run);

  BK_VRETURN(B);
}
