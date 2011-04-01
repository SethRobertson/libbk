#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2009-2011";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2009-2011 The Authors. All rights reserved.
 *
 * This source code is licensed to you under the terms of the file
 * LICENSE.TXT in this release for further details.
 *
 * Send e-mail to <projectbaka@baka.org> for further information.
 *
 * - -Copyright BAKA- -
 */

/**
 * @file
 *
 * Miscellaneous math functions.
 */

#include <libbk.h>



/**
 * Compute the inverse of the Gaussian Error Function.
 *
 * @param x the value of erf()
 * @return a value such that erf(retval) == x
 */
double
bk_erfinv(double x)
{
  double c1;
  double inv;
  static const double a = (8.0 / (3.0 * M_PIl)) * ((M_PIl - 3.0) / (4.0 - M_PIl));

  c1 = (2.0 / (M_PIl * a)) + (log(1.0 - x * x) / 2.0);
  inv = -c1 + sqrt((c1 * c1) - ((1.0 / a) * log(1.0 - x * x)));
  inv = sqrt(inv);
  return(inv);
}
