######################################################################
# $Id: Makefile,v 1.4 2003/03/26 16:46:06 jtt Exp $
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

LOCAL_LIBS=$(BK_LIBM)
BK_LOCALJUNK=*.bak

enttest:	ent
	entest.sh
	diff entest.master entest.bak
