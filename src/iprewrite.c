#if !defined(lint) && !defined(__INSIGHT__)
#include "libbk_compiler.h"
UNUSED static const char libbk__rcsid[] = "$Id: iprewrite.c,v 1.1 2008/09/24 20:52:10 jtt Exp $";
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
 *
 * Program to rewrite a pcap file by replacing the ip addresses with a static mapping.
 *
 * Usage: iprewrite [-i in-file] [-o out-file] ipold1:ipnew1 [ipold2:ipnew2 ...]
 */
#include <libbk.h>
#include <libbk_i18n.h>
#include <libbk_net.h>
#include <pcap.h>


#define STD_LOCALEDIR_KEY     "LOCALEDIR"	///< Key in bkconfig to find the locale translation files
#define STD_LOCALEDIR_ENV     "BAKA_HOME"	///< Key in Environment to find base of locale directory
#define STD_LOCALEDIR_DEF     "/usr/local/baka"	///< Default base of where locale directory might be found
#define STD_LOCALEDIR_SUB     "locale"		///< Sub-component from install base where locale might be found
#define ERRORQUEUE_DEPTH      32		///< Default error queue depth



/**
 * @name Defines: netinfo_addrs_clc
 * list of addresses within a @a struct @bk_netinfo.
 * which hides CLC choice.
 */
// @{
#define rewrite_list_create(o,k,f)	dll_create((o),(k),(f))
#define rewrite_list_destroy(h)		dll_destroy(h)
#define rewrite_list_insert(h,o)	dll_insert((h),(o))
#define rewrite_list_insert_uniq(h,n,o)	dll_insert_uniq((h),(n),(o))
#define rewrite_list_append(h,o)	dll_append((h),(o))
#define rewrite_list_append_uniq(h,n,o)	dll_append_uniq((h),(n),(o))
#define rewrite_list_search(h,k)	dll_search((h),(k))
#define rewrite_list_delete(h,o)	dll_delete((h),(o))
#define rewrite_list_minimum(h)		dll_minimum(h)
#define rewrite_list_maximum(h)		dll_maximum(h)
#define rewrite_list_successor(h,o)	dll_successor((h),(o))
#define rewrite_list_predecessor(h,o)	dll_predecessor((h),(o))
#define rewrite_list_iterate(h,d)	dll_iterate((h),(d))
#define rewrite_list_nextobj(h,i)	dll_nextobj((h),(i))
#define rewrite_list_iterate_done(h,i)	dll_iterate_done((h),(i))
#define rewrite_list_error_reason(h,i)	dll_error_reason((h),(i))
// @}

/**
 * Information of international importance to everyone
 * which cannot be passed around.
 */
struct global_structure
{
} Global;


struct rewrite_node
{
  struct in_addr	rn_orig;		///< Orginal pkt address
  struct in_addr	rn_rewr;		///< Rewritten address
  bk_flags		rn_flags;		///< Everyone needs flags.
};



/**
 * Information about basic program runtime configuration
 * which must be passed around.
 */
struct program_config
{
  char *		pc_infile;		///< Input pcap file
  char *		pc_outfile;		///< Output pcap file
  dict_h		pc_rewrites;		///< List of rewrites
  bk_flags		pc_flags;		///< Flags are fun!
#define PC_VERBOSE	0x001			///< Verbose output
};



static int proginit(bk_s B, struct program_config *pc, char **argv, int argc);
static void progrun(bk_s B, struct program_config *pc);
static int replace_ip(bk_s B, struct in_addr *addr, struct program_config *pc, bk_flags flags);



/**
 * Program entry point
 *
 *	@param argc Number of argv elements
 *	@param argv Program name and arguments
 *	@param envp Program environment
 *	@return <i>0</i> Success
 *	@return <br><i>254</i> Initialization failed
 */
