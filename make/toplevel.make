#
# toplevel.make: invoke top-level make from a subdirectory
#
# Copyright (c) 2001-2007 Michael P. Touloumtzis.
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

# Rules for building from within a subdirectory.  We want to invoke
# the top-level make, but with the rules restricted to this
# subdirectory only.
#
MAKE := make
MAKEFLAGS := --no-print-directory
top-make := $(MAKE) $(MAKEFLAGS) -C$(MAKEPATH)
MAKEPATH :=

ifeq (,$(MAKECMDGOALS))
MAKECMDGOALS := build
endif

# Modify the command-line make goals to be suitable for the top-level
# Makefile.  Special 'phony' goals have '@' prepended as a top-level
# Makefile convention to keep them out of the file namespace.
#
phony-names := build clean default distclean install scrub test testclean
phony-goals := $(filter $(phony-names),$(MAKECMDGOALS))
file-goals := $(filter-out $(phony-names),$(MAKECMDGOALS))

goals := $(addprefix $(subdir)@,$(phony-goals)) \
	$(addprefix $(subdir),$(file-goals))

# Assign an empty rule to all mentioned targets, but make them all
# depend on run-top-make, which just invokes the top-level Makefile.
#
.PHONY: $(MAKECMDGOALS) run-top-make
$(MAKECMDGOALS): run-top-make
	@:
run-top-make:
	$(top-make) $(goals)
