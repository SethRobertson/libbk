/*
	ENT  --  Entropy calculation and analysis of putative
		 random sequences.

        Designed and implemented by John "Random" Walker in May 1985.

	Multiple analyses of random sequences added in December 1985.

	Bit stream analysis added in September 1997.

	Terse mode output, getopt() command line processing,
	optional stdin input, and HTML documentation added in
	October 1998.

	For additional information and the latest version,
	see http://www.fourmilab.ch/random/

*/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#ifndef _MSDOS
#include <unistd.h>
#endif

#include "iso8859.h"
#include "randtest.h"

#define UPDATE  "October 20th, 1998"

#define FALSE 0
#define TRUE  1

#ifdef M_PI
#define PI	 M_PI
#else
#define PI	 3.14159265358979323846
#endif

#define V (void)

/*  Table of chi-square Xp values versus corresponding probabilities  */

static double chsqt[2][10] = {
	0.5,
	0.25,
	0.1,
	0.05,
	0.025,
	0.01,
	0.005,
	0.001,
	0.0005,
	0.0001,

	0.0,
	0.6745,
	1.2816,
	1.6449,
	1.9600,
	2.3263,
	2.5758,
	3.0902,
	3.2905,
	3.7190

};

/*  HELP  --  Print information on how to call	*/

static void help(void);
static void help(void)
{
        V printf("ent --  Calculate entropy of file.  Call");
        V printf("\n        with ent [options] [input-file]");
        V printf("\n");
        V printf("\n        Options:   -b   Treat input as a stream of bits");
        V printf("\n                   -c   Print occurrence counts");
        V printf("\n                   -f   Fold upper to lower case letters");
        V printf("\n                   -u   Print this message\n");
        V printf("\nBy John Walker");
        V printf("\n   http://www.fourmilab.ch/");
        V printf("\n   %s\n", UPDATE);
}

/*  GETOPT  --	Dumb version of getopt for brain-dead MS-DOS.  */

#ifdef _MSDOS	
static int optind = 1;

static int getopt(int argc, char *argv[], char *opts)
{
    static char *opp = NULL;
    int o;
    
    while (opp == NULL) {
        if ((optind >= argc) || (*argv[optind] != '-')) {
	   return -1;
	}
	opp = argv[optind] + 1;
	optind++;
	if (*opp == 0) {
	    opp = NULL;
	}	
    }
    o = *opp++;
    if (*opp == 0) { 
	opp = NULL;
    }
    return strchr(opts, o) == NULL ? '?' : o;
}
#endif

/*  Main program  */

