/*
 * $Id: libbk_oscompat.h,v 1.3 2001/09/11 17:20:09 seth Exp $
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

#ifndef _libbk_oscompat_h_
#define _libbk_oscompat_h_

#ifdef SOMETHING_FOR_CPLUSPLUS
#ifdef NULL
#undef NULL
#endif /* NULL */
#define NULL ((void *)0)
#endif /* SOMETHING_FOR_CPLUSPLUS */

#ifndef MAX
#define MAX(x,y) ((x) > (y) ? (x) : (y))
#endif /* !MAX */

#ifndef MIN
#define MIN(x,y) ((x) > (y) ? (y) : (x))
#endif /* !MIN */


#endif /* _libbk_oscompat_h_ */
