#if !defined(lint) && !defined(__INSIGHT__)
#include "libbk_compiler.h"
UNUSED static const char libbk__rcsid[] = "$Id: test_string.c,v 1.13 2004/07/08 04:40:19 lindauer Exp $";
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

#include <libbk.h>



#define MAXLINE 1024



/*
 * Information of international importance to everyone
 * which cannot be passed around.
 */
struct global_structure
{
} Global;



/*
 * Information about basic program runtime configuration
 */
struct program_config
{
  bk_flags pc_flags;
#define PC_VERBOSE	1
};



void usage(bk_s B);
int proginit(bk_s B, struct program_config *pconfig);
void progrun(bk_s B, struct program_config *pconfig);


int
main(int argc, char **argv, char **envp)
{
  bk_s B = NULL;				/* Baka general structure */
  BK_ENTRY_MAIN(B, __FUNCTION__, __FILE__, "SIMPLE");
  int getopterr = 0;
  extern char *optarg;
  extern int optind;
  struct program_config Pconfig, *pconfig;

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD(B, "BK_ENV_CONF_APP", BK_APP_CONF), NULL, 64, LOG_USER, 0)))
  {
    fprintf(stderr,"Could not perform basic initialization\n");
    exit(254);
  }

  bk_fun_reentry(B);

  pconfig = &Pconfig;
  memset(pconfig,0,sizeof(*pconfig));

  if (getopterr)
  {
    usage(B);
    bk_exit(B,254);
  }

  if (proginit(B, pconfig) < 0)
  {
    bk_die(B,254,stderr,"Could not perform program initialization\n",BK_FLAG_ISSET(pconfig->pc_flags, PC_VERBOSE)?BK_WARNDIE_WANTDETAILS:0);
  }

  progrun(B, pconfig);
  bk_exit(B,0);
  abort();
  BK_RETURN(B,255);				/* Insight is stupid */
}



/*
 * Usage
 */
void usage(bk_s B)
{
  fprintf(stderr,"Usage: %s: [-dv]\n",BK_GENERAL_PROGRAM(B));
}



/*
 * Initialization
 */
int proginit(bk_s B, struct program_config *pconfig)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
  BK_RETURN(B, 0);
}


