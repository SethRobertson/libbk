#if !defined(lint) && !defined(__INSIGHT__)
#include "libbk_compiler.h"
UNUSED static const char libbk__rcsid[] = "$Id: test_clc.c,v 1.5 2005/09/02 17:13:56 dupuy Exp $";
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
 * Test CLC correctness and speed
 */
#include <libbk.h>
#include <libbk_i18n.h>


#define STD_LOCALEDIR_KEY     "LOCALEDIR"	///< Key in bkconfig to find the locale translation files
#define STD_LOCALEDIR_ENV     "BAKA_HOME"	///< Key in Environment to find base of locale directory
#define STD_LOCALEDIR_DEF     "/usr/local/baka"	///< Default base of where locale directory might be found
#define STD_LOCALEDIR_SUB     "locale"		///< Sub-component from install base where locale might be found
#define ERRORQUEUE_DEPTH      32		///< Default error queue depth
#define CLC_TIME	      15		///< Default seconds per phase
#define CLC_CHOICE	      HT		///< Default CLC choice
#define CLC_FLAGS	      "\1NOCOALESCE\2UNIQUE_KEYS\3UNORDERED\4ORDERED\5BALANCED_TREE\6HT_STRICT_HINTS\7THREADED_SAFE\10THREADED_MEMORY"



/**
 * Data we are adding to CLC
 */
struct ctest
{
  u_int ct_value;				///< Value of interest
};



static int kv_oo_cmp(struct ctest *bck1, struct ctest *bck2);
static int kv_ko_cmp(u_int *a, struct ctest *bck2);
static ht_val kv_obj_hash(struct ctest *bck);
static ht_val kv_key_hash(u_int *a);
static struct ht_args kv_args = { 10*1024*1024, 2, (ht_func)kv_obj_hash, (ht_func)kv_key_hash };
enum clc_choice { DLL, BST, HT };



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
  int			pc_injectnum;		///< Number of items to inject
  int			pc_time;		///< Length of each phase
  enum clc_choice	pc_clc;			///< Valid CLC choices
  bk_flags		pc_clcflags;		///< Flags to test various features of CLC
  bk_flags		pc_flags;		///< Flags are fun!
#define PC_VERBOSE	0x001			///< Verbose output
};



/**
 * Map to convert textual CLC names to internal symbols
 */
struct bk_symbol clcconvlist[] =
{
  { HT,		"HT" },
  { BST,	"BST" },
  { DLL,	"DLL" },
  { 0,		NULL },
};



