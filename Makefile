######################################################################
#
#
# ++Copyright BAKA++
#
# Copyright © 2001-2010 The Authors. All rights reserved.
#
# This source code is licensed to you under the terms of the file
# LICENSE.TXT in this release for further details.
#
# Send e-mail to <projectbaka@baka.org> for further information.
#
# - -Copyright BAKA- -
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

BK_SUBDIR=lib

# Suppress libbkxml.so
ifneq ($(strip $(WANT_BK_XML)),false)
BK_SUBDIR+=xml
endif

# Suppress libbkssl.so
ifneq ($(strip $(BK_USING_SSL)),false)
BK_SUBDIR+=ssl # This must come *after* lib in list
endif # BK_USING_SSL

# Suppress building of BK programs (like bttcp and bchill). Includes suppresion of test
ifneq ($(strip $(WANT_BK_PROCS)),false)
BK_SUBDIR+=src
# Even if BK programs are wanted, suppress building of test
ifneq ($(strip $(WANT_BK_TEST)),false)
BK_SUBDIR+=test
endif
endif

BK_SUBDIR+=man

# Must install completely before anything else.
install-first: install