int
main(int argc, char **argv, char **envp)
{
  bk_s B = NULL;				/* Baka general structure */
  BK_ENTRY_MAIN(B, __FUNCTION__, __FILE__, "iprewrite");
  int c;
  int getopterr = 0;
  int debug_level = 0;
  char i18n_localepath[_POSIX_PATH_MAX];
  char *i18n_locale;
  struct program_config Pconfig, *pc = NULL;
  poptContext optCon = NULL;
  struct poptOption optionsTable[] =
  {
    {"debug", 'd', POPT_ARG_NONE, NULL, 'd', N_("Turn on debugging"), NULL },
    {"verbose", 'v', POPT_ARG_NONE, NULL, 'v', N_("Turn on verbose message"), NULL },
    {"no-seatbelts", 0, POPT_ARG_NONE, NULL, 0x1000, N_("Sealtbelts off & speed up"), NULL },
    {"seatbelts", 0, POPT_ARG_NONE, NULL, 0x1001, N_("Enable function tracing"), NULL },
    {"profiling", 0, POPT_ARG_STRING, NULL, 0x1002, N_("Enable and write profiling data"), N_("filename") },

    {"in-file", 'i', POPT_ARG_STRING, NULL, 'i', N_("Input pcap file"), N_("infile") },
    {"out-file", 'o', POPT_ARG_STRING, NULL, 'o', N_("Output pcap file"), N_("outfile") },
    POPT_AUTOHELP
    POPT_TABLEEND
  };

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD(NULL, "BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, LOG_LOCAL0, 0)))
  {
    fprintf(stderr,"Could not perform basic initialization\n");
    exit(254);
  }
  bk_fun_reentry(B);

  // Enable error output
  bk_error_config(B, BK_GENERAL_ERROR(B), ERRORQUEUE_DEPTH, stderr, BK_ERR_ERR,
		  BK_ERR_ERR, BK_ERROR_CONFIG_FH |
		  BK_ERROR_CONFIG_SYSLOGTHRESHOLD | BK_ERROR_CONFIG_HILO_PIVOT);

  // i18n stuff
  setlocale(LC_ALL, "");
  if (!(i18n_locale = BK_GWD(B, STD_LOCALEDIR_KEY, NULL)))
  {
    i18n_locale = i18n_localepath;
    snprintf(i18n_localepath, sizeof(i18n_localepath), "%s/%s", BK_ENV_GWD(B, STD_LOCALEDIR_ENV,STD_LOCALEDIR_DEF), STD_LOCALEDIR_SUB);
  }
  bindtextdomain(BK_GENERAL_PROGRAM(B), i18n_locale);
  textdomain(BK_GENERAL_PROGRAM(B));
  for (c = 0; optionsTable[c].longName || optionsTable[c].shortName; c++)
  {
    if (optionsTable[c].descrip) (*((char **)&(optionsTable[c].descrip)))=_(optionsTable[c].descrip);
    if (optionsTable[c].argDescrip) (*((char **)&(optionsTable[c].argDescrip)))=_(optionsTable[c].argDescrip);
  }

  pc = &Pconfig;
  memset(pc, 0, sizeof(*pc));

  pc->pc_infile = "/dev/stdin";
  pc->pc_outfile = "/dev/stdout";

  if (!(optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not initialize options processing\n");
    bk_exit(B, 254);
  }
  // XXX - customize here
  poptSetOtherOptionHelp(optCon, _("[NON-FLAG ARGUMENTS]"));

  while ((c = poptGetNextOpt(optCon)) >= 0)
  {
    switch (c)
    {
    case 'd':					// debug
      if (!debug_level)
      {
	// Set up debugging, from config file
	bk_general_debug_config(B, stderr, BK_ERR_NONE, 0);
	bk_debug_printf(B, "Debugging on\n");
	debug_level++;
      }
      else if (debug_level == 1)
      {
	/*
	 * Enable output of error and higher error logs (this can be
	 * annoying so require -dd)
	 */
	bk_error_config(B, BK_GENERAL_ERROR(B), 0, stderr, BK_ERR_NONE, BK_ERR_ERR, BK_ERROR_CONFIG_FH | BK_ERROR_CONFIG_HILO_PIVOT | BK_ERROR_CONFIG_SYSLOGTHRESHOLD);
	bk_debug_printf(B, "Extra debugging on\n");
	debug_level++;
      }
      else if (debug_level == 2)
      {
	/*
	 * Enable output of all levels of bk_error logs (this can be
	 * very annoying so require -ddd)
	 */
	bk_error_config(B, BK_GENERAL_ERROR(B), 0, stderr, BK_ERR_NONE, BK_ERR_DEBUG, BK_ERROR_CONFIG_FH | BK_ERROR_CONFIG_HILO_PIVOT | BK_ERROR_CONFIG_SYSLOGTHRESHOLD);
	bk_debug_printf(B, "Super-extra debugging on\n");
	debug_level++;
      }
      break;
    case 'v':					// verbose
      BK_FLAG_SET(pc->pc_flags, PC_VERBOSE);
      bk_error_config(B, BK_GENERAL_ERROR(B), ERRORQUEUE_DEPTH, stderr, BK_ERR_NONE, BK_ERR_ERR, 0);
      break;
    case 0x1000:				// no-seatbelts
      BK_FLAG_CLEAR(BK_GENERAL_FLAGS(B), BK_BGFLAGS_FUNON);
      break;
    case 0x1001:				// seatbelts
      BK_FLAG_SET(BK_GENERAL_FLAGS(B), BK_BGFLAGS_FUNON);
      break;
    case 0x1002:				// profiling
      bk_general_funstat_init(B, (char *)poptGetOptArg(optCon), 0);
      break;

    case 'i':
      pc->pc_infile = (char *)poptGetOptArg(optCon);
      break;
    case 'o':
      pc->pc_outfile = (char *)poptGetOptArg(optCon);
      break;

    default:
      getopterr++;
      break;

    }
  }

  /*
   * Reprocess so that argc and argv contain the remaining command
   * line arguments (note argv[0] is an argument, not the program
   * name).  argc remains the number of elements in the argv array.
   */
  argv = (char **)poptGetArgs(optCon);
  argc = 0;
  if (argv)
    for (; argv[argc]; argc++)
      ; // Void

  // XXX - customize here (argv test)
  if (c < -1 || getopterr)
  {
    if (c < -1)
    {
      fprintf(stderr, "%s\n", poptStrerror(c));
    }
    poptPrintUsage(optCon, stderr, 0);
    bk_exit(B, 254);
  }

  if (proginit(B, pc, argv, argc) < 0)
  {
    bk_die(B, 254, stderr, _("Could not perform program initialization\n"), BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  }

  progrun(B, pc);

  poptFreeContext(optCon);
  bk_exit(B, 0);
  return(255);					// Stupid INSIGHT stuff.
}



/**
 * General program initialization
 *
 *	@param B BAKA Thread/Global configuration
 *	@param pc Program configuration
 *	@return <i>0</i> Success
 *	@return <br><i>-1</i> Total terminal failure
 */
static int proginit(bk_s B, struct program_config *pc, char **argv, int argc)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"iprewrite");
  int c;
  char *colon;

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_RETURN(B, -1);
  }

  if (!(pc->pc_rewrites = rewrite_list_create(NULL, NULL, DICT_UNORDERED)))
  {
    fprintf(stderr, "Could not create rewrite list: %s\n", rewrite_list_error_reason(NULL, NULL));
    goto error;
  }

  for(c = 0; c < argc; c++)
  {
    char *orig, *rewr;
    struct rewrite_node *rn;

    if (!(colon = strchr(argv[c], ':')))
    {
      fprintf(stderr, "Illegal argument: %s\n", argv[c]);
      goto error;
    }

    *colon = '\0';
    orig = argv[c];
    rewr = colon + 1;
    
    if (!(BK_MALLOC(rn)))
    {
      fprintf(stderr, "Could not allocate rewrite node: %s\n", strerror(errno));
      goto error;
    }
    
    if (!inet_aton(orig, &(rn->rn_orig)))
    {
      fprintf(stderr, "'%s' is not a valid IP address: %s\n", orig, strerror(errno));
      goto error;
    }

    if (!inet_aton(rewr, &(rn->rn_rewr)))
    {
      fprintf(stderr, "'%s' is not a valid IP address: %s\n", rewr, strerror(errno));
      goto error;
    }

    *colon = ':'; // Pendantic
    
    if (rewrite_list_append(pc->pc_rewrites, rn) != DICT_OK)
    {
      fprintf(stderr, "Could not insert rewrite node in list: %s\n", rewrite_list_error_reason(pc->pc_rewrites, NULL));
      goto error;
    }
  }

  BK_RETURN(B, 0);

 error:
  BK_RETURN(B, -1);  
}



