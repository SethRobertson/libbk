######################################################################
# $Id: Makefile,v 1.14 2002/01/19 12:42:34 dupuy Exp $
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
BK_SIMPLE_PROGS=			\
		test_bio		\
		test_config		\
		test_errorstuff		\
		test_getbyfoo		\
		test_ioh		\
		test_locks 		\
		test_printbuf 		\
		test_stringconv 	\
		test_time	 	\
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
