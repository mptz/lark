m4_divert(-1)
#
# init.m4: Makefile macro definitions and header.
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

# The above m4 (non-existent diversion in order to discard unwanted
# output in blocks of macro definitions) is apparently a "common
# programming idiom".

# Replace each newline in the argument with space-backslash-newline;
# needed to build variable assignments in make.
#
m4_define(`m4_backslashed', `m4_patsubst($1, `
', ` \\
')')

m4_define(`m4_assert_empty',
`# m4 macro: $0
ifneq (`$'(strip `$'($1)),)
`$'(error Makefile: `$0': variable "$1")
endif
# make sure variable is simply (not recursively) expanded
$1 :=
')

m4_define(`m4_assert_full',
`# m4 macro: $0
ifeq (`$'(strip `$'($1)),)
`$'(error Makefile: `$0': variable "$1")
endif
')

m4_define(`make_binary',
`# m4 macro: $0
module-target := $1
module-srcs := m4_backslashed($2)
module-project-libs := m4_backslashed($3)
module-libs := m4_backslashed($4)
module-type := binary
include make/standard-module.make
')

m4_define(`make_main_binary',
`# m4 macro: $0
module-target := $1
module-srcs := main.c m4_backslashed($2)
module-project-libs := m4_backslashed($3)
module-type := binary
include make/standard-module.make
')

m4_define(`make_simple_binary',
`# m4 macro: $0
module-c := $1
module-project-libs := m4_backslashed($2)
module-type := simple-binary
include make/standard-module.make
')

m4_define(`make_library',
`# m4 macro: $0
module-target := $1.a
module-srcs := m4_backslashed($2)
module-type := library
include make/standard-module.make
')

m4_define(`make_shared_library',
`# m4 macro: $0
module-target := lib$1.`$'(platform-so-extension)
module-srcs := m4_backslashed($2)
module-project-libs := m4_backslashed($3)
module-type := shared-library
include make/standard-module.make
')

m4_define(`make_test',
`# m4 macro: $0
module-target := $1.pass
module-srcs := m4_backslashed($2)
module-deps := m4_backslashed($3)
module-type := test
include make/standard-module.make
')

m4_divert`'m4_dnl
# this is a generated file; do not edit!

m4_assert_empty(subdir)
m4_assert_empty(subdir-deps)
m4_assert_empty(module-target)

# subdir path comes from a command line macro
subdir := m4_subdir

# boilerplate (from init.m4) to include top-level make
ifndef TOPLEVEL
MAKEPATH := $(subst /, ,$(subdir))
MAKEPATH := $(patsubst %,..,$(MAKEPATH))
MAKEPATH := $(subst $(empty) ,/,$(MAKEPATH))/
include $(MAKEPATH)make/toplevel.make
else

######################################################################
# module specific code begins here
######################################################################
