######################################################################
# $Id: Makefile,v 1.30 2006/06/15 17:38:15 seth Exp $
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
# libbk test Makefile
#
BK_SUBDIR=entropy

# <TRICKY>The funky path for test_fun is intentional; don't remove</TRICKY>
BK_SIMPLE_INTERNALPROGS=		\
		../test/test_fun	\
		allocate_actual		\
		diskspeed		\
		mutex			\
		test_clc		\
		test_closerace		\
		test_config		\
		test_errorstuff		\
		test_fun		\
		test_getbyfoo		\
		test_ioh		\
		test_iospeed		\
		test_locks 		\
		test_mt19937		\
		test_printbuf 		\
		test_ringdir		\
		test_string	 	\
		test_stringconv 	\
		test_threads		\
		test_time	 	\
		test_url	 	\
		testhashspeed		\
		testrandidea		\
# Leave me -- line eater fodder

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
