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
BK_LARGE_LIB=libbkssl$(LIBEXT)

BK_LARGE_LIBSRC=				\
		b_ssl.c				\
# Line eater fodder -- leave

LT_LDLIBS=-lbk -lssl -lcrypto

GROUPTOP=..
GROUPSUBDIR=ssl

# *BSD openssl header files have old-style prototypes
override BK_NO_STRICT_PROTOTYPES=true

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