static void progrun(bk_s B, struct program_config *pc, bk_flags flags);



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
    {"profiling", 0, POPT_ARG_STRING, NULL, 0x1002, N_("Enable and write profiling data"), N_("filename") },

    {"clcflags", 0, POPT_ARG_STRING, NULL, 1, N_("Flags for CLC ([NOCOALESCE,UNIQUE_KEYS,UNORDERED,ORDERED,BALANCED_TREE,HT_STRICT_HINTS,THREADED_SAFE,THREADED_MEMORY])"), N_("flags") },
    {"clc", 'c', POPT_ARG_STRING, NULL, 'c', N_("CLC choice"), N_("DLL/BST/HT") },
    {"time", 't', POPT_ARG_INT, NULL, 't', N_("Seconds duration per phase"), N_("seconds") },
    {"inject", 'i', POPT_ARG_INT, NULL, 'i', N_("Number of items to inject (Default by time)"), N_("items") },
    POPT_AUTOHELP
    POPT_TABLEEND
  };

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD(B, "BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, LOG_LOCAL0, 0)))
  {
    fprintf(stderr,"Could not perform basic initialization\n");
    exit(254);
  }
  bk_fun_reentry(B);

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
  pc->pc_clc = CLC_CHOICE;
  pc->pc_time = CLC_TIME;

  if (!(optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not initialize options processing\n");
    bk_exit(B, 254);
  }

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
    default:
      getopterr++;
      break;

    case 1:					// CLC flags
      if (bk_string_atoflag(B, poptGetOptArg(optCon), &pc->pc_clcflags, CLC_FLAGS, 0) < 0)
      {
	fprintf(stderr,"Invalid format of clc flags\n");
	getopterr++;
      }
      break;
    case 't':					// time
      pc->pc_time = atoi(poptGetOptArg(optCon));
      break;
    case 'i':					// inject
      pc->pc_injectnum = atoi(poptGetOptArg(optCon));
      break;
    case 'c':					// CLC choice
      if (bk_string_atosymbol(B, poptGetOptArg(optCon), &pc->pc_clc, clcconvlist, BK_STRING_ATOSYMBOL_CASEINSENSITIVE) != 0)
      {
	fprintf(stderr,"Invalid CLC choice\n");
	getopterr++;
      }
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

  progrun(B, pc, pc->pc_clcflags);

  poptFreeContext(optCon);
  bk_exit(B, 0);
  return(255);					// Stupid INSIGHT stuff.
}



/**
 * Normal processing of program
 *
 *	@param B BAKA Thread/Global configuration
 *	@param pc Program configuration
 *	@return <i>0</i> Success--program may terminate normally
 *	@return <br><i>-1</i> Total terminal failure
 */
static void progrun(bk_s B, struct program_config *pc, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
  dict_h firstl, insertl = NULL;
  dict_h list = NULL;
  int cnt=0;
  u_int num=0,inlist=0,searchyloops=0,searchnloops=0,iterateloops=0;
  struct bk_stat_node *create, *insert, *searchy, *searchn, *iterate, *delete;
  time_t end;
  quad_t myus;
  struct ctest *cnext, *ctest = NULL;

  if (!pc)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_VRETURN(B);
  }

  srand48(pc->pc_time);

  firstl = dll_create((dict_function)kv_oo_cmp, (dict_function)kv_ko_cmp, DICT_ORDERED);
  insertl = dll_create(NULL, NULL, DICT_UNORDERED);

  end = time(NULL)+pc->pc_time;
  for (cnt = 0; 1; cnt += 2)
  {
    if (pc->pc_injectnum)
    {
      if (cnt >= pc->pc_injectnum*2)
	break;
    }
    else
    {
      if (time(NULL) < end)
	break;
    }


    if (!BK_MALLOC(ctest))
    {
      bk_error_printf(B, BK_ERR_ERR, "Allocate of ctest failed: %s\n", strerror(errno));
      bk_die(B, 254, stderr, _("Could not allocate storage\n"), BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
    }
    ctest->ct_value = cnt;

    if (dll_insert(firstl, ctest) != DICT_OK)
    {
      bk_error_printf(B, BK_ERR_ERR, "Initial insert failure\n");
      bk_die(B, 254, stderr, _("Initial insert failure\n"), BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
    }
  }
  inlist = cnt/2;

  cnext = dll_minimum(firstl);
  for (cnt = inlist; cnt >= 0; cnt--)
  {
    num = (int)(drand48() * inlist);

    ctest = cnext;
    while (num--)
    {
      if (!(ctest = dll_successor(firstl, ctest)))
	ctest = dll_minimum(firstl);
    }
    if (!(cnext = dll_successor(firstl, ctest)))
      cnext = dll_minimum(firstl);
    dll_insert(insertl,ctest);
    dll_delete(firstl,ctest);
  }
  dll_destroy(firstl);

  create = bk_stat_node_create(B, "Create", NULL, 0);
  insert = bk_stat_node_create(B, "Insert", NULL, 0);
  searchy = bk_stat_node_create(B, "Searchy", NULL, 0);
  searchn = bk_stat_node_create(B, "Searchn", NULL, 0);
  iterate = bk_stat_node_create(B, "Iterate", NULL, 0);
  delete = bk_stat_node_create(B, "Delete", NULL, 0);

  bk_stat_node_start(B, create, 0);
  switch (pc->pc_clc)
  {
  case HT:
    list = ht_create((dict_function)kv_oo_cmp, (dict_function)kv_ko_cmp, flags, &kv_args);
    break;
  case BST:
    list = bst_create((dict_function)kv_oo_cmp, (dict_function)kv_ko_cmp, flags);
    break;
  case DLL:
    list = dll_create((dict_function)kv_oo_cmp, (dict_function)kv_ko_cmp, flags);
    break;
  }
  bk_stat_node_end(B, create, 0);
  bk_stat_node_info(B, create, NULL, NULL, &myus, NULL, 0);
  fprintf(stderr,"%15s %10d cycles in %10.3f seconds for %15.3f c/s\n", "Create", 1, myus/1000000.0, 1000000.0/myus);

  end = time(NULL)+pc->pc_time;
  bk_stat_node_start(B, insert, 0);
  while (ctest = dll_minimum(insertl))
  {
    int ret = 0;

    dll_delete(insertl, ctest);

    switch (pc->pc_clc)
    {
    case HT:
      ret = ht_insert(list, ctest);
      break;
    case BST:
      ret = bst_insert(list, ctest);
      break;
    case DLL:
      ret = dll_insert(list, ctest);
      break;
    }

    if (ret != DICT_OK)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not insert value %d\n", num);
      bk_die(B, 254, stderr, _("Could not insert\n"), BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
    }
  }
  bk_stat_node_end(B, insert, 0);
  bk_stat_node_info(B, insert, NULL, NULL, &myus, NULL, 0);
  fprintf(stderr,"%15s %10d cycles in %10.3f seconds for %15.3f c/s\n", "Insert", inlist, myus/1000000.0, inlist*1000000.0/myus);

  end = time(NULL)+pc->pc_time;
  bk_stat_node_start(B, searchy, 0);
  while (time(NULL) < end)
  {
    searchyloops++;
    num = (int)(drand48() * inlist)*2;

    switch (pc->pc_clc)
    {
    case HT:
      ctest = ht_search(list, &num);
      break;
    case BST:
      ctest = bst_search(list, &num);
      break;
    case DLL:
      ctest = dll_search(list, &num);
      break;
    }

    if (!ctest || ctest->ct_value != num)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not find expected value %d\n", num);
      bk_die(B, 254, stderr, _("Could not find expected value\n"), BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
    }
  }
  bk_stat_node_end(B, searchy, 0);
  bk_stat_node_info(B, searchy, NULL, NULL, &myus, NULL, 0);
  fprintf(stderr,"%15s %10d cycles in %10.3f seconds for %15.3f c/s\n", "Search (hit)", searchyloops, myus/1000000.0, searchyloops*1000000.0/myus);

  end = time(NULL)+pc->pc_time;
  bk_stat_node_start(B, searchn, 0);
  while (time(NULL) < end)
  {
    searchnloops++;
    num = (int)(drand48() * inlist)*2 + 1;

    switch (pc->pc_clc)
    {
    case HT:
      ctest = ht_search(list, &num);
      break;
    case BST:
      ctest = bst_search(list, &num);
      break;
    case DLL:
      ctest = dll_search(list, &num);
      break;
    }

    if (ctest)
    {
      bk_error_printf(B, BK_ERR_ERR, "Found unexpected value %d\n", num);
      bk_die(B, 254, stderr, _("Found unexpected value\n"), BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
    }
  }
  bk_stat_node_end(B, searchn, 0);
  bk_stat_node_info(B, searchn, NULL, NULL, &myus, NULL, 0);
  fprintf(stderr,"%15s %10d cycles in %10.3f seconds for %15.3f c/s\n", "Search (miss)", searchnloops, myus/1000000.0, searchnloops*1000000.0/myus);

  end = time(NULL)+pc->pc_time;
  bk_stat_node_start(B, iterate, 0);
  while (time(NULL) < end)
  {
    void *iter = NULL;

    iterateloops++;

    switch (pc->pc_clc)
    {
    case HT:
      iter = ht_iterate(list, DICT_FROM_START);
      break;
    case BST:
      iter = bst_iterate(list, DICT_FROM_START);
      break;
    case DLL:
      iter = dll_iterate(list, DICT_FROM_START);
      break;
    }
    num = 0;

    while (1)
    {
      switch (pc->pc_clc)
      {
      case HT:
	ctest = ht_nextobj(list, iter);
	break;
      case BST:
	ctest = bst_nextobj(list, iter);
	break;
      case DLL:
	ctest = dll_nextobj(list, iter);
	break;
      }

      if (!ctest)
	break;

      num++;
    }
    if (num != inlist)
    {
      bk_error_printf(B, BK_ERR_ERR, "Mismatch between iterate count %d and insert count %d\n", num, inlist);
      bk_die(B, 254, stderr, _("Iterate/insert mismatch\n"), BK_FLAG_ISSET(pc->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
    }

    switch (pc->pc_clc)
    {
    case HT:
      ht_iterate_done(list, iter);
      break;
    case BST:
      bst_iterate_done(list, iter);
      break;
    case DLL:
      dll_iterate_done(list, iter);
      break;
    }
  }
  bk_stat_node_end(B, iterate, 0);
  bk_stat_node_info(B, iterate, NULL, NULL, &myus, NULL, 0);
  fprintf(stderr,"%15s %10d cycles in %10.3f seconds for %15.3f c/s\n", "Iterate", iterateloops, myus/1000000.0, iterateloops*1000000.0/myus);

  bk_stat_node_start(B, delete, 0);
  switch (pc->pc_clc)
  {
  case HT:
    DICT_NUKE(list, ht, ctest, bk_error_printf(B, BK_ERR_ERR, "Cannot delete what we just minimized\n"); break, free(ctest));
    break;
  case BST:
    DICT_NUKE(list, bst, ctest, bk_error_printf(B, BK_ERR_ERR, "Cannot delete what we just minimized\n"); break, free(ctest));
    break;
  case DLL:
    DICT_NUKE(list, dll, ctest, bk_error_printf(B, BK_ERR_ERR, "Cannot delete what we just minimized\n"); break, free(ctest));
    break;
  }
  bk_stat_node_end(B, delete, 0);
  bk_stat_node_info(B, delete, NULL, NULL, &myus, NULL, 0);
  fprintf(stderr,"%15s %10d cycles in %10.3f seconds for %15.3f c/s\n", "Delete", 1, myus/1000000.0, 1000000.0/myus);
}



static int kv_oo_cmp(struct ctest *a, struct ctest *b)
{
  return (a->ct_value - b->ct_value);
}
static int kv_ko_cmp(u_int *a, struct ctest *b)
{
  return (*a - b->ct_value);
}
static ht_val kv_obj_hash(struct ctest *a)
{
  return (a->ct_value);
}
static ht_val kv_key_hash(u_int *a)
{
  return (*a);
}
