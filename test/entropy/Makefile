######################################################################
#
#
# ++Copyright BAKA++
#
# Copyright © 2002-2011 The Authors. All rights reserved.
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
BK_LARGE_PROG=ent
BK_LARGE_SRC=ent.c iso8859.c randtest.c

GROUPTOP=../..
GROUPSUBDIR=test/entropy

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

ifneq ($(BK_WANT_LIBTOOL),false)

# If we are using libtool, we can rely on libraries to specify their own
# dependencies, and therefore should specify only the minimal and absolutely
# required libraries on the program link line (bkmk/Make.productions ignores
# this stuff for modules and shared libraries, but not for programs).
#
# We must do this in order for ent to run even if the postgres/xml/etc.
# libraries are missing.

PKG_LIBS:=

THIRD_PARTY_LIBS:=

endif # BK_WANT_LIBTOOL

LOCAL_LIBS=
BK_LOCALJUNK=*.bak

enttest:	ent
	entest.sh
	diff entest.master entest.bak
