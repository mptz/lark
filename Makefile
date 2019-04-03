#
# Master Makefile for Lark.
#
# Copyright (c) 2001-2019 Michael P. Touloumtzis.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#

# Make's default target is the first one listed in the Makefile.
# List the 'default' target at the very beginning of the Makefile
# to make sure nothing precedes it.
#
.PHONY: default
default: all

# ===========================
#	Toolchain Setup
# ===========================
#
# We specifically require not just C99, but GCC and GNU extensions.
#
# There are three separate build flavors: coverage, optimize, and the default,
# which is a debugging build.  Switching builds doesn't automatically clean
# object files from other configurations, so use e.g.:
#
# make scrub
# make CONFIG=optimize

CC := gcc
CFLAVOR := -std=gnu99

ifeq ($(CONFIG),coverage)
CONFIG_CFLAGS := -g -fprofile-arcs -ftest-coverage
CONFIG_DEFINES := DEBUG
CONFIG_LDFLAGS := -fprofile-arcs
else ifeq ($(CONFIG),optimize)
CONFIG_CFLAGS := -O3 -fomit-frame-pointer -fstrength-reduce -fstrict-aliasing
CONFIG_DEFINES := NDEBUG
else
CONFIG_CFLAGS := -g
CONFIG_DEFINES := DEBUG
CONFIG_LDFLAGS := -rdynamic
endif

DEFINES := $(CONFIG_DEFINES) _GNU_SOURCE _FILE_OFFSET_BITS=64
CWARN := -Wall -Wno-trigraphs -Wpointer-arith
CFLAGS := $(CFLAVOR) $(CWARN) $(CONFIG_CFLAGS) $(addprefix -D,$(DEFINES)) -I./src
LDFLAGS := -g $(CONFIG_LDFLAGS)

MAKE := make --no-builtin-rules
TOPLEVEL := y

# We'll need some tools from the local bin directory.
PATH := $(shell pwd)/bin:$(PATH)

# ===========================================
#	Subdirectory & Module Traversal
# ===========================================
#
# Now we get into enumerating subdirs, which contain modules; we discover
# modules by generating subdir Makefiles from m4 templates, then including
# them.

all-subdir-names := util
all-subdirs := $(patsubst %,src/%/,$(sort $(all-subdir-names)))

# The key variables which are populated as we bring in modules.
all-modules :=
all-deps :=
all-srcs :=
all-objs :=

# Set global variables here to make them visible to subdir Makefiles.
installdir := /usr/local/

# Common Makefiles
include make/platform.make
include make/implicit.make

# Module makefiles
M4 := m4
%file: make/init.m4 %file.m4 make/fini.m4
	$(M4) -P -Dm4_subdir=$(dir $@) $^ > $@
include $(addsuffix Makefile,$(all-subdirs))

# Include auto-generated C include dependencies as Makefiles.  We include
# them unconditionally, but the subdir Makefiles generate them using only
# whatever dependency (.d) files have been generated by previous compiles,
# so the overall effect is "opportunistic".
#
# The case where $(all-deps) is empty is just the first run of make on a
# completely clean tree in which we have not yet built the subdir Makefiles.
#
ifneq ($(all-deps),)
include $(all-deps)
endif

# DONE INCLUDING MAKEFILES AT THIS POINT--GRAPH IS NOW STABLE

# =========================
#	Build Targets
# =========================
#
# Main targets.  'all' and 'build' are synonymous.  Use 'nop' to generate
# the generated Makefiles (including Dependencies) and nothing else.

.PHONY: all build nop
all: build
build: $(addsuffix @build,$(all-modules))
nop: ;

# Conveniences build targets for commonly built subdirectories.
.PHONY: util
util: src/util/@build

# ========================
#	Test Targets
# ========================
#
# Test support at the global level is currently very rudimentary.

all-test := $(addsuffix @test,$(all-subdirs))
.PHONY: test $(all-test)
test: $(all-test)

# ===============================
#	Installation Targets
# ===============================
#
# Installation support at the toplevel is currently rudimentary.  We made
# the $(installdir) variable visible to subdirectory Makefiles earlier;
# they should have configured their installation targets and connected
# them to the subdirectory-level @install rule.

all-install := $(addsuffix @install,$(all-subdirs))
.PHONY: install $(all-install)
install: $(all-install)

# ===========================
#	Cleanup Targets
# ===========================
#
# We support three types of cleanup: 'clean', 'distclean', and 'scrub';
# each cleans a superset of the prior's targets.
#
# 'clean' removes object files, generated targets, and core dumps.
#
# 'distclean' (intended for distribution) additionally removes generated
# dependency files, but leaves source files generated by tools such as
# flex, bison, or project-specific code generators, allowing clean builds
# with reduced toolchain dependencies.
#
# 'scrub' removes all generated files including m4-generated Makefiles.
# To restore Makefiles after 'make scrub', run a toplevel 'make nop'.

all-clean := $(addsuffix @clean,$(all-subdirs))
all-distclean := $(addsuffix @distclean,$(all-subdirs))
all-scrub := $(addsuffix @scrub,$(all-subdirs))

.PHONY: clean distclean scrub $(all-clean) $(all-distclean) $(all-scrub)
clean: $(all-clean)
distclean: $(all-distclean)
scrub: $(all-scrub)
$(all-clean):
	rm -f $(strip $(clean-files)) # files
	rm -rf $(strip $(clean-subdirs)) # subdirs
$(all-distclean) $(all-scrub):
	rm -f $(strip $(clean-files)) # files

.PHONY: coreclean
clean distclean scrub: coreclean
	rm -f core.*

# ======================================
#	Makefile Debugging Targets
# ======================================

all-vars := $(addsuffix @vars,$(all-subdirs)) $(addsuffix @vars,$(all-modules))
.PHONY: vars $(all-vars)
.SILENT: vars $(all-vars)
vars: $(all-vars)
	echo all-modules: $(all-modules)
	echo all-test-modules: $(all-test-modules)
	echo all-deps: $(all-deps)
	echo all-srcs: $(all-srcs)
	echo all-objs: $(all-objs)
	echo platform-is-cygwin: $(platform-is-cygwin)
$(all-vars):
	echo module-srcs-$(subst @vars,,$@): $(module-srcs-$(subst @vars,,$@))
	echo module-deps-$(subst @vars,,$@): $(module-deps-$(subst @vars,,$@))
	echo module-objs-$(subst @vars,,$@): $(module-objs-$(subst @vars,,$@))