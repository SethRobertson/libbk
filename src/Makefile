######################################################################
# $Id: Makefile,v 1.12 2003/03/27 19:15:41 seth Exp $
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

# This guy provides a convenient hook for instantly creating "one-off" .c files for testing things.
# If you are writing a test program which excersizes and exemplifies a libbk feature, you should
# probably commit that progam to ../test.

BK_SIMPLE_PROGS=			\
	bttcp				\
	genrand				\
	mmcat				\
	proto				\
	timeout				\
	$(BK_TMP_PROG)			\
#line fodder eater. Do not remove.

GROUPTOP=..
GROUPSUBDIR=src


# This has to go here so we get the benefit of $(TEST) which is included by one of the above.
BK_TMP_PROG:=$(shell if [ -f tmp.c ]; then echo tmp; fi)

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
