# The contents of this file are subject to the Netscape Public
# License Version 1.1 (the "License"); you may not use this file
# except in compliance with the License. You may obtain a copy of
# the License at http://www.mozilla.org/NPL/
#
# Software distributed under the License is distributed on an "AS
# IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
# implied. See the License for the specific language governing
# rights and limitations under the License.
#
# The Original Code is mozilla.org code.
#
# The Initial Developer of the Original Code is Netscape
# Communications Corporation.  Portions created by Netscape are
# Copyright (C) 1998 Netscape Communications Corporation. All
# Rights Reserved.
#
# Contributor(s): Stephen Lamm

# Build the Mozilla client.
#
# This needs CVSROOT set to work, e.g.,
#   setenv CVSROOT :pserver:anonymous@cvs-mirror.mozilla.org:/cvsroot
# or
#   setenv CVSROOT :pserver:username%somedomain.org@cvs.mozilla.org:/cvsroot
# 
# To checkout and build a tree,
#    1. cvs co mozilla/client.mk
#    2. cd mozilla
#    3. gmake -f client.mk
#
# Other targets (gmake -f client.mk [targets...]),
#    checkout
#    build
#    clean (realclean is now the same as clean)
#    distclean
#
# See http://www.mozilla.org/build/unix.html for more information.
#
# Options:
#   MOZ_OBJDIR           - Destination object directory
#   MOZ_CO_DATE          - Date tag to use for checkout (default: none)
#   MOZ_CO_MODULE        - Module to checkout (default: SeaMonkeyAll)
#   MOZ_CVS_FLAGS        - Flags to pass cvs (default: -q -z3)
#   MOZ_CO_FLAGS         - Flags to pass after 'cvs co' (default: -P)
#   MOZ_MAKE_FLAGS       - Flags to pass to $(MAKE)
#   MOZ_CO_BRANCH        - Branch tag (Depricated. Use MOZ_CO_TAG below.)
#

#######################################################################
# Checkout Tags
#
# For branches, uncomment the MOZ_CO_TAG line with the proper tag,
# and commit this file on that tag.
#MOZ_CO_TAG = <tag>
NSPR_CO_TAG = NSPRPUB_CLIENT_BRANCH
PSM_CO_TAG = SECURITY_CLIENT_BRANCH
LDAPCSDK_CO_TAG = LDAPCSDK_40_BRANCH
BUILD_MODULES = all

#######################################################################
# Defines
#
CWD := $(shell pwd)
ifneq (, $(wildcard client.mk))
# Ran from mozilla directory
ROOTDIR   := $(shell dirname $(CWD))
TOPSRCDIR := $(CWD)
else
# Ran from mozilla/.. directory (?)
ROOTDIR   := $(CWD)
TOPSRCDIR := $(CWD)/mozilla
endif

AUTOCONF := autoconf
MKDIR := mkdir
SH := /bin/sh
ifndef MAKE
MAKE := gmake
endif

CONFIG_GUESS_SCRIPT := $(wildcard $(TOPSRCDIR)/build/autoconf/config.guess)
ifdef CONFIG_GUESS_SCRIPT
  CONFIG_GUESS = $(shell $(CONFIG_GUESS_SCRIPT))
else
  _IS_FIRST_CHECKOUT := 1
endif

####################################
# CVS

# Add the CVS root to CVS_FLAGS if needed
CVS_ROOT_IN_TREE := $(shell cat $(TOPSRCDIR)/CVS/Root 2>/dev/null)
ifneq ($(CVS_ROOT_IN_TREE),)
ifneq ($(CVS_ROOT_IN_TREE),$(CVSROOT))
  CVS_FLAGS := -d $(CVS_ROOT_IN_TREE)
endif
endif

CVSCO = $(strip cvs $(CVS_FLAGS) co $(CVS_CO_FLAGS))
CVSCO_LOGFILE := $(ROOTDIR)/cvsco.log

ifdef MOZ_CO_TAG
  CVS_CO_FLAGS :=  -r $(MOZ_CO_TAG)
endif

####################################
# Load mozconfig Options

