m4_divert(-1)
#
# fini.m4: Makefile epilogue.
#
# Copyright (c) 2001 Michael P. Touloumtzis.
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
m4_divert
######################################################################
# subdir specific code ends here
######################################################################

m4_assert_full(subdir)

$(subdir)@distclean: clean-files := $(clean-files) $(subdir)Dependencies
$(subdir)@scrub: clean-files := $(clean-files) $(subdir)Makefile

# Create directory-wide dependencies file
D := $(subdir)Dependencies
$(D): subdir := $(subdir)
.SECONDARY: $(subdir-deps)
d := $(wildcard $(subdir)*.d)
ifeq ($(d),)
$(D):
	touch $@
else
$(D): $(d)
	make/depend.sh $@ $(filter %.d,$^)
endif

all-deps += $(D)
D :=
d :=

# Subdirectory-level test exploration.
# Since tests for all modules are combined in an 'test/' subdir, we
# don't know which tests map to which modules--so we associate test runs
# with the subdir-level @test target.

test-refs := $(sort $(wildcard $(subdir)test/*.ref))
test-results := $(test-refs:.ref=.runout) $(test-refs:.ref=.runerr)
.SECONDARY: $(test-results)
$(subdir)@testclean: clean-files := $(clean-files) $(test-results)
$(subdir)@test: $(test-refs:.ref=.diff)

# For MLC and eventually other projects: do the same for test libraries.
# These libraries contain separate reference files for each test, but we
# concatenate them for the full test run.

test-libs := $(sort $(wildcard $(subdir)test/test-*.lib))
test-refs := $(test-libs:.lib=.ref)
.SECONDARY: $(test-refs)
$(subdir)@testclean: clean-files := $(clean-files) $(test-refs)
$(subdir)@test: $(test-refs:.ref=.diff)

test-libs :=
test-refs :=
test-results :=

# Subdir-specific implicit rule; we can't add a global implicit rule
# because the target's path contains 'test' whereas we're looking in
# the parent subdir for the binary--I can't find a way for make to handle
# this case.
#
# The outcome: if your module builds the binary 'foo' you can use it as a
# test by creating test/foo.ref (and optionally test/foo.err).
#
$(subdir)test/%.runout $(subdir)test/%.runerr: $(subdir)%
	$< > $(@D)/$*.runout 2> $(@D)/$*.runerr

subdir :=
subdir-deps :=

# ifndef TOPLEVEL ... else ...
endif
