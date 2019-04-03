#
# standard-module.make: rules for standard module types.
#
# Copyright (c) 2001-2007 Mike Touloumtzis.
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

# binary module (module-type 'binary') prerequisites:
#
# module-target: name of output file (no path)
# module-srcs: list of source files
# module-c: shorthand to replace both module-target and
#	module-srcs; contains a list of .c files; module
#	output binary is the first item in the list
#	minus the trailing .c.
# module-project-libs: library dependencies within this project
#

# Massage a simple-binary module into an ordinary binary module.
#
ifeq ($(module-type),simple-binary)
ifneq ($(module-c),)
module-srcs := $(module-c)
module-target := $(basename $(firstword $(module-c)))
endif
module-type := binary
endif

ifeq ($(module-type),binary)
include make/module.make
$(module-target-path): $(module-objs-$(module))
	$(CC) -o $@ $(LDFLAGS) $(filter %.o,$^) $(filter %.a,$^) $(LOADLIBES) $(LDLIBS)
include make/unmodule.make
module-c :=
endif

ifeq ($(module-type),library)
include make/module.make
$(module-target-path): $(module-objs-$(module))
	ar crS $@ $(filter %.o,$^)
	ranlib $@
include make/unmodule.make
endif

# The shared library link line includes all of the compile flags as well;
# according to the GCC documentation, linking a shared object may require
# stubs to be compiled.  We want these compiles to use the same flags, or
# "subtle defects" may result.
#
ifeq ($(module-type),shared-library)
include make/module.make
ifeq ($(platform-is-cygwin),)
$(module-target-path): $(filter %.lo,$(module-objs-$(module)))
	$(LIBTOOL) --mode=link $(CC) -o $@ $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -rpath /usr/local/lib $^ $(LOADLIBES) $(LDLIBS)
else
$(module-target-path): $(filter %.o,$(module-objs-$(module)))
	$(CC) -o $@ $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -shared $^ $(LOADLIBES) $(LDLIBS)
endif
include make/unmodule.make
endif

# A test's target is a module-wide '.pass' file which is created only if all
# of the module's '.diff' files are empty (i.e. if comparison of expected
# vs. actual results is empty for all tests).
#
ifeq ($(module-type),test)
include make/test-module.make
include make/unmodule.make
endif

module-type :=
