#if !defined(lint) && !defined(__INSIGHT__)
#include "libbk_compiler.h"
UNUSED static const char libbk__rcsid[] = "$Id: test_rt.c,v 1.3 2005/09/02 17:13:57 dupuy Exp $";
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
 * Example/testing progrom for bk route information
 */
#include <libbk.h>
#include <libbk_i18n.h>
#include <libbk_net.h>


#define STD_LOCALEDIR_KEY     "LOCALEDIR"	///< Key in bkconfig to find the locale translation files
#define STD_LOCALEDIR_ENV     "BAKA_HOME"	///< Key in Environment to find base of locale directory
#define STD_LOCALEDIR_DEF     "/usr/local/baka"	///< Default base of where locale directory might be found
#define STD_LOCALEDIR_SUB     "locale"		///< Sub-component from install base where locale might be found
#define ERRORQUEUE_DEPTH      32		///< Default error queue depth



/**
 * Information of international importance to everyone
 * which cannot be passed around.
 */
struct global_structure
{
} Global;



/**
 * Information about basic program runtime configuration
 * which must be passed around.
 */
struct program_config
{
  char *		pc_dst;			///< Get route
  bk_flags		pc_flags;		///< Flags are fun!
#define PC_VERBOSE	0x001			///< Verbose output
};



static int proginit(bk_s B, struct program_config *pc);
static void progrun(bk_s B, struct program_config *pc);



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
  BK_ENTRY_MAIN(B, __FUNCTION__, __FILE__, "SIMPLE");
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
    {"route", 'r', POPT_ARG_STRING, NULL, 'r', N_("Get specific route instead of whole table"), N_("ip_destination") },
    {"profiling", 0, POPT_ARG_STRING, NULL, 0x1002, N_("Enable and write profiling data"), N_("filename") },
    {"long-arg-only", 0, POPT_ARG_NONE, NULL, 1, N_("An example of a long argument without a shortcut"), NULL },
    {NULL, 's', POPT_ARG_NONE, NULL, 2, N_("An example of a short argument without a longcut"), NULL },
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

  if (!(optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not initialize options processing\n");
    bk_exit(B, 254);
  }
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

    case 'r':
      pc->pc_dst = (char *)poptGetOptArg(optCon);
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
    default:
      getopterr++;
      break;
    case 1:					// long-arg-only
      printf("You specificed the long-arg-only option\n");
      break;
    case 2:					// s
      printf("You specificed the short only option\n");
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

  if (c < -1 || getopterr)
  {
    if (c < -1)
    {
      fprintf(stderr, "%s\n", poptStrerror(c));
    }
    poptPrintUsage(optCon, stderr, 0);
    bk_exit(B, 254);
  }

  if (proginit(B, pc) < 0)
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
static int proginit(bk_s B, struct program_config *pc)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_RETURN(B, -1);
  }

  BK_RETURN(B, 0);
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
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
  bk_rtinfo_list_t rtlist;
  struct bk_route_info *bri;
  char flags_str[100];
  char *p;

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_VRETURN(B);
  }

  if (!(rtlist = bk_rtinfo_list_create(B, 0)))
  {
    fprintf(stderr,"Could not create route list\n");
    exit(1);
  }

  if (!pc->pc_dst)
  {
    printf("%-15s%-15s%-15s%-15s%-15s\n","Destination","Gateway","Netmask","Flags","Iface");

    for(bri = bk_rtinfo_list_minimum(B, rtlist, 0);
	bri;
	bri = bk_rtinfo_list_successor(B, rtlist, bri, 0))
    {
      char dst[BK_MAX_INET_ADDR_LEN+1];
      char gateway[BK_MAX_INET_ADDR_LEN+1];
      char mask[BK_MAX_INET_ADDR_LEN+1];

      bk_inet_ntoa(B, ((struct sockaddr_in *)(&bri->bri_dst))->sin_addr,  dst);
      bk_inet_ntoa(B, ((struct sockaddr_in *)(&bri->bri_gateway))->sin_addr, gateway);
      bk_inet_ntoa(B, ((struct sockaddr_in *)(&bri->bri_mask))->sin_addr, mask);

      printf("%-15s%-15s%-15s", dst, gateway, mask);

      p = flags_str;
      memset(flags_str, 0, sizeof(flags_str));

      if (bri->bri_flags & RTF_UP)
	*p++ = 'U';
    
      if (bri->bri_flags & RTF_HOST)
	*p++ = 'H';
    
      if (bri->bri_flags & RTF_GATEWAY)
	*p++ = 'G';
    
#ifdef RTF_REINSTATE
      if (bri->bri_flags & RTF_REINSTATE)
	*p++ = 'R';
#endif
    
      if (bri->bri_flags & RTF_DYNAMIC)
	*p++ = 'D';
    
      if (bri->bri_flags & RTF_MODIFIED)
	*p++ = 'M';
    
#ifdef RTF_ADDRCONF
      if (bri->bri_flags & RTF_ADDRCONF)
	*p++ = 'A';
#endif
    
#ifdef RTF_CACHE
      if (bri->bri_flags & RTF_CACHE)
	*p++ = 'C';
#endif
    
      if (bri->bri_flags & RTF_REJECT)
#ifdef RTF_REINSTATE
	*p++ = '!';
#else
	*p++ = 'R';
#endif
    
#ifdef RTF_DONE
      if (bri->bri_flags & RTF_DONE)
	*p++ = 'd';
#endif
    
#if RTF_CLONING
      if (bri->bri_flags & RTF_CLONING)
	*p++ = 'C';
#endif
    
#ifdef RTF_XRESOLVE
      if (bri->bri_flags & RTF_XRESOLVE)
	*p++ = 'X';
#endif
    
#ifdef RTF_LLINFO
      if (bri->bri_flags & RTF_LLINFO)
	*p++ = 'L';
#endif
    
      if (bri->bri_flags & RTF_STATIC)
	*p++ = 'S';
    
#ifdef RTF_PROTO1
      if (bri->bri_flags & RTF_PROTO1)
	*p++ = '1';
#endif
    
#ifdef RTF_PROTO2
      if (bri->bri_flags & RTF_PROTO2)
	*p++ = '2';
#endif
    
#ifdef RTF_PROTO3
      if (bri->bri_flags & RTF_PROTO3)
	*p++ = '3';
#endif
    
#ifdef RTF_WASCLONED
      if (bri->bri_flags & RTF_WASCLONED)
	*p++ = 'W';
#endif
    
#ifdef RTF_PRCLONING
      if (bri->bri_flags & RTF_PRCLONING)
	*p++ = 'c';
#endif
    
#ifdef RTF_BLACKHOLE
      if (bri->bri_flags & RTF_BLACKHOLE)
	*p++ = 'b';
#endif
    
#ifdef RTF_BROADCAST
      if (bri->bri_flags & RTF_BROADCAST)
	*p++ = 'B';
#endif
    
      printf("%-15s%-15s\n", flags_str, bri->bri_if_name);
    }
  }
  else
  {
    if (!(bri = bk_rtinfo_get_route_by_string(B, rtlist, pc->pc_dst, 0)))
    {
      fprintf(stderr, "Could not obtain route to 172.31.2.1: %s\n", strerror(errno));
      exit(1);
    }

    printf("%s routes to %s (%s)\n", pc->pc_dst, inet_ntoa(((struct sockaddr_in *)(&bri->bri_gateway))->sin_addr), bri->bri_if_name);
  }

  bk_rtinfo_list_destroy(B, rtlist);

  BK_VRETURN(B);
}