/**
 * Normal processing of program
 *
 *	@param B BAKA Thread/Global configuration
 *	@param pc Program configuration
 *	@return <i>0</i> Success--program may terminate normally
 *	@return <br><i>-1</i> Total terminal failure
 */
static void progrun(bk_s B, struct program_config *pc)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"iprewrite");
  pcap_t *in_pcap = NULL;
  pcap_dumper_t *out_pcap = NULL;
  char errbuf[PCAP_ERRBUF_SIZE];
  struct pcap_pkthdr pkt_header;
  const u_char *pkt_data;
  u_int16_t ip_ltype = htons(0x0800);

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_VRETURN(B);
  }

  if (!(in_pcap = pcap_open_offline(pc->pc_infile, errbuf)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not open %s: %s\n", pc->pc_infile, errbuf);
    goto error;
  }

  if (!(out_pcap = pcap_dump_open(in_pcap, pc->pc_outfile)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not open %s: %s\n", pc->pc_infile, pcap_geterr(in_pcap));
    goto error;
  }

  while(pkt_data = pcap_next(in_pcap, &pkt_header))
  {
    struct baka_etherhdr *pkt_eth = (struct baka_etherhdr *)pkt_data;
    struct baka_iphdr *pkt_ip;

    if (pkt_header.len < sizeof(*pkt_eth) + (sizeof(*pkt_ip)))
      continue;

    if (pkt_eth->pkt_eth_ltype != ip_ltype)
      continue;
    
    pkt_ip = (struct baka_iphdr *)(pkt_data+sizeof(*pkt_eth));
    
    if (replace_ip(B, (struct in_addr *)&pkt_ip->pkt_ip_src, pc, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not replace src IP address\n");
      goto error;
    }
    
    if (replace_ip(B, (struct in_addr *)&pkt_ip->pkt_ip_dst, pc, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not replace dst IP address\n");
      goto error;
    }

    pcap_dump((u_char *)out_pcap, &pkt_header, pkt_data);
  }


 error:
  if (out_pcap)
    pcap_dump_close(out_pcap);

  if (in_pcap)
    pcap_close(in_pcap);
  BK_VRETURN(B);
}



/**
 * Replace the given ip address if is found in the list
 *
 *	@param B BAKA thread/global state.
 *	@param addr The locattion of the IP address to replace
 *	@param pc Program config
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
replace_ip(bk_s B, struct in_addr *addr, struct program_config *pc, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"iprewrite");
  struct rewrite_node *rn;

  if (!pc || !addr)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  for(rn = rewrite_list_minimum(pc->pc_rewrites);
      rn;
      rn = rewrite_list_successor(pc->pc_rewrites, rn))
  {
    if (!memcmp(addr, &rn->rn_orig, sizeof(*addr)))
    {
      *addr = rn->rn_rewr;
      continue;
    }
  }
  BK_RETURN(B, 0);  
}
