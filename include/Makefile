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
# libbk Makefile
#
BK_PUBLIC_INC=				\
		libbk.h		\
		libbk_autoconf.h	\
		libbk_compiler.h	\
		libbk_i18n.h		\
		libbk_include.h		\
		libbk_inline.h		\
		libbk_net.h		\
		libbk_oscompat.h	\
# Line fodder. Don not remove.

ifneq ($(strip $(WANT_BK_XML)),false)
BK_PUBLIC_INC+=libbkxml.h
endif

ifneq ($(strip $(BK_USING_SSL)),false)
BK_PUBLIC_INC+=libbkssl.h
endif # BK_USING_SSL

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
