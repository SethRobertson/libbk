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
 *
 * Random number production
 */
#include <libbk.h>
#include <libbk_i18n.h>


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
  u_int			pc_bytes;		///< How much output
  u_int			pc_reinit;		///< Reinit interval
  u_int			pc_maxnum;		///< Maximum number in range
  bk_flags		pc_flags;		///< Flags are fun!
#define PC_VERBOSE	0x001			///< Verbose output
#define PC_MULTI	0x002			///< Ascii-output
#define PC_HEX		0x004			///< Hex-output
};



static void runit(bk_s B, struct program_config *pconfig);



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
  char i18n_localepath[_POSIX_PATH_MAX], *i18n_locale = NULL;
  struct program_config Pconfig, *pconfig = NULL;
  poptContext optCon = NULL;
  struct poptOption optionsTable[] =
  {
    {"debug", 'd', POPT_ARG_NONE, NULL, 'd', N_("Turn on debugging"), NULL },
    {"verbose", 'v', POPT_ARG_NONE, NULL, 'v', N_("Turn on verbose message"), NULL },
    {"no-seatbelts", 0, POPT_ARG_NONE, NULL, 0x1000, N_("Sealtbelts off & speed up"), NULL },
    {"binary", 'b', POPT_ARG_NONE, NULL, 'b', N_("Produce 8-bit output"), NULL },
    {"hexoutput", 'h', POPT_ARG_NONE, NULL, 'h', N_("Produce hex output"), NULL },
    {"multioutput", 'm', POPT_ARG_NONE, NULL, 'm', N_("Produce ascii output"), NULL },
    {"outbytes", 's', POPT_ARG_INT, NULL, 's', "Number of bytes of output", "bytes" },
    {"reinit", 'R', POPT_ARG_INT, NULL, 'R', "Refresh pool every Exponent (2^N) words", "exponent" },
    {"number", 'n', POPT_ARG_INT, NULL, 'n', "Integers between 0 and n", "max number" },
    POPT_AUTOHELP
    POPT_TABLEEND
  };

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD(NULL, "BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, LOG_LOCAL0, 0)))
  {
    fprintf(stderr,"Could not perform basic initialization\n");
    exit(254);
  }
  bk_fun_reentry(B);

  // i18n stuff
  setlocale(LC_ALL, "");
  if (!(i18n_locale = BK_GWD(B, STD_LOCALEDIR_KEY, NULL)) && (i18n_locale = (char *)&i18n_localepath))
    snprintf(i18n_localepath, sizeof(i18n_localepath), "%s/%s", BK_ENV_GWD(B, STD_LOCALEDIR_ENV,STD_LOCALEDIR_DEF), STD_LOCALEDIR_SUB);
  bindtextdomain(BK_GENERAL_PROGRAM(B), i18n_locale);
  textdomain(BK_GENERAL_PROGRAM(B));
  for (c = 0; optionsTable[c].longName || optionsTable[c].shortName; c++)
  {
    if (optionsTable[c].descrip) (*((char **)&(optionsTable[c].descrip)))=_(optionsTable[c].descrip);
    if (optionsTable[c].argDescrip) (*((char **)&(optionsTable[c].argDescrip)))=_(optionsTable[c].argDescrip);
  }

  pconfig = &Pconfig;
  memset(pconfig, 0, sizeof(*pconfig));

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
      bk_error_config(B, BK_GENERAL_ERROR(B), 0, stderr, 0, 0, BK_ERROR_CONFIG_FH);	// Enable output of all error logs
      bk_general_debug_config(B, stderr, BK_ERR_NONE, 0);				// Set up debugging, from config file
      bk_debug_printf(B, "Debugging on\n");
      break;
    case 'v':					// verbose
      BK_FLAG_SET(pconfig->pc_flags, PC_VERBOSE);
      bk_error_config(B, BK_GENERAL_ERROR(B), ERRORQUEUE_DEPTH, stderr, BK_ERR_NONE, BK_ERR_ERR, 0);
      break;
    case 0x1000:				// no-seatbelts
      BK_FLAG_CLEAR(BK_GENERAL_FLAGS(B), BK_BGFLAGS_FUNON);
      break;
    default:
      getopterr++;
      break;

    case 'b':					// bit
      BK_FLAG_CLEAR(pconfig->pc_flags, PC_MULTI);
      BK_FLAG_CLEAR(pconfig->pc_flags, PC_HEX);
      break;
    case 'm':					// multi
      BK_FLAG_SET(pconfig->pc_flags, PC_MULTI);
      BK_FLAG_CLEAR(pconfig->pc_flags, PC_HEX);
      break;
    case 'h':					// hex
      BK_FLAG_CLEAR(pconfig->pc_flags, PC_MULTI);
      BK_FLAG_SET(pconfig->pc_flags, PC_HEX);
      break;
    case 's':					// Bytes of output
      pconfig->pc_bytes=atoi(poptGetOptArg(optCon));
      break;
    case 'R':					// Bits of entropy
      pconfig->pc_reinit=atoi(poptGetOptArg(optCon));
      break;
    case 'n':					// Number
      pconfig->pc_maxnum=atoi(poptGetOptArg(optCon));
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
    bk_exit(B, 254);
  }

  runit(B, pconfig);

  bk_exit(B, 0);
  return(255);					// Stupid INSIGHT stuff.
}



