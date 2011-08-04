######################################################################
#
#
# ++Copyright BAKA++
#
# Copyright © 2001-2011 The Authors. All rights reserved.
#
# This source code is licensed to you under the terms of the file
# LICENSE.TXT in this release for further details.
#
# Send e-mail to <projectbaka@baka.org> for further information.
#
# - -Copyright BAKA- -
#
# libbk test Makefile
#
BK_SUBDIR=entropy

# <TRICKY>The funky path for test_fun is intentional; don't remove</TRICKY>
BK_SIMPLE_INTERNALPROGS=		\
		../test/test_fun	\
		allocate_actual		\
		app-timed-replay	\
		diskspeed		\
		ipctest			\
		mutex			\
		shmmap			\
		sourcesink		\
		test_bua		\
		test_bloomfilter	\
		test_clc		\
		test_closerace		\
		test_config		\
		test_errorstuff		\
		test_fun		\
		test_getbyfoo		\
		test_ioh		\
		test_iospeed		\
		test_locks		\
		test_mt19937		\
		test_patricia		\
		test_printbuf		\
		test_proc		\
		test_recursive_locks	\
		test_ringdir		\
		test_stats		\
		test_string		\
		test_string_expand	\
		test_stringconv		\
		test_syscall		\
		test_threads		\
		test_time		\
		test_url		\
		test_xml_comment	\
		testhashspeed		\
		testrandidea		\
		writev			\
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
