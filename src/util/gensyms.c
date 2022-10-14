/*
 * Copyright (c) 2009-2019 Michael P. Touloumtzis.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>

#include "symtab.h"

#define NUMSYMS 100
#define NUMFRESH 5

int main(int argc, char *argv[])
{
	symtab_intern("unused");	/* initialize */
	symtab_intern("genC");		/* ensure skipped */
	for (int i = NUMSYMS; i--; /* nada */) {
		symbol_mt sym = symtab_gensym();
		puts(symtab_lookup(sym));
		symtab_intern("genAB");		/* ensure skipped */
	}
	symbol_mt x = symtab_intern("x");
	for (int i = NUMFRESH; i--; /* nada */) {
		symbol_mt fresh = symtab_fresh(x);
		puts(symtab_lookup(fresh));
	}
	symbol_mt xAq = symtab_intern("xAq");	/* prev. output */
	for (int i = NUMFRESH; i--; /* nada */) {
		symbol_mt fresh = symtab_fresh(xAq);
		puts(symtab_lookup(fresh));
	}
	for (int i = NUMSYMS; i--; /* nada */) {
		symbol_mt sym = symtab_gensym();
		puts(symtab_lookup(sym));
	}

	return 0;
}
