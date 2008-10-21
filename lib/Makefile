######################################################################
#
#
# ++Copyright BAKA++
#
# Copyright © 2001-2008 The Authors. All rights reserved.
#
# This source code is licensed to you under the terms of the file
# LICENSE.TXT in this release for further details.
#
# Send e-mail to <projectbaka@baka.org> for further information.
#
# - -Copyright BAKA- -
#
# libbk Makefile
#
BK_LARGE_LIB=libbk$(LIBEXT)

BK_LARGE_LIBSRC=				\
		b_addrgroup.c			\
		b_bigint.c			\
		b_bits.c			\
		b_child.c			\
		b_cksum.c			\
		b_config.c			\
		b_crc.c				\
		b_debug.c			\
		b_dll.c				\
		b_error.c			\
		b_exec.c			\
		b_fileutils.c			\
		b_fun.c				\
		b_funlist.c			\
		b_general.c			\
		b_getbyfoo.c			\
		b_intinfo.c			\
		b_ioh.c				\
		b_listnum.c			\
		b_md5.c				\
		b_memx.c			\
		b_netaddr.c			\
		b_netinfo.c			\
		b_netutils.c			\
		b_nvmap.c			\
		b_pollio.c			\
		b_procinfo.c			\
		b_protoinfo.c			\
		b_rand.c			\
		b_realloc.c			\
		b_relay.c			\
		b_ringbuf.c			\
		b_ringdir.c			\
		b_rtinfo.c			\
		b_run.c				\
		b_servinfo.c			\
		b_shmipc.c			\
		b_signal.c			\
		b_sprintf.c			\
		b_stats.c			\
		b_stdfun.c			\
		b_stdsock.c			\
		b_strcode.c			\
		b_strconv.c			\
		b_string.c			\
		b_sysutils.c			\
		b_thread.c			\
		b_time.c			\
		b_url.c				\
		b_vault.c			\
		b_vptr.c			\
		mt19937-64.c			\
# Line eater fodder -- leave

LT_LDLIBS=-ldict -lfsma -lpqueue  $(PTHREAD_LIBS)

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

ifneq ($(findstring -i686,$(BK_ARCHNAME)),)
# <HACK>
# For unknown reasons gcc 4.1.1 on x86_64 seems to flag no-undefined less
# strictly than i386. This is very odd. JTT hopes that this can go away
# soon.
#
# Look for copy/pasted versions of this in other Makefiles
#
# </HACK>
TEMP_LT_LDFLAGS := $(LT_LDFLAGS)
LT_LDFLAGS = $(filter-out -no-undefined, $(TEMP_LT_LDFLAGS))
endif
