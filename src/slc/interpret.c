/*
 * Copyright (c) 2009-2022 Michael P. Touloumtzis.
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

#include <assert.h>
#include <stdio.h>

#include "interpret.h"
#include "term.h"

static void interpret_bool(const struct term *term)
{
	if (term->variety != TERM_ABS ||
	    (term = term->abs.body, term->variety != TERM_ABS) ||
	    (term = term->abs.body, term->variety != TERM_BOUND_VAR))
		return;
	/* we expect the term to be closed */
	assert(term->bv.index == 0 || term->bv.index == 1);
	printf("read: %s\n", term->bv.index ? "True" : "False");
}

static void interpret_int(const struct term *term)
{
	/* Integers start with three abstractions */
	if (term->variety != TERM_ABS ||
	    (term = term->abs.body, term->variety != TERM_ABS) ||
	    (term = term->abs.body, term->variety != TERM_ABS))
		return;
	term = term->abs.body;

	/* Application of the outermost function means negative sign */
	int sign = 1;
	if (term->variety == TERM_APP &&
	    term->app.fun->variety == TERM_BOUND_VAR &&
	    term->app.fun->bv.index == 2) {
		term = term->app.arg;
		sign = -1;
	}

	/* Then repeated self-application to the argument */
	int n;
	for (n = 0; 1; term = term->app.arg, ++n) {
		if (term->variety == TERM_BOUND_VAR && term->bv.index == 0) {
			printf("read: %c%d\n", sign > 0 ? '+' : '-', n);
			return;
		}
		if (term->variety != TERM_APP ||
		    term->app.fun->variety != TERM_BOUND_VAR ||
		    term->app.fun->bv.index != 1)
			return;
	}
}

static void interpret_nat(const struct term *term)
{
	/* Church numerals start with two abstractions */
	if (term->variety != TERM_ABS ||
	    (term = term->abs.body, term->variety != TERM_ABS))
		return;
	term = term->abs.body;

	/* Then repeated self-application to the argument */
	unsigned n;
	for (n = 0; 1; term = term->app.arg, ++n) {
		if (term->variety == TERM_BOUND_VAR && term->bv.index == 0) {
			printf("read: %u\n", n);
			return;
		}
		if (term->variety != TERM_APP ||
		    term->app.fun->variety != TERM_BOUND_VAR ||
		    term->app.fun->bv.index != 1)
			return;
	}
}

void interpret(const struct term *term)
{
	interpret_bool(term);
	interpret_nat(term);
	interpret_int(term);
}
