#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: test_stringconv.c,v 1.12 2003/03/19 00:10:35 jtt Exp $";
static const char libbk__copyright[] = "Copyright (c) 2001";
static const char libbk__contact[] = "<projectbaka@baka.org>";
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


char *prompts[] = 
{
  "String to unsigned 32 bit integer conversion.  Please enter number:  ",
  "String to signed 32 bit integer conversion.  Please enter number:  ",
  "String to unsigned 64 bit integer conversion.  Please enter number:  ",
  "String to signed 64 bit integer conversion.  Please enter number:  ",
  "String hash.  Please enter string to hash:  ",
  "String tokenization.  Please enter arguments followed by the string to tokenize.\n  <limit>#<spliton>#<flagbits>#<string to tokenize>\n  0x01 - Multisplit (foo::bar are two tokens, not three)\n  0x02 - Singlequote quoting\n  0x04 - Doublequote quoting\n  0x08 - Variable expansion\n  0x10 - Backslash quoting of next character\n  0x20 - Backslash character interpolation\\n\n  0x40 - Backslash octal number ascii interpolation\010\n\n",
  NULL,
};



int
main(int argc, char **argv, char **envp)
{
  bk_s B = NULL;				/* Baka general structure */
  BK_ENTRY_MAIN(B, __FUNCTION__, __FILE__, "SIMPLE");
  int getopterr = 0;
  extern char *optarg;
  extern int optind;
  struct program_config Pconfig, *pconfig;

  if (!(B=bk_general_init(argc, &argv, &envp, BK_ENV_GWD("BK_ENV_CONF_APP", BK_APP_CONF), NULL, 64, LOG_USER, 0)))
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
    
  printf("Enter END to end each section's input.\n");

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



/*
 * Normal processing
 */
void progrun(bk_s B, struct program_config *pconfig)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"SIMPLE");
  char line[MAXLINE];
  u_int32_t ui32;
  int32_t i32;
  u_int64_t ui64;
  int64_t i64;
  int ret;
  int state=0;

  printf("%s",prompts[state]);
  fflush(stdout);
  while (fgets(line,sizeof(line),stdin))
  {
    printf("%s", line);
    bk_string_rip(B, line, NULL, 0);		/* Nuke CRLF */

    if (!strcmp(line,"END"))
      state++;
    else
      switch(state)
      {
      case 0:
	ui32 = ~0;
	ret = bk_string_atou32(B, line, &ui32, 0);
	if (ret < 0)
	  printf("bk_string_atou32 failed with %d -- converted to 0x%x or %u or 0%o\n",ret,ui32,ui32,ui32);
	else
	  printf("Converted to 0x%x or %u or 0%o\n",ui32,ui32,ui32);
	break;
      case 1:
	i32 = ~0;
	ret = bk_string_atoi32(B, line, &i32, 0);
	if (ret < 0)
	  printf("bk_string_atoi32 failed with %d -- converted to 0x%x or %d or 0%o\n",ret,(u_int)i32,i32,(u_int)i32);
	else
	  printf("Converted to 0x%x or %d or 0%o\n",(u_int)i32,i32,(u_int)i32);
	break;
      case 2:
	ui64 = ~0;
	ret = bk_string_atou64(B, line, &ui64, 0);
	if (ret < 0)
	  printf("bk_string_atou64 failed with %d -- converted to 0x%llx or %llu or 0%llo\n",ret,ui64,ui64,ui64);
	else
	  printf("Converted to 0x%llx or %llu or 0%llo\n",ui64,ui64,ui64);
	break;
      case 3:
	i64 = ~0;
	ret = bk_string_atoi64(B, line, &i64, 0);
	if (ret < 0)
	  printf("bk_string_atoi64 failed with %d -- converted to 0x%llx or %lld or 0%llo\n",ret,(u_int64_t)i64,i64,(u_int64_t)i64);
	else
	  printf("Converted to 0x%llx or %lld or 0%llo\n",(u_int64_t)i64,i64,(u_int64_t)i64);
	break;
      case 4:
	ui32 = bk_strhash(line, BK_STRHASH_NOMODULUS);
	printf("Hashed to 0x%x\n",ui32);
	break;
      case 5:
	{
	  char *split, *flag,*string;
	  u_int limit;
	  bk_flags flags;
	  char **tokens, **cur;
	  char **vardb = NULL;

	  if (!(split=strchr(line,'#')))
	  {
	    printf("Invalid tokenization command\n");
	    break;
	  }
	  *split++ = 0;

	  if (!(flag=strchr(split,'#')))
	  {
	    printf("Invalid tokenization command\n");
	    break;
	  }
	  *flag++ = 0;

	  if (!(string=strchr(flag,'#')))
	  {
	    printf("Invalid tokenization command\n");
	    break;
	  }
	  *string++ = 0;

	  if (BK_STRING_ATOU(B, line, &limit, 0) < 0 || bk_string_atou32(B, flag, &flags, 0) < 0)
	  {
	    printf("Could not convert limit or flags\n");
	    break;
	  }

	  if (flags & 0x8)
	  {
	    flags &= ~0x8;
	    vardb = environ;
	  }

	  if (!(tokens = bk_string_tokenize_split(B, string, limit, split, NULL, (const char **)vardb, flags)))
	  {
	    printf("Could not tokenize string\n");
	    break;
	  }
	  i32 = 0;
	  for(cur=tokens;*cur;cur++)
	  {
	    printf("  Token %d: ``%s''\n",i32++,*cur);
	  }
	  break;
	}
      default:
	state++;
	break;
      }

    if (!prompts[state])
      break;

    printf("\n\n%s",prompts[state]);
    fflush(stdout);
  }

  BK_VRETURN(B);
}
