/*
 * $Id: libbk_i18n.h,v 1.1 2001/11/16 21:30:03 brian Exp $
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
#else
 #define _(String) (String)
 #define N_(String) (String)
 #define textdomain(Domain)
 #define bindtextdomain(Package, Directory)
#endif