# See build pages, http://www.mozilla.org/build/unix.html, 
# for how to set up mozconfig.
MOZCONFIG_LOADER := mozilla/build/autoconf/mozconfig2client-mk
MOZCONFIG_FINDER := mozilla/build/autoconf/mozconfig-find 
MOZCONFIG_MODULES := mozilla/build/unix/modules.mk
run_for_side_effects := \
  $(shell cd $(ROOTDIR); \
     if test "$(_IS_FIRST_CHECKOUT)"; then \
        $(CVSCO) $(MOZCONFIG_FINDER) $(MOZCONFIG_LOADER) $(MOZCONFIG_MODULES); \
     else true; \
     fi; \
     $(MOZCONFIG_LOADER) $(TOPSRCDIR) mozilla/.mozconfig.mk > mozilla/.mozconfig.out)
include $(TOPSRCDIR)/.mozconfig.mk
include $(TOPSRCDIR)/build/unix/modules.mk

####################################
# Options that may come from mozconfig

# Change CVS flags if anonymous root is requested
ifdef MOZ_CO_USE_MIRROR
  CVS_FLAGS := -d :pserver:anonymous@cvs-mirror.mozilla.org:/cvsroot
endif

# MOZ_CVS_FLAGS - Basic CVS flags
ifeq "$(origin MOZ_CVS_FLAGS)" "undefined"
  CVS_FLAGS := $(CVS_FLAGS) -q -z 3
else
  CVS_FLAGS := $(MOZ_CVS_FLAGS)
endif

# This option is depricated. The best way to have client.mk pull a tag
# is to set MOZ_CO_TAG (see above) and commit that change on the tag.
ifdef MOZ_CO_BRANCH
  CVS_CO_FLAGS :=  -r $(MOZ_CO_BRANCH)
endif

# MOZ_CO_FLAGS - Anything that we should use on all checkouts
ifeq "$(origin MOZ_CO_FLAGS)" "undefined"
  CVS_CO_FLAGS := $(CVS_CO_FLAGS) -P
else
  CVS_CO_FLAGS := $(CVS_CO_FLAGS) $(MOZ_CO_FLAGS)
endif

ifdef MOZ_CO_DATE
  CVS_CO_DATE_FLAGS := -D "$(MOZ_CO_DATE)"
endif

ifeq "$(origin MOZ_MAKE_FLAGS)" "undefined"
  MOZ_MAKE_ENV :=
else
  MOZ_MAKE_ENV := MAKE="$(MAKE) $(MOZ_MAKE_FLAGS)"
endif

ifdef MOZ_OBJDIR
  OBJDIR := $(MOZ_OBJDIR)
  MOZ_MAKE := $(MOZ_MAKE_ENV) $(MAKE) -C $(OBJDIR)
else
  OBJDIR := $(TOPSRCDIR)
  MOZ_MAKE := $(MOZ_MAKE_ENV) $(MAKE)
endif

####################################
# CVS defines for PSM
#
PSM_CO_MODULE= mozilla/security
PSM_CO_FLAGS := -P
ifdef PSM_CO_TAG
  PSM_CO_FLAGS := $(PSM_CO_FLAGS) -r $(PSM_CO_TAG)
endif
CVSCO_PSM = cvs $(CVS_FLAGS) co $(PSM_CO_FLAGS) $(CVS_CO_DATE_FLAGS) $(PSM_CO_MODULE)

####################################
# CVS defines for NSPR
#
NSPR_CO_MODULE = mozilla/nsprpub
NSPR_CO_FLAGS := -P
ifdef NSPR_CO_TAG
  NSPR_CO_FLAGS := $(NSPR_CO_FLAGS) -r $(NSPR_CO_TAG)
endif
CVSCO_NSPR = cvs $(CVS_FLAGS) co $(NSPR_CO_FLAGS) $(CVS_CO_DATE_FLAGS) $(NSPR_CO_MODULE)

####################################
# CVS defines for the C LDAP SDK
#
LDAPCSDK_CO_MODULE = DirectorySDKSourceC
LDAPCSDK_CO_FLAGS := -P
ifdef LDAPCSDK_CO_TAG
  LDAPCSDK_CO_FLAGS := $(LDAPCSDK_CO_FLAGS) -r $(LDAPCSDK_CO_TAG)
