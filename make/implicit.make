#
# Project-specific implicit rules for builds.
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

LIBTOOL := libtool

#
# Replacement for make's builtin CC rule; adds dependency generation.
# The '-Wp' option is used to pass arguments to the preprocessor.  We
# need to go straight to the preprocessor to do this right because
# the compiler's -MD generates the dependency file in the current
# directory, not in the same directory as the source file.
#
%.o %.lo: %.c
	$(LIBTOOL) --mode=compile $(CC) $(CPPFLAGS) $(CFLAGS) -Wp,-MD,$(<:.c=.d) -c -o $@ $<

GPERF := gperf
#
# gperf has many flags.  A few of them are:
#
#       -c      Use 'strncmp' instead of 'strcmp'.
#       -C      Make lookup tables constant.
#       -E      Define constant with enum instead of #defines.
#       -I      Include <string.h> (which is needed).
#	-k	Select character positions for hash function.
#       -L      Generated language name.
#       -o      Reorder keywords.
#       -t      Use user-defined 'struct' in input file.
#
GPERFFLAGS := -c -C -E -I -L ANSI-C -o -t
GPERFKEYS := '*'

%.c: %.gperf
	$(GPERF) $(GPERFFLAGS) -k $(GPERFKEYS) -H $(basename $(notdir $^))_hash -N $(basename $(notdir $^))_lookup < $^ > $@

#
# Rule for bison (yacc) generated files.  For some reason make's
# implicit rule is foo.y -> foo.c, which deviates from the standard
# bison foo.y -> foo.tab.c.  Also, newer versions of bison strip the
# path from the input file name, requiring a special rule unless
# bison is run in the output directory.
#
# Remove the implicit rule by declaring its pattern with no actions.
#
YACC := bison

%.c: %.y
%.tab.c %.tab.h: %.y
	$(YACC) $(YFLAGS) -Wall -Werror -d -p $(notdir $(<:.y=_yy)) -t $< -o $(<:.y=.tab.c)

#
# As above, for flex (lex) generated files.
#
LEX := flex

#
# Flex options:
# -s	: suppress default rule (which copies unrecognized text to stdout)
#
# Actually, not using this option b/c the catch-all rules I use (which I
# need in order to output line number) trigger a warning.
#
# LFLAGS := -s

%.c: %.l
%.lex.c: %.l
	$(LEX) $(LFLAGS) -P$(notdir $(<:.l=_yy)) -o$@ $<

#
# And for TeX.
#
TEX := tex
TEXFLAGS := -interaction nonstopmode

%.dvi: %.tex
	$(TEX) $(TEXFLAGS) -output-directory $(dir $<) $<

DVIPDF := dvipdf

%.pdf: %.dvi
	$(DVIPDF) $(DVIPDFFLAGS) $< $@

#
# Rules for test outputs; our test infrastructure revolves around capturing
# standard output & error and comparing to expected values (plus, each test
# tool must complete its run successfully).
#

#
# Many tests don't produce output on stderr; rather than check in a bunch
# of empty files, we create these as needed.  Touching an existing error
# file won't cause any harm other than making us re-run some diffs.
#
%.err:
	touch $@

%.diff: %.ref %.runout %.err %.runerr
	diff -u $*.ref $*.runout
	diff -u $*.err $*.runerr