int main(int argc, char *argv[])
{
	int i, oc, opt;
	long ccount[256];	      /* Bins to count occurrences of values */
	long totalc = 0;	      /* Total character count */
	char *samp;
	double a, montepi, chip,
	       scc, ent, mean, chisq;
	FILE *fp = stdin;
	int counts = FALSE,	      /* Print character counts */
	    fold = FALSE,	      /* Fold upper to lower */
	    binary = FALSE,	      /* Treat input as a bitstream */
	    terse = FALSE;	      /* Terse (CSV format) output */

        while ((opt = getopt(argc, argv, "bcftu?BCFTU")) != -1) {
	    switch (toISOlower(opt)) {
                 case 'b':
		    binary = TRUE;
		    break;

                 case 'c':
		    counts = TRUE;
		    break;

                 case 'f':
		    fold = TRUE;
		    break;

                 case 't':
		    terse = TRUE;
		    break;

                 case '?':
                 case 'u':
		    help();
		    return 0;
	    }
	}
	if (optind < argc) {
	   if (optind != (argc - 1)) {
              V printf("Duplicate file name.\n");
	      help();
	      return 2;
	   }
           if ((fp = fopen(argv[optind], "rb")) == NULL) {
              V printf("Cannot open file %s\n", argv[optind]);
	      return 2;
	   }
	}

        samp = binary ? "bit" : "byte";
	memset(ccount, 0, sizeof ccount);

	/* Initialise for calculations */

	rt_init(binary);

	/* Scan input file and count character occurrences */

	while ((oc = fgetc(fp)) != EOF) {
	   unsigned char ocb;

	   if (fold && isISOalpha(oc) && isISOupper(oc)) {
	      oc = toISOlower(oc);
	   }
	   ocb = (unsigned char) oc;
	   totalc += binary ? 8 : 1;
	   if (binary) {
	    int b;
	    unsigned char ob = ocb;

	    for (b = 0; b < 8; b++) {
		ccount[ob & 1]++;
		ob >>= 1;
	    }
	   } else {
	       ccount[ocb]++;	      /* Update counter for this bin */
	   }
	   rt_add(&ocb, 1);
	}
	V fclose(fp);

	/* Complete calculation and return sequence metrics */

	rt_end(&ent, &chisq, &mean, &montepi, &scc);

	if (terse) {
           V printf("0,File-%ss,Entropy,Chi-square,Mean,Monte-Carlo-Pi,Serial-Correlation\n",
              binary ? "bit" : "byte");
           V printf("1,%ld,%f,%f,%f,%f,%f\n",
	      totalc, ent, chisq, mean, montepi, scc);
	}

	/* Calculate probability of observed distribution occurring from
	   the results of the Chi-Square test */

	chip = sqrt(2.0 * chisq) - sqrt(2.0 * (binary ? 1 : 255.0) - 1.0);
	a = fabs(chip);
	for (i = 9; i >= 0; i--) {
	   if (chsqt[1][i] < a) {
	      break;
	   }
	}
	chip = (chip >= 0.0) ? chsqt[0][i] : 1.0 - chsqt[0][i];

	/* Print bin counts if requested */

	if (counts) {
	   if (terse) {
              V printf("2,Value,Occurrences,Fraction\n");
	   } else {
              V printf("Value Char Occurrences Fraction\n");
	   }
	   for (i = 0; i < (binary ? 2 : 256); i++) {
	      if (terse) {
                 V printf("3,%d,%ld,%f\n", i,
		    ccount[i], ((double) ccount[i] / totalc));
	      } else {
		 if (ccount[i] > 0) {
                    V printf("%3d   %c   %10ld   %f\n", i,
		       /* The following expression shows ISO 8859-1
			  Latin1 characters and blanks out other codes.
			  The test for ISO space replaces the ISO
			  non-blanking space (0xA0) with a regular
                          ASCII space, guaranteeing it's rendered
                          properly even when the font doesn't contain
			  that character, which is the case with many
			  X fonts. */
                       (!isISOprint(i) || isISOspace(i)) ? ' ' : i,
		       ccount[i], ((double) ccount[i] / totalc));
		 }
	      }
	   }
	   if (!terse) {
              V printf("\nTotal:    %10ld   %f\n\n", totalc, 1.0);
	   }
	}

	/* Print calculated results */

	if (!terse) {
           V printf("Entropy = %f bits per %s.\n", ent, samp);
           V printf("\nOptimum compression would reduce the size\n");
           V printf("of this %ld %s file by %d percent.\n\n", totalc, samp,
		    (short) ((100 * ((binary ? 1 : 8) - ent) /
			      (binary ? 1.0 : 8.0))));
	   V printf(
              "Chi square distribution for %ld samples is %1.2f, and randomly\n",
	      totalc, chisq);
           V printf("would exceed this value %1.2f percent of the times.\n\n",
	      chip * 100);
	   V printf(
              "Arithmetic mean value of data %ss is %1.4f (%.1f = random).\n",
	      samp, mean, binary ? 0.5 : 127.5);
           V printf("Monte Carlo value for Pi is %1.9f (error %1.2f percent).\n",
	      montepi, 100.0 * (fabs(PI - montepi) / PI));
           V printf("Serial correlation coefficient is ");
	   if (scc >= -99999) {
              V printf("%1.6f (totally uncorrelated = 0.0).\n", scc);
	   } else {
              V printf("undefined (all values equal!).\n");
	   }
	}

	return 0;
}
