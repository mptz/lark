#
# Copyright (c) 2001-2022 Michael P. Touloumtzis.
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

# Prerequisites:
#
# $(subdir) must contain the current module's path relative to the top-level
#	build directory.
# $(module-target) must contain the module output target (no path).
#
# $(module-srcs) must contain a list of the current module's source files.
# $(module-libs), if present, contains link libraries of the current module
#	(e.g 'gdbm' to link with -lgdbm).
# $(module-project-libs), if present, contains link dependency libraries
#	of the current module.
#

#
# Assign $(module) and hook us into the master module tree.
#
module := $(subdir)$(module-target)
all-modules += $(module)

#
# Create variants of the prerequisites with paths attached.  Currently
# $(module-target-path) is always the same as $(module), but this may
# change at some point.
#
module-srcs-path := $(addprefix $(subdir),$(module-srcs))
module-target-path := $(subdir)$(module-target)

#
# These variants (suffixed with the module name) will enter the global
# scope, whereas the above files will be reset with each inclusion of
# module.make.
#
module-srcs-$(module) := $(module-srcs-path)
module-gperf-srcs-$(module) := $(filter %.gperf,$(module-srcs-path))
module-lex-srcs-$(module) := $(filter %.l,$(module-srcs-path))
module-yacc-srcs-$(module) := $(filter %.y,$(module-srcs-path))
module-c-srcs-$(module) := $(filter %.c,$(module-srcs-path)) \
			$(module-gperf-srcs-$(module):.gperf=.c) \
			$(module-lex-srcs-$(module):.l=.lex.c) \
			$(module-yacc-srcs-$(module):.y=.tab.c)
module-tex-srcs-$(module) := $(filter %.tex,$(module-srcs-path))

#
# Handle source files built by tools (like gperf).  Some of these tools
# are obscure enough that not everyone will have them installed; thus
# we mark the generated files as secondary, and don't clean them except
# when scrubbing.  Right here, we're just starting the list to be used
# later.
#
secondary-srcs :=

#
# Other generated C and TeX files for the secondary list.
#
secondary-srcs += $(module-gperf-srcs-$(module):.gperf=.c) \
		$(module-lex-srcs-$(module):.l=.lex.c) \
		$(module-yacc-srcs-$(module):.y=.tab.h) \
		$(module-yacc-srcs-$(module):.y=.tab.c) \
		$(module-tex-srcs-$(module):.tex=.log)

#
# Set up the generated files lists.  We only build dependencies from C
# source files; likewise with objects for now.
#
module-deps-$(module) := $(module-c-srcs-$(module):.c=.d)
module-objs-$(module) := $(module-c-srcs-$(module):.c=.o) \
	$(module-c-srcs-$(module):.c=.lo) \
	$(module-tex-srcs-$(module):.tex=.dvi)
ifeq ($(CONFIG),coverage)
module-objs-$(module) += $(module-c-srcs-$(module):.c=.gcda) \
	$(module-c-srcs-$(module):.c=.gcno)
endif
module-target-$(module) := $(module-target-path)

#
# Handle main build targets.  Each module depends on its target build
# path.  $(subdir)@build ends up depending on all modules in $(subdir).
#
.PHONY: $(module)@build $(subdir)@build
.INTERMEDIATE: $(module)@build $(subdir)@build
$(module)@build: $(module-target-path)
$(subdir)@build: $(module)@build

#
# Set up library dependencies.  For convenience we use certain libraries,
# such as the POSIX math and realtime extensions libraries, everywhere.
#
ubiquitous-libs := m rt
lib-names := $(addprefix -l,$(module-libs) \
			    $(ubiquitous-libs))
$(module-target-path): LDLIBS := $(lib-names)
lib-paths := $(addprefix src/,$(module-project-libs))
lib-names := $(patsubst %,/lib%.a,$(module-project-libs))
$(module-target-path): $(join $(lib-paths),$(lib-names))
lib-paths :=
lib-names :=

#
# Make all of the module's object files depend on generated header files.
# This is needed because we don't do a dependency scanning pass before we
# try the first build.
#
$(module-objs-$(module)): $(filter %.h,$(secondary-srcs))

#
# Set up the various clean targets.  This uses target-specific variables
# called 'clean-*' on each of the module's *clean targets.
#
.SECONDARY: $(secondary-srcs)
clean-files :=
clean-subdirs :=
$(subdir)@clean: $(subdir)@testclean
$(subdir)@clean: clean-files := $(clean-files) $(module-target-path) \
	$(module-objs-$(module))
$(subdir)@clean: clean-subdirs := $(subdir).libs
$(subdir)@distclean: clean-files := $(clean-files) $(module-deps-$(module))
$(subdir)@distclean: $(subdir)@clean
$(subdir)@scrub: clean-files := $(clean-files) $(secondary-srcs)
$(subdir)@scrub: $(subdir)@distclean
secondary-srcs :=

#
# Update per-subdirectory Makefile variables.
#
subdir-deps += $(module-deps-$(module))

#
# Update global, top-level Makefile variables.
# XXX should move to per-directory!
#
all-srcs += $(module-c-srcs-$(module)) $(module-tex-srcs-$(module))
all-objs += $(module-objs-$(module))