endif
CVSCO_LDAPCSDK = cvs $(CVS_FLAGS) co $(LDAPCSDK_CO_FLAGS) $(CVS_CO_DATE_FLAGS) $(LDAPCSDK_CO_MODULE)

####################################
# CVS defines for standalone modules
#
ifneq ($(BUILD_MODULES),all)
  MOZ_CO_MODULE := $(filter-out $(NSPRPUB_DIR) security, $(BUILD_MODULE_DIRS))
  MOZ_CO_MODULE += allmakefiles.sh client.mk aclocal.m4 configure configure.in
  MOZ_CO_MODULE += Makefile.in
  MOZ_CO_MODULE := $(addprefix mozilla/, $(MOZ_CO_MODULE))
ifeq (,$(filter $(NSPRPUB_DIR), $(BUILD_MODULE_DIRS)))
  CVSCO_NSPR :=
endif
ifeq (,$(filter security, $(BUILD_MODULE_DIRS)))
  CVSCO_PSM :=
endif
endif

####################################
# CVS defines for SeaMonkey
#
ifeq ($(MOZ_CO_MODULE),)
  MOZ_CO_MODULE := SeaMonkeyAll
endif
CVSCO_SEAMONKEY := $(CVSCO) $(CVS_CO_DATE_FLAGS) $(MOZ_CO_MODULE)

#######################################################################
# Rules
# 

# Print out any options loaded from mozconfig.
all build checkout clean depend distclean export install realclean::
	@if test -f .mozconfig.out; then \
	  cat .mozconfig.out; \
	  rm -f .mozconfig.out; \
	else true; \
	fi

ifdef _IS_FIRST_CHECKOUT
all:: checkout build
else
all:: checkout depend build
endif

# Windows equivalents
pull_all: checkout
build_all: build
clobber clobber_all: clean
pull_and_build_all: checkout depend build

# Do everything from scratch
everything: checkout clean build

####################################
# CVS checkout
#
checkout::
	@: Backup the last checkout log.
	@if test -f $(CVSCO_LOGFILE) ; then \
	  mv $(CVSCO_LOGFILE) $(CVSCO_LOGFILE).old; \
	else true; \
	fi
	@echo "checkout start: "`date` | tee $(CVSCO_LOGFILE)
	@echo '$(CVSCO) mozilla/client.mk'; \
        cd $(ROOTDIR); \
	$(CVSCO) mozilla/client.mk && \
	$(MAKE) -f mozilla/client.mk real_checkout

real_checkout:
	@: Start the checkout. Split the output to the tty and a log file. \
	 : If it fails, touch an error file because "tee" hides the error.
	@failed=.cvs-failed.tmp; rm -f $$failed*; \
	cvs_co() { echo "$$@" ; \
	  ("$$@" || touch $$failed) 2>&1 | tee -a $(CVSCO_LOGFILE) && \
	  if test -f $$failed; then false; else true; fi; }; \
	cvs_co $(CVSCO_NSPR) && \
	cvs_co $(CVSCO_PSM) && \
        cvs_co $(CVSCO_LDAPCSDK) && \
	cvs_co $(CVSCO_SEAMONKEY)
	@echo "checkout finish: "`date` | tee -a $(CVSCO_LOGFILE)
	@: Check the log for conflicts. ;\
	conflicts=`egrep "^C " $(CVSCO_LOGFILE)` ;\
	if test "$$conflicts" ; then \
	  echo "$(MAKE): *** Conflicts during checkout." ;\
	  echo "$$conflicts" ;\
	  echo "$(MAKE): Refer to $(CVSCO_LOGFILE) for full log." ;\
	  false; \
	else true; \
	fi


####################################
# Web configure

WEBCONFIG_FILE  := $(HOME)/.mozconfig

MOZCONFIG2CONFIGURATOR := build/autoconf/mozconfig2configurator
webconfig:
	@cd $(TOPSRCDIR); \
	url=`$(MOZCONFIG2CONFIGURATOR) $(TOPSRCDIR)`; \
	echo Running netscape with the following url: ;\
	echo ;\
	echo $$url ;\
	netscape -remote "openURL($$url)" || netscape $$url ;\
	echo ;\
	echo   1. Fill out the form on the browser. ;\
	echo   2. Save the results to $(WEBCONFIG_FILE)

