######################################################################
# $Id: Makefile,v 1.36 2002/11/11 22:53:58 jtt Exp $
#
# ++Copyright LIBBK++
#
# Copyright (c) 2001 The Authors.  All rights reserved.
#
# This source code is licensed to you under the terms of the file
# LICENSE.TXT in this release for further details.
#
# Mail <projectbaka@baka.org> for further information
#
# --Copyright LIBBK--
#
# libbk Makefile
#
BK_LARGE_LIB=libbk$(LIBEXT)

BK_LARGE_LIBSRC=		       		\
		b_addrgroup.c			\
		b_bits.c			\
		b_bnbio.c			\
		b_child.c			\
		b_cksum.c			\
		b_config.c			\
		b_crc.c				\
		b_debug.c			\
		b_error.c			\
		b_exec.c			\
		b_fileutils.c			\
		b_fun.c 			\
		b_funlist.c			\
		b_general.c			\
		b_getbyfoo.c			\
		b_ioh.c				\
		b_listnum.c			\
		b_md5.c				\
		b_memx.c			\
		b_netaddr.c			\
		b_netinfo.c			\
		b_netutils.c			\
		b_nvmap.c			\
		b_pollio.c			\
		b_protoinfo.c			\
		b_rand.c			\
		b_relay.c			\
		b_run.c				\
		b_servinfo.c			\
		b_signal.c			\
		b_stdfun.c			\
		b_stdsock.c			\
		b_strcode.c			\
		b_strconv.c			\
		b_string.c			\
		b_sysutils.c			\
		b_thread.c			\
		b_time.c			\
		b_url.c				\
# Line eater fodder -- leave

GROUPTOP=..
GROUPSUBDIR=lib
##################################################
## BEGIN BKSTANDARD MAKEFILE
-include ./Make.preinclude
-include $(GROUPTOP)/Make.preinclude
-include $(GROUPTOP)/$(PKGTOP)/Make.preinclude
include $(GROUPTOP)/$(PKGTOP)/bkmk/Make.include
-include $(GROUPTOP)/$(PKGTOP)/Make.include
-include $(GROUPTOP)/Make.include
-include ./Make.include
## END BKSTANDARD MAKEFILE
##################################################

$(BK_LARGE_LIBSRC): $(GROUPTOP)/include/libbk_autoconf.h

$(GROUPTOP)/include/libbk_autoconf.h: $(GROUPTOP)/$(PKGTOP)/bkmk/libbk_autoconf.h
	cd $(@D) && $(LN_S) -f $< $(@F)
