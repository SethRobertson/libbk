/*
 * $Id: libbk_i18n.h,v 1.3 2001/12/06 00:17:47 seth Exp $
 *
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

#ifndef _libbk_i18n_h_
#define _libbk_i18n_h_

#ifndef BK_I18N_DISABLE
 #include <libintl.h>
 #define  _(String) gettext(String)
 #define N_(String) gettext_noop(String)
 #define gettext_noop(String) (String)

#if defined(USING_INSIGHT)
#define LC_ALL 6				// XXX - evil, naughty code
#endif /* __INSURE__ */

#else /* BK_I18N_DISABLE */
 #define _(String) (String)
 #define N_(String) (String)
 #ifndef LC_ALL
  #define LC_ALL
 #endif /* LC_ALL */
 #define setlocale(Cat, Locale) do { ; } while
 #define textdomain(Package) do { ; } while
 #define bindtextdomain(Package, Directory) do { ; } while
#endif /* BK_I18N_DISABLE */

#endif /* _libbk_i18n_h_ */
