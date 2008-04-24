#if !defined(lint) && !defined(__INSIGHT__)
#include "libbk_compiler.h"
UNUSED static const char libbk__rcsid[] = "$Id: genrand.c,v 1.14 2008/04/24 23:06:56 seth Exp $";
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
  u_long		pc_bytes;		///< How much output
  u_int			pc_reinit;		///< Reinit interval
  u_int			pc_maxnum;		///< Maximum number in range
  bk_flags		pc_flags;		///< Flags are fun!
#define PC_VERBOSE	0x001			///< Verbose output
#define PC_MULTI	0x002			///< Ascii-output
#define PC_HEX		0x004			///< Hex-output
#define PC_MT19937	0x008			///< Twister PRNG
#define PC_PRETTY	0x016			///< Pretty (return terminated)
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
    {"rawoutput", 'r', POPT_ARG_NONE, NULL, 'r', N_("Produce 8-bit output"), NULL },
    {"hexoutput", 'h', POPT_ARG_NONE, NULL, 'h', N_("Produce hex output"), NULL },
    {"multioutput", 'm', POPT_ARG_NONE, NULL, 'm', N_("Produce ascii output"), NULL },
    {"outbytes", 's', POPT_ARG_INT, NULL, 's', N_("Number of bytes of output"), "bytes" },
    {"pretty", 'p', POPT_ARG_NONE, NULL, 'p', N_("Return termination"), NULL },
    {"reinit", 'R', POPT_ARG_INT, NULL, 'R', N_("Refresh pool every Exponent (2^N) words"), "exponent" },
    {"mt19937", 'T', POPT_ARG_NONE, NULL, 'T', N_("Use Mersenne Twister pseudorandom number instead of true random number generator"), NULL },
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

    case 'r':					// bit/raw
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
      {
	double tmplimit = bk_string_demagnify(B, poptGetOptArg(optCon), 0);
	if (isnan(tmplimit))
	  getopterr++;
	pconfig->pc_bytes = tmplimit;
      }
      break;
    case 'T':					// Twister PNRG
      BK_FLAG_SET(pconfig->pc_flags, PC_MT19937);
      break;
    case 'p':					// Pretty
      BK_FLAG_SET(pconfig->pc_flags, PC_PRETTY);
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
  u_long cntr = 0;
  u_long used = 0;
  struct bk_truerandinfo *R = NULL;
  struct mt_state *M = NULL;

  if (BK_FLAG_ISSET(pconfig->pc_flags, PC_MT19937))
  {
    if (!(M = mt19937_truerand_init()))
    {
      bk_die(B, 1, stderr, "Could not initialize Twister random number generator\n", BK_FLAG_ISSET(pconfig->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
    }
  }
  else
  {
    if (!(R = bk_truerand_init(B, pconfig->pc_reinit, 0)))
    {
      bk_die(B, 1, stderr, "Could not initialize random number generator\n", BK_FLAG_ISSET(pconfig->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
    }
  }

  if (pconfig->pc_maxnum)
  {
    double fraction;

    for(cntr = 0; cntr < pconfig->pc_bytes; cntr++)
    {
      u_int64_t first, second;

      if (BK_FLAG_ISSET(pconfig->pc_flags, PC_MT19937))
      {
	first = mt19937_genrand64_int64(M);
      }
      else
      {
	first = ((u_int64_t)bk_truerand_getword(B, R, NULL, 0))<<32 | ((u_int64_t)bk_truerand_getword(B, R, NULL, 0));
      }
      second = UINT64_MAX;

      fraction = (double)first/(double)second;

      printf("%d\n",(int)(fraction*pconfig->pc_maxnum));
    }

    exit(0);
  }

  while (!pconfig->pc_bytes || cntr < pconfig->pc_bytes)
  {
    int wanted, ret = 0;

    if (used < 1)
    {
      if (BK_FLAG_ISSET(pconfig->pc_flags, PC_MT19937))
      {
	number = mt19937_genrand64_int64(M);
	used = 8;
      }
      else
      {
	number = bk_truerand_getword(B, R, NULL, 0);
	used = 4;
      }
    }

    wanted = pconfig->pc_bytes - cntr;

    if (BK_FLAG_ISSET(pconfig->pc_flags, PC_MULTI))
    {
      data[--used] &= 0x7f;
      if ((data[used] >= '0' && data[used] <= '9') ||
	  (data[used] >= 'A' && data[used] <= 'Z') ||
	  (data[used] >= 'a' && data[used] <= 'z'))
      {
	ret = printf("%c",data[used]);
	cntr++;
      }
    }
    else if (BK_FLAG_ISSET(pconfig->pc_flags, PC_HEX))
    {
      if (!pconfig->pc_bytes || cntr + used*2 <= pconfig->pc_bytes)
      {
	ret = printf("%0*lx", (int)used*2,number);
	cntr += used*2;
	used = 0;
      }
      else
      {
	ret = printf("%0*x", wanted, (unsigned) data[--used]);
	cntr += 2;
      }
    }
    else
    {
      u_int towrite = used;

      if (pconfig->pc_bytes)
      {
	towrite = pconfig->pc_bytes - cntr;
	if (used < towrite)
	  towrite = used;
      }

      ret = fwrite(data,1,towrite,stdout);
      cntr += ret;
      used -= ret;
    }

    if (ret < 0)
    {
      fprintf(stderr,"Output failed: %s\n",strerror(errno));
      break;
    }
  }

  if (BK_FLAG_ISSET(pconfig->pc_flags, PC_PRETTY))
    printf("\n");

  bk_truerand_destroy(B, R);

  BK_VRETURN(B);
}
