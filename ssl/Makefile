######################################################################
# $Id: Makefile,v 1.2 2003/06/03 17:47:38 lindauer Exp $
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
BK_LARGE_LIB=libbkssl$(LIBEXT)

BK_LARGE_LIBSRC=		       		\
		b_ssl.c				\
# Line eater fodder -- leave

LT_LDLIBS=-lbk -lssl -lcrypto

GROUPTOP=..
GROUPSUBDIR=ssl
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
