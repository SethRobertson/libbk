/*
 * $Id: libbk_internal.h,v 1.20 2001/11/27 00:58:41 seth Exp $
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

/**
 * @file
 * Internal information required between libbk files which should NOT
 * be used by those outside libbk.
 */

#ifndef _libbk_internal_h_
#define _libbk_internal_h_


#define SCRATCHLEN (MAX(MAXPATHLEN,1024))
#define SCRATCHLEN2 ((SCRATCHLEN)-100)

/* FRIENDLY FUNCTIONS */

extern bk_s bk_general_thread_init(bk_s B, char *name);
extern void bk_general_thread_destroy(bk_s B);


#endif /* _libbk_internal_h_ */
