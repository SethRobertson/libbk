######################################################################
# $Id: Makefile,v 1.20 2002/09/05 19:20:54 seth Exp $
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
# libbk test Makefile
#
BK_SUBDIR=entropy

# <TRICKY>The funky path for test_fun is intentional; don't remove</TRICKY>
BK_SIMPLE_INTERNALPROGS=		\
		test_bio		\
		test_config		\
		test_errorstuff		\
		../test/test_fun	\
		test_fun		\
		test_getbyfoo		\
		test_ioh		\
		test_locks 		\
		test_printbuf 		\
		testrandidea		\
		test_string	 	\
		test_stringconv 	\
		test_time	 	\
		test_iospeed		\
		test_url	 	\

GROUPTOP=..
GROUPSUBDIR=test
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
