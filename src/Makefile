######################################################################
# $Id: Makefile,v 1.29 2004/06/03 21:30:56 seth Exp $
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
	b_chill				\
	bdtee				\
	bk_daemon			\
	bk_funi				\
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
#
# Guess what?  It is already broken.
#
PKG_LIBS:=
LOCAL_LIBS:=
THIRD_PARTY_LIBS:=

ifneq ($(strip $(BK_USING_SSL)),false)
bttcp: BK_LIBS := -lbkssl $(BK_LIBS)
endif

# No, this doesn't fucking work either.  Fullrelease kills it
#bk_daemon timeout: BK_LIBS :=
#bk_daemon timeout: PTHREAD_LIBS :=
#bk_daemon timeout: PTHREAD_CFLAGS :=
#bk_daemon timeout: PTHREAD_CC := $(CC_CONFIG)

endif # BK_WANT_LIBTOOL

