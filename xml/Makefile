######################################################################
#
#
# ++Copyright BAKA++
#
# Copyright © 2003-2019 The Authors. All rights reserved.
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
BK_LARGE_LIB=libbkxml$(LIBEXT)

BK_LARGE_LIBSRC=				\
		b_minimize.c			\
		b_search.c			\
# Line eater fodder -- leave

LT_LDLIBS=-lbk -lxml2

GROUPTOP=..
GROUPSUBDIR=xml
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

LOCAL_INCS+=$(shell $(XML2CONFIG) --cflags)
LOCAL_LIBS+=$(shell $(XML2CONFIG) --libs)
