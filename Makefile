######################################################################
# $Id: Makefile,v 1.2 2001/06/18 20:09:51 seth Exp $
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
# libbk group Makefile 
#
BK_SUBDIR=lib src include man test

GROUPTOP=.
GROUPSUBDIR=.
##################################################
## BEGIN BKSTANDARD MAKEFILE
-include $(GROUPTOP)/Make.preinclude
include $(PKGTOP)/bkmk/Make.include
-include $(PKGTOP)/Make.include
-include $(GROUPTOP)/Make.include
-include ./Make.include
## END BKSTANDARD MAKEFILE
##################################################