#define TEST(expr,res)				\
do						\
{						\
  flags = -1;					\
  if ((expr) != res)				\
    fprintf(stderr, "%s != %s\n", #expr, #res);	\
} while (0)



/*
 * Normal processing
 */
void progrun(bk_s B, struct program_config *pconfig)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
  bk_flags flags;
  char buf[512];
  char *onebit = "\1onebit";
  char *twobits = "\1onebit\3threebit";

  TEST(bk_strnspacecmp(B, "a", "b", 1, 1), -1);

  TEST(bk_strnspacecmp(B, "a b", "a \t a", 5, 5), 1);

  TEST(bk_strnspacecmp(B, "a b", "a \t b", 5, 5), 0);

  TEST(bk_strnspacecmp(B, "a b", "a \t b", 2, 5), 0);

  TEST(bk_strnspacecmp(B, "a \t b", "a b", 5, 3), 0);

  TEST(bk_memdiff(B, "", "a", 0, 1), INT_MIN + 256);

  TEST(bk_memdiff(B, "z", "", 1, 0), INT_MAX - 255);

  TEST(bk_memdiff(B, "a", "b", 1, 1), INT_MIN + 512 - 1);

  TEST(bk_memdiff(B, "b", "a", 1, 1), -(INT_MIN + 512 - 1));

  TEST(bk_memdiff(B, "ab", "ac", 2, 2), INT_MIN + 768 - 1);

  TEST(bk_memdiff(B, "ac", "add", 2, 3), INT_MIN + 768 - 1);

  TEST(bk_memdiff(B, "ab", "add", 2, 3), INT_MIN + 768 - 2);

  TEST(bk_memdiff(B, "ab", "ade", 2, 3), INT_MIN + 768 - 2);

  TEST(bk_memdiff(B, "add", "ade", 3, 3), INT_MIN + 1024 - 1);

  TEST(bk_memdiff(B, "\177\020\377\377", "\177\020\177\062", 4, 4),
       -(INT_MIN + 1024 - 128));

  TEST(bk_memdiff(B, "\177\020\000\000", "\177\020\177\062", 4, 4),
       INT_MIN + 1024 - 127);

  TEST(bk_memdiff(B, "\377\377\377\377", "\177\020\177\062", 4, 4),
       -(INT_MIN + 512 - 128));

  TEST(bk_memdiff(B, "\000\000\000\000", "\177\020\177\062", 4, 4),
       INT_MIN + 512 - 127);

  // <TODO>Many functions need test cases (some are in test_stringconv)</TODO>

  // check too little space
  TEST(bk_string_flagtoa(B, 5, buf, 2, NULL, 0), -1);

  TEST(bk_string_atoflag(B, buf, &flags, NULL, 0), 0);

  // check basic hex
  TEST((bk_string_flagtoa(B, 5, buf, 4, NULL, 0),
	strcmp(buf, "0x5")), 0);
  TEST((bk_string_flagtoa(B, 5, buf, 256, NULL, 0),
	strcmp(buf, "0x5")), 0);

  TEST((bk_string_atoflag(B, buf, &flags, NULL, 0),flags), 5);

  // check too little space
  TEST(bk_string_flagtoa(B, 5, buf, 3, onebit, 0), -1);

  // check too little space for symbolic
  TEST((bk_string_flagtoa(B, 5, buf, 4, onebit, 0),
	strcmp(buf, "0x5")), 0);
  TEST((bk_string_flagtoa(B, 5, buf, 10, onebit, 0),
	strcmp(buf, "0x5")), 0);
  TEST((bk_string_flagtoa(B, 5, buf, 19, twobits, 0),
	strcmp(buf, "0x5")), 0);

  // check bogus flag names
  TEST((bk_string_flagtoa(B, 1, buf, 32, "\1\2", 0),
	strcmp(buf, "0x1")), 0);
  TEST((bk_string_flagtoa(B, 2, buf, 32, "\1onebit\2", 0),
	strcmp(buf, "0x2")), 0);

  // check bogus symbols
  TEST(bk_string_atoflag(B, "[]", &flags, NULL, 0), 0);
  TEST(bk_string_atoflag(B, "[]0", &flags, NULL, 0), 0);
  TEST(bk_string_atoflag(B, "[onebit,]0x1", &flags, twobits, 0), 0);
  TEST(bk_string_atoflag(B, "[,onebit]0x1", &flags, NULL, 0), 0);

  // check partial symbolic
  TEST((bk_string_flagtoa(B, 5, buf, 13, onebit, 0),
	strcmp(buf, "[onebit]~0x5")), 0);

  TEST((bk_string_atoflag(B, buf, &flags, NULL, 0),flags), 5);

  TEST((bk_string_flagtoa(B, 5, buf, 256, onebit, 0),
	strcmp(buf, "[onebit]~0x5")), 0);

  TEST((bk_string_atoflag(B, buf, &flags, twobits, 0),flags), 5);

  TEST((bk_string_flagtoa(B, 7, buf, 256, twobits, 0),
	strcmp(buf, "[onebit,threebit]~0x7")), 0);

  TEST((bk_string_atoflag(B, buf, &flags, twobits, 0),flags), 7);

  // check full symbolic
  TEST((bk_string_flagtoa(B, 5, buf, 22, twobits, 0),
	strcmp(buf, "[onebit,threebit]0x5")), 0);

  TEST((bk_string_atoflag(B, buf, &flags, onebit, 0),flags), 5);

  TEST((bk_string_flagtoa(B, 5, buf, 256, twobits, 0),
	strcmp(buf, "[onebit,threebit]0x5")), 0);

  TEST((bk_string_atoflag(B, buf, &flags, twobits, 0),flags), 5);

  *(strchr(buf, ']') + 1) = '\0';	 // chop hex digits
  TEST((bk_string_atoflag(B, buf, &flags, twobits, 0),flags), 5);

  // check symbolic-only decoding
  strcpy(buf, "onebit,threebit");
  TEST(bk_string_atoflag(B, buf, &flags, onebit, 0), -1);
  TEST((bk_string_atoflag(B, buf, &flags, twobits, 0),flags), 5);

  BK_VRETURN(B);
}
