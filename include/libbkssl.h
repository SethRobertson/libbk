/*
 *
 *
 * ++Copyright BAKA++
 *
 * Copyright © 2003-2010 The Authors. All rights reserved.
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
 * Public APIs for libbkssl
 */

#ifndef _LIBBKSSL_h_
#define _LIBBKSSL_h_

/**
 * Forward (and incomplete) declarations
 */
struct bk_ssl_ctx;
struct bk_ssl;

extern struct bk_ssl_ctx *bk_ssl_create_context(bk_s B, const char *cert_path, const char *key_path, const char *dhparam_path, const char *cafile, bk_flags flags);
#define BK_SSL_REJECT_V2	0x01		///< Reject SSL v2 clients
#define BK_SSL_NOCERT		0x02		///< Don't use a certificate
#define BK_SSL_WANT_CRL		0x04		///< Enable Certificate Revocation Lists
extern void bk_ssl_destroy_context(bk_s B, struct bk_ssl_ctx *ssl_ctx);
extern void bk_ssl_destroy(bk_s B, struct bk_ssl *ssl, bk_flags flags);
#define BK_SSL_DESTROY_DONTCLOSEFDS	0x1	///< Don't close underlying fds on destroy

#endif /* _LIBBKSSL_h_ */