#####################################################
# First Checkout

ifdef _IS_FIRST_CHECKOUT
# First time, do build target in a new process to pick up new files.
build::
	$(MAKE) -f $(TOPSRCDIR)/client.mk build
else

#####################################################
# After First Checkout


####################################
# Configure

CONFIG_STATUS := $(wildcard $(OBJDIR)/config.status)
CONFIG_CACHE  := $(wildcard $(OBJDIR)/config.cache)

ifdef RUN_AUTOCONF_LOCALLY
EXTRA_CONFIG_DEPS := \
	$(TOPSRCDIR)/aclocal.m4 \
	$(TOPSRCDIR)/build/autoconf/gtk.m4 \
	$(TOPSRCDIR)/build/autoconf/altoptions.m4 \
	$(NULL)

$(TOPSRCDIR)/configure: $(TOPSRCDIR)/configure.in $(EXTRA_CONFIG_DEPS)
	@echo Generating $@ using autoconf
	cd $(TOPSRCDIR); $(AUTOCONF)
endif

CONFIG_STATUS_DEPS := \
	$(TOPSRCDIR)/configure \
	$(TOPSRCDIR)/allmakefiles.sh \
	$(TOPSRCDIR)/.mozconfig.mk \
	$(wildcard $(TOPSRCDIR)/mailnews/makefiles) \
	$(NULL)

# configure uses the program name to determine @srcdir@. Calling it without
#   $(TOPSRCDIR) will set @srcdir@ to "."; otherwise, it is set to the full
#   path of $(TOPSRCDIR).
ifeq ($(TOPSRCDIR),$(OBJDIR))
  CONFIGURE := ./configure
else
  CONFIGURE := $(TOPSRCDIR)/configure
endif

$(OBJDIR)/Makefile $(OBJDIR)/config.status: $(CONFIG_STATUS_DEPS)
	@if test ! -d $(OBJDIR); then $(MKDIR) $(OBJDIR); else true; fi
	@echo cd $(OBJDIR);
	@echo $(CONFIGURE)
	@cd $(OBJDIR) && $(CONFIGURE_ENV_ARGS) $(CONFIGURE) \
	  || ( echo "*** Fix above errors and then restart with\
               \"$(MAKE) -f client.mk build\"" && exit 1 )
	@touch $(OBJDIR)/Makefile

ifdef CONFIG_STATUS
$(OBJDIR)/config/autoconf.mk: $(TOPSRCDIR)/config/autoconf.mk.in
	cd $(OBJDIR); \
	  CONFIG_FILES=config/autoconf.mk ./config.status
endif


####################################
# Depend

depend:: $(OBJDIR)/Makefile $(OBJDIR)/config.status
	$(MOZ_MAKE) $@;

####################################
# Build it

build::  $(OBJDIR)/Makefile $(OBJDIR)/config.status
	$(MOZ_MAKE) export && $(MOZ_MAKE) install

####################################
# Other targets

# Pass these target onto the real build system
install export clean realclean distclean:: $(OBJDIR)/Makefile $(OBJDIR)/config.status
	$(MOZ_MAKE) $@

cleansrcdir:
	@cd $(TOPSRCDIR); \
	if [ -f Makefile ]; then \
	  $(MAKE) realclean && _skip_find=1; \
	fi; \
        if [ -f webshell/embed/gtk/Makefile ]; then \
          $(MAKE) -C webshell/embed/gtk distclean; \
        fi; \
	if [ ! "$$_skip_find" ]; then \
	  echo "Removing object files from srcdir..."; \
	  rm -fr `find . -type d \( -name .deps -print -o -name CVS \
	          -o -exec test ! -d {}/CVS \; \) -prune \
	          -o \( -name '*.[ao]' -o -name '*.so' \) -type f -print`; \
	fi; \
	echo "Removing files generated by configure..."; \
	build/autoconf/clean-config.sh;


# (! IS_FIRST_CHECKOUT)
endif

.PHONY: checkout real_checkout depend build export install clean realclean distclean cleansrcdir pull_all build_all clobber clobber_all pull_and_build_all everything
