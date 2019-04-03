#
# Copyright (c) 2007 Michael P. Touloumtzis.
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
# $(module-target) must contain the module test target (no path).
# $(module-srcs) must contain a list of the current module's test basenames.
# $(module-deps), if present, contains the test's build dependencies.
#

# Assign $(module) and hook us into the master test module tree.
#
module := $(subdir)$(module-target)
all-test-modules += $(module)

# Create variants of the prerequisites with paths attached.  Currently
# $(module-target-path) is always the same as $(module), but this may
# change at some point.
#
module-srcs-path := $(addprefix $(subdir),$(module-srcs))
module-target-path := $(subdir)$(module-target)

# These variants (suffixed with the module name) will enter the global
# scope, whereas the above files will be reset with each inclusion of
# module.make.
#
module-tests-$(module) := $(addsuffix .test,$(module-srcs-path))
module-actuals-$(module) := $(addsuffix .actual,$(module-srcs-path))
module-diffs-$(module) := $(addsuffix .diff,$(module-srcs-path))
module-target-$(module) := $(module-target-path)

# We mark the test output files SECONDARY so make doesn't delete them,
# since when tests fail we want to get a look at the output.
#
.SECONDARY: $(module-actuals-$(module))
.INTERMEDIATE: $(module-diffs-$(module))

# Build rules for the module's target.
#
$(module-target-path): $(module-target-path:.pass=.diff)
	if [ -s $< ]; then cat $<; false; fi
	cp $< $@
	: PASS: $@
$(module-target-path:.pass=.diff): $(module-diffs-$(module))
	cat /dev/null $^ > $@

# Handle main build targets.  Each module depends on its target build
# path.  $(subdir)@test ends up depending the build target of each test
# module with $(subdir).
#
.PHONY: $(module)@build $(subdir)@test
.INTERMEDIATE: $(module)@build $(subdir)@test
$(module)@build: $(module-target-path)
$(subdir)@test: $(module)@build

# Set up build dependencies on other modules.  The module's 'test'
# executables, which are invoked through implicit rules, depend on the
# listed build dependencies.
#
$(module-tests-$(module)): $(addprefix src/,$(addsuffix /@build,$(module-deps)))

# Set up the various clean targets.  This uses target-specific variables
# called 'clean-*' on each of the module's clean, distclean, and scrub
# targets.
#
module-clean-$(module) := $(module-actuals-$(module)) \
	$(module-diffs-$(module)) $(module-target-path) \
	$(module-target-path:.pass=.diff)
$(subdir)@clean: clean-files := $(clean-files) $(module-clean-$(module))
$(subdir)@testclean: clean-files := $(clean-files) $(module-clean-$(module))
