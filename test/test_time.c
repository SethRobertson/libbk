#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: test_time.c,v 1.11 2004/04/23 21:36:21 lindauer Exp $";
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
 * Example libbk user with main() prototype.
 */
#include <libbk.h>


#define ERRORQUEUE_DEPTH 32			///< Default depth

/**
 * Information of international importance to everyone
 * which cannot be passed around.
 */
struct global_structure
{
  bk_flags	gs_flags;
} Global;



/**
 * Information about basic program runtime configuration
 * which must be passed around.
 */
struct program_config
{
  int z;
  int pc_format_flags;
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
  char c;
  int getopterr=0;
  extern char *optarg;
  extern int optind;
  struct program_config Pconfig, *pconfig=NULL;
  poptContext optCon=NULL;
  const struct poptOption optionsTable[] =
  {
    {"debug", 'd', POPT_ARG_NONE, NULL, 'd', "Turn on debugging", NULL },
    {"notz", 0, POPT_ARG_NONE, NULL, 1, "Don't print T or Z in iso format", NULL},
    POPT_AUTOHELP
    POPT_TABLEEND
  };

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD(B, "BK_ENV_CONF_APP", BK_APP_CONF), NULL, ERRORQUEUE_DEPTH, LOG_USER, 0)))
  {
    fprintf(stderr,"Could not perform basic initialization\n");
    exit(254);
  }
  bk_fun_reentry(B);

  pconfig = &Pconfig;
  memset(pconfig,0,sizeof(*pconfig));

  if (!(optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not initialize options processing\n");
    bk_exit(B,254);
  }

  while ((c = poptGetNextOpt(optCon)) >= 0)
  {
    switch (c)
    {
    case 'd':
      bk_error_config(B, BK_GENERAL_ERROR(B), 0, stderr, 0, BK_ERR_DEBUG, BK_ERROR_CONFIG_FH);	// Enable output of all error logs
      bk_general_debug_config(B, stderr, BK_ERR_NONE, 0);					// Set up debugging, from config file
      bk_debug_printf(B, "Debugging on\n");
      break;
    case 0x1:
      pconfig->pc_format_flags |= BK_TIME_FORMAT_FLAG_NO_TZ;
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

  if (proginit(B, pconfig) < 0)
  {
    bk_die(B,254,stderr,"Could not perform program initialization\n",0);
  }

  progrun(B, pconfig);
  BK_RETURN(B,0);
}



/**
 * General program initialization
 *
 *	@param B BAKA Thread/Global configuration
 *	@param pconfig Program configuration
 *	@return <i>0</i> Success
 *	@return <br><i>-1</i> Total terminal failure
 */
int proginit(bk_s B, struct program_config *pconfig)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");

  if (!pconfig)
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
 *	@param pconfig Program configuration
 *	@return <i>0</i> Success--program may terminate normally
 *	@return <br><i>-1</i> Total terminal failure
 */
void progrun(bk_s B, struct program_config *pconfig)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
  char scratch[1024];
  char inputline[1024];
  struct timeval tv;
  struct timespec t;
  struct timezone z;
  time_t duration;

  // Duration parse tests

  if (bk_time_duration_parse(B, "3d2h1m35s", &duration, 0) < 0)
  {
    fprintf(stderr, "parse of 3d2h1m35s failed\n");
  }
  else if (duration != 266495)
  {
    fprintf(stderr, "3d2h1m35s conversion incorrect\n");
  }

  if (bk_time_duration_parse(B, "5d12h", &duration, 0) < 0)
  {
    fprintf(stderr, "parse of 5d12h failed\n");
  }
  else if (duration != 475200)
  {
    fprintf(stderr, "5d12h conversion incorrect\n");
  }

  if (bk_time_duration_parse(B, "12h40m", &duration, 0) < 0)
  {
    fprintf(stderr, "parse of 12h40m failed\n");
  }
  else if (duration != 45600)
  {
    fprintf(stderr, "12h40m conversion incorrect\n");
  }

  /////////////

  gettimeofday(&tv, &z);
  t.tv_sec = tv.tv_sec;
  t.tv_nsec = tv.tv_usec * 1000;

  if (!(bk_time_ntp_format(B, inputline, sizeof(inputline), &t, 0)))
    fprintf(stderr,"Could not ntp format current time: %ld.%ld\n",
	    (long) t.tv_sec, (long) t.tv_nsec);
  else
  {
    fprintf(stdout, "current ntp time = \"%s\"\n", inputline);

    switch (bk_time_ntp_parse(B, inputline, &t, 0))
    {
    case -1:
      fprintf(stderr,"Could not convert time: %s\n", inputline);
      break;

    default:
      fprintf(stderr,"[Trailing junk for following time]\n");
      // fallthrough

    case 0:
      fprintf(stdout, "input = \"%s\"\n", inputline);
      if (!(bk_time_ntp_format(B, scratch, sizeof(scratch), &t, 0)))
	fprintf(stderr,"Could not format converted time: %ld.%ld\n",
		(long) t.tv_sec, (long) t.tv_nsec);
      else
	fprintf(stdout, "formatted = \"%s\"\n", scratch);
      if (!(bk_time_iso_format(B, scratch, sizeof(scratch), &t, NULL, pconfig->pc_format_flags)))
	fprintf(stderr,"Could not format converted time: %ld.%ld\n",
		(long) t.tv_sec, (long) t.tv_nsec);
      else
	fprintf(stdout, "formatted = \"%s\"\n", scratch);
    }
  }

  if (!(bk_time_iso_format(B, inputline, sizeof(inputline), &t, NULL, pconfig->pc_format_flags)))
    fprintf(stderr,"Could not iso format current time: %ld.%ld\n",
	    (long) t.tv_sec, (long) t.tv_nsec);
  else
    fprintf(stdout, "current time = \"%s\"\n", inputline);

  do
  {
    bk_string_rip(B, inputline, NULL, 0);

    if (BK_STREQ(inputline,"quit") || BK_STREQ(inputline,"exit"))
      BK_VRETURN(B);

    if (inputline[0] == ' ')
    {
      fprintf(stdout, "should be same or similar to%s\n", inputline);
      continue;
    }
    if (inputline[0] == '#')
    {
      fprintf(stdout, "%s\n", inputline);
      continue;
    }
    if (inputline[0] == '\0')
      continue;
    if (!strncmp(inputline, "0x", 2))
      switch (bk_time_ntp_parse(B, inputline, &t, 0))
      {
      case -1:
	fprintf(stderr,"Could not convert time: %s\n", inputline);
	break;

      default:
	fprintf(stderr,"[Trailing junk for following time]\n");
	// fallthrough

      case 0:
	fprintf(stdout, "input = \"%s\"\n", inputline);
	if (!(bk_time_iso_format(B, scratch, sizeof(scratch), &t, NULL, pconfig->pc_format_flags)))
	  fprintf(stderr,"Could not format converted time: %ld.%ld\n",
		  (long) t.tv_sec, (long) t.tv_nsec);
	else
	  fprintf(stdout, "formatted = \"%s\"\n", scratch);
      }
    else
      switch (bk_time_iso_parse(B, inputline, &t, pconfig->pc_format_flags))
      {
      case -1:
	fprintf(stderr,"Could not convert time: %s\n", inputline);
	break;

      default:
	fprintf(stderr,"[Trailing junk for following time]\n");
	// fallthrough

      case 0:
	fprintf(stdout, "input = \"%s\"\n", inputline);
	if (!(bk_time_iso_format(B, scratch, sizeof(scratch), &t, NULL, pconfig->pc_format_flags)))
	  fprintf(stderr,"Could not format converted time: %ld.%ld\n",
		  (long) t.tv_sec, (long) t.tv_nsec);
	else
	  fprintf(stdout, "formatted = \"%s\"\n", scratch);
      }
  }
  while(fgets(inputline, 1024, stdin));

  BK_VRETURN(B);
}
