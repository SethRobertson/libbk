######################################################################
# $Id: Makefile,v 1.11 2003/06/17 06:07:15 seth Exp $
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
# libbk group Makefile
#
BK_INCLUDE_DIRS=include
BK_SUBDIR=Packages_Defined_Below

GROUPTOP=.
GROUPSUBDIR=.

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

BK_SUBDIR=lib xml
ifneq ($(strip $(BK_USING_SSL)),false)
BK_SUBDIR+=ssl # This must come *after* lib in list
endif # BK_USING_SSL
BK_SUBDIR+=src man test
