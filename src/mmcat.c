#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: mmcat.c,v 1.10 2003/06/25 15:34:05 brian Exp $";
static const char libbk__copyright[] = "Copyright (c) 2003";
static const char libbk__contact[] = "<projectbaka@baka.org>";
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
 * Convert mime encoded data into raw data (or vis-versa)
 */
#include <libbk.h>


#define ERRORQUEUE_DEPTH 32			///< Default depth
#define LARGE_SIZE	 (57*20)		///< Binary data holding 20 mime output lines



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
  FILE		       *pc_input;		///< Input file handle
  FILE		       *pc_output;		///< Output file handle
  size_t		pc_size;		///< Buffer size to use
  bk_flags		pc_flags;		///< Flags are fun!
#define PC_VERBOSE	0x1			///< Verbose output
#define PC_ENCODE	0x2			///< Encoding (!present->decoding)
};



int proginit(bk_s B, struct program_config *pconfig);
void progrun(bk_s B, struct program_config *pconfig);



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
  char *buf;
  int getopterr=0;
  extern char *optarg;
  extern int optind;
  struct program_config Pconfig, *pconfig=NULL;
  poptContext optCon=NULL;
  const struct poptOption optionsTable[] =
  {
    {"debug", 'd', POPT_ARG_NONE, NULL, 'd', "Turn on debugging", "debug" },
    {"verbose", 'v', POPT_ARG_NONE, NULL, 'v', "Turn on verbose message", NULL },
    {"no-seatbelts", 0, POPT_ARG_NONE, NULL, 0x1000, "Sealtbelts off & speed up", NULL },
    {"decode", 'D', POPT_ARG_NONE, NULL, 'D', "decode mime into raw", "decode" },
    {"encode", 'E', POPT_ARG_NONE, NULL, 'E', "encode input into mime", "encode" },
    {"size", 's', POPT_ARG_INT, NULL, 's', "size of blocks to read", "block size" },
    POPT_AUTOHELP
    POPT_TABLEEND
  };

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD(NULL, "BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, LOG_LOCAL0, 0)))
  {
    fprintf(stderr,"Could not perform basic initialization\n");
    exit(254);
  }
  bk_fun_reentry(B);

  pconfig = &Pconfig;
  memset(pconfig,0,sizeof(*pconfig));
  pconfig->pc_size = LARGE_SIZE;

  if (!(optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not initialize options processing\n");
    bk_exit(B,254);
  }
  poptSetOtherOptionHelp(optCon, "[<input file> [<output file>]]");

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

    case 'D':
      BK_FLAG_CLEAR(pconfig->pc_flags, PC_ENCODE);
      break;
    case 'E':
      BK_FLAG_SET(pconfig->pc_flags, PC_ENCODE);
      break;
    case 's':
      pconfig->pc_size = atoi(poptGetOptArg(optCon));
      break;
    }
  }

  // Process additional command line arguments, checking for errors
  pconfig->pc_input = stdin;
  pconfig->pc_output = stdout;
  argv = (char **)poptGetArgs(optCon);
  if (argv && argv[0])
  {
    if (strcmp(argv[0], "-"))
    {
      if (!(pconfig->pc_input = fopen(argv[0], "r")))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not open %s for input: %s\n", argv[0], strerror(errno));
	bk_warn(B, stderr, "Could not open input file\n", BK_FLAG_ISSET(pconfig->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
	getopterr++;
      }
    }
    if (argv[1] && strcmp(argv[1], "-"))
    {
      if (!(pconfig->pc_input = fopen(argv[1], "w")))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not open %s for output: %s\n", argv[1], strerror(errno));
	bk_warn(B, stderr, "Could not open output file\n", BK_FLAG_ISSET(pconfig->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
	getopterr++;
      }
    }
    if (argv[1] && argv[2])
    {
      bk_error_printf(B, BK_ERR_ERR, "Too many argument\n");
      bk_warn(B, stderr, "Too many arguments\n", BK_FLAG_ISSET(pconfig->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
      getopterr++;
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

  if (!BK_MALLOC_LEN(buf, pconfig->pc_size))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate input buffer of size %d: %s\n", (int)pconfig->pc_size, strerror(errno));
    bk_die(B, 254, stderr,"Could not allocate memory for input buffer\n", BK_FLAG_ISSET(pconfig->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  }

  if (BK_FLAG_ISSET(pconfig->pc_flags, PC_ENCODE))
  {
    bk_vptr tmp;
    int ret;
    char *out;

    while ((ret = fread(buf, 1, pconfig->pc_size, pconfig->pc_input)) > 0)
    {
      tmp.len = ret;
      tmp.ptr = buf;

      if (!(out = bk_encode_base64(B, &tmp, NULL)))
	bk_die(B,1,stderr,"Could not encode input string\n", BK_FLAG_ISSET(pconfig->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);

      fwrite(out, strlen(out), 1, pconfig->pc_output);
      free(out);
    }
  }
  else
  {
    bk_vptr *tmp;

    while (fgets(buf, pconfig->pc_size, pconfig->pc_input))
    {
      if (!(tmp = bk_decode_base64(B, buf)))
	bk_die(B, 1, stderr,"Could not decode input string\n", BK_FLAG_ISSET(pconfig->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);

      fwrite(tmp->ptr, tmp->len, 1, pconfig->pc_output);
      free(tmp->ptr);
      free(tmp);
    }
  }

  bk_exit(B, 0);
  abort();
  return(255);
}
