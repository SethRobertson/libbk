######################################################################
# $Id: Makefile,v 1.3 2003/06/17 05:10:49 seth Exp $
#
# ++Copyright SYSDETECT++
#
# Copyright (c) 2003 System Detection.  All rights reserved.
#
# THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SYSTEM DETECTION.
# The copyright notice above does not evidence any actual
# or intended publication of such source code.
#
# Only properly authorized employees and contractors of System Detection
# are authorized to view, possess, to otherwise use this file.
#
# System Detection
# 5 West 19th Street Floor 2K
# New York, NY 10011-4240
#
# +1 212 206-1900
# <support@sysd.com>
#
# --Copyright SYSDETECT--
#
# libbk Makefile
#
BK_LARGE_LIB=libbkxml$(LIBEXT)

BK_LARGE_LIBSRC=		       		\
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