/**
 * Actually output the random numbers
 *
 *	@param B BAKA Global/thread environment
 *	@param pconfig program config
 */
static void runit(bk_s B, struct program_config *pconfig)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "SIMPLE");
  u_int64_t number;
  u_char *data = (u_char *)&number;
  u_int32_t cntr = 0;
  u_int32_t used = 999999;
  struct mt_state *mts;

  if (!(mts = mt19937_truerand_init()))
  {
    bk_die(B, 1, stderr, "Could not initialize random number generator\n", BK_FLAG_ISSET(pconfig->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  }

  if (pconfig->pc_maxnum)
  {
    double fraction;

    for(cntr = 0; cntr < pconfig->pc_bytes; cntr++)
    {
      u_int64_t first, second;

      first = mt19937_genrand64_int64(mts);
      second = UINT64_MAX;

      fraction = (double)first/(double)second;

      printf("%d\n",(int)(fraction*pconfig->pc_maxnum));
    }

    exit(0);
  }

  while (!pconfig->pc_bytes || cntr < pconfig->pc_bytes)
  {
    if (used >= 8)
    {
      number = mt19937_genrand64_int64(mts);
      used = 0;
    }

    if (BK_FLAG_ISSET(pconfig->pc_flags, PC_MULTI))
    {
      data[++used] &= 0x7f;
      if ((data[used] >= '0' && data[used] <= '9') ||
	  (data[used] >= 'A' && data[used] <= 'Z') ||
	  (data[used] >= 'a' && data[used] <= 'z'))
      {
	printf("%c",data[used]);
	cntr++;
      }
    }
    else if (BK_FLAG_ISSET(pconfig->pc_flags, PC_HEX))
    {
      if (!pconfig->pc_bytes || cntr + 8 <= pconfig->pc_bytes)
      {
	printf("%08x", *(unsigned *)(data+used));
	used += 4;
	cntr += 8;
      }
      else
      {
	printf("%02x", (unsigned) data[++used]);
	cntr += 2;
      }
    }
    else
    {
      if (!pconfig->pc_bytes || cntr + 4 <= pconfig->pc_bytes)
      {
	printf("%1c%1c%1c%1c",data[used],data[used+1],data[used+2],data[used+3]);
	used += 4;
	cntr += 4;
      }
      else
      {
	printf("%1c",data[++used]);
	cntr++;
      }
    }
  }

  mt19937_destroy(mts);

  BK_VRETURN(B);
}
