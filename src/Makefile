######################################################################
# $Id: Makefile,v 1.34 2005/03/16 18:53:26 seth Exp $
#
# ++Copyright LIBBK++
#
# Copyright (c) 2003 The Authors. All rights reserved.
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

# proto.c is template for building other simple programs; we want to compile
# this, but as it serves no purpose itself we don't need to install it.
BK_SIMPLE_INTERNALPROGS=		\
	proto				\
#line fodder eater. Do not remove.

# BK_TMP_PROG provides a convenient hook for quickly creating "one-off"
# programs for testing things.  Test programs that provide a regression suite
# for a set of libbk features should be committed to ../test.
BK_SIMPLE_PROGS=			\
	adjtime				\
	b_chill				\
	bdtee				\
	bk_daemon			\
	bk_funi				\
	bk_pty				\
	bk_trunc			\
	bkrelay				\
	bttcp				\
	genrand				\
	mmcat				\
	runsize				\
	timeout				\
	$(BK_TMP_PROG)			\
#line fodder eater. Do not remove.

GROUPTOP=..
GROUPSUBDIR=src

# This has to go here so we get the benefit of $(TEST) which is included by one of the above.
BK_TMP_PROG:=$(shell if [ -f tmp.c ]; then echo tmp; fi)


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

ifneq ($(strip $(BK_USING_SSL)),false)
LOCAL_LIBS=-lbkssl
ifeq ($(BK_WANT_LIBTOOL),false)
LOCAL_LIBS+=-lssl -lcrypto
endif # BK_WANT_LIBTOOL
else
LOCAL_DEFINES+=-DNO_SSL
endif # BK_USING_SSL

ifneq ($(BK_WANT_LIBTOOL),false)

# If we are using libtool, we can rely on libraries to specify their own
# dependencies, and therefore should specify only the minimal and absolutely
# required libraries on the program link line (bkmk/Make.productions ignores
# this stuff for modules and shared libraries, but not for programs).
#
# We must do this in order for bttcp to run even if the postgres/xml/etc.
# libraries are missing.

# <TRICKY>We don't want -lbkssl or -lbkxml for most of the programs here, and
# some of the programs don't need *anything* (e.g. timeout); however, some
# programs (e.g. bttcp) do need -lbkssl.  To manage this mess, we use a fairly
# obscure GNU make feature, Target-specific Variables, to set variables
# differently for different targets.  Note that this won't necessarily do what
# we want if there are previous := assignments using the variable we're trying
# to override, but it works until somebody breaks it by getting fancy.</TRICKY>

# basic dependencies for typical baka programs
LDLIBS=$(filter-out -lbkssl -lssl -lcrypto -lbkxml -lxml2 -lm -lz,$(BK_ALLLIBS))

# these are not really baka programs, despite their names
bk_daemon bk_funi timeout: LDLIBS=

ifneq ($(strip $(BK_USING_SSL)),false)
bttcp: LDLIBS=$(filter-out -lbkxml -lxml2 -lm -lz,$(BK_ALLLIBS))
endif

endif # BK_WANT_LIBTOOL

