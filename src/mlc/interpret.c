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
	    term->abs.nbodies != 1 ||
	    (term = term->abs.bodies[0], term->variety != TERM_ABS) ||
	    term->abs.nbodies != 1 ||
	    (term = term->abs.bodies[0], term->variety != TERM_BOUND_VAR))
		return;
	/* we expect the term to be closed */
	assert(term->bv.up == 0 || term->bv.up == 1);
	printf("read: %s\n", term->bv.up ? "True" : "False");
}

static void interpret_int(const struct term *term)
{
	/* Integers start with three abstractions */
	if (term->variety != TERM_ABS ||
	    term->abs.nbodies != 1 ||
	    (term = term->abs.bodies[0], term->variety != TERM_ABS) ||
	    term->abs.nbodies != 1 ||
	    (term = term->abs.bodies[0], term->variety != TERM_ABS) ||
	    term->abs.nbodies != 1)
		return;
	term = term->abs.bodies[0];

	/* Application of the outermost function means negative sign */
	int sign = 1;
	if (term->variety == TERM_APP &&
	    term->app.fun->variety == TERM_BOUND_VAR &&
	    term->app.fun->bv.up == 2 &&
	    term->app.nargs == 1) {
		/* XXX should be a test for unary app above? */
		/* XXX maybe this isn't even an int if not... */
		term = term->app.args[0];
		sign = -1;
	}

	/* Then repeated self-application to the argument */
	int n;
	for (n = 0; 1; term = term->app.args[0], ++n) {
		if (term->variety == TERM_BOUND_VAR && term->bv.up == 0) {
			printf("read: %c%d\n", sign > 0 ? '+' : '-', n);
			return;
		}
		if (term->variety != TERM_APP ||
		    term->app.fun->variety != TERM_BOUND_VAR ||
		    term->app.fun->bv.up != 1 ||
		    term->app.nargs != 1)
			return;
	}
}

static void interpret_nat(const struct term *term)
{
	/* Church numerals start with two unary abstractions */
	if (term->variety != TERM_ABS ||
	    term->abs.nformals != 1 ||
	    term->abs.nbodies != 1 ||
	    (term = term->abs.bodies[0], term->variety != TERM_ABS) ||
	    term->abs.nformals != 1 ||
	    term->abs.nbodies != 1)
		return;
	term = term->abs.bodies[0];

	/* Then repeated self-application to the argument */
	unsigned n;
	for (n = 0; 1; term = term->app.args[0], ++n) {
		if (term->variety == TERM_BOUND_VAR && term->bv.up == 0) {
			printf("read: %u\n", n);
			return;
		}
		if (term->variety != TERM_APP ||
		    term->app.fun->variety != TERM_BOUND_VAR ||
		    term->app.fun->bv.up != 1 ||
		    term->app.nargs != 1)
			return;
	}
}

void interpret(const struct term *term)
{
	interpret_bool(term);
	interpret_nat(term);
	interpret_int(term);
}
