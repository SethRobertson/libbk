######################################################################
# $Id: Makefile,v 1.11 2003/03/19 00:03:05 seth Exp $
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
BK_PUBLIC_INC=libbk.h libbk_autoconf.h libbk_include.h libbk_oscompat.h libbk_i18n.h libbk_net.h libbkxml.h

GROUPTOP=..
GROUPSUBDIR=include

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

libbk_autoconf.h:
	@$(RM) $@
	$(LN_S) $(GROUPTOP)/$(PKGTOP)/bkmk/$@ $@
