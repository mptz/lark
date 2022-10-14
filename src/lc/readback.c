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

#include "readback.h"
#include "term.h"

static void readback_bool(const struct term *term)
{
	if (term->type != ABS ||
	    (term = term->abs.body, term->type != ABS) ||
	    (term = term->abs.body, term->type != VAR))
		return;
	/* we expect the term to be closed */
	assert(term->var.index == 0 || term->var.index == 1);
	printf("read: %s\n", term->var.index ? "TRUE" : "FALSE");
}

static void readback_int(const struct term *term)
{
	/* Integers start with three abstractions */
	if (term->type != ABS ||
	    (term = term->abs.body, term->type != ABS) ||
	    (term = term->abs.body, term->type != ABS))
		return;
	term = term->abs.body;

	/* Application of the outermost function means negative sign */
	int sign = 1;
	if (term->type == APP &&
	    term->app.fun->type == VAR &&
	    term->app.fun->var.index == 2) {
		term = term->app.arg;
		sign = -1;
	}

	/* Then repeated self-application to the argument */
	int n;
	for (n = 0; 1; term = term->app.arg, ++n) {
		if (term->type == VAR && term->var.index == 0) {
			printf("read: %c%d\n", sign > 0 ? '+' : '-', n);
			return;
		}
		if (term->type != APP ||
		    term->app.fun->type != VAR ||
		    term->app.fun->var.index != 1)
			return;
	}
}

static void readback_nat(const struct term *term)
{
	/* Church numerals start with two abstractions */
	if (term->type != ABS || (term = term->abs.body, term->type != ABS))
		return;
	term = term->abs.body;

	/* Then repeated self-application to the argument */
	int n;
	for (n = 0; 1; term = term->app.arg, ++n) {
		if (term->type == VAR && term->var.index == 0) {
			printf("read: %d\n", n);
			return;
		}
		if (term->type != APP ||
		    term->app.fun->type != VAR ||
		    term->app.fun->var.index != 1)
			return;
	}
}

void readback(const struct term *term)
{
	readback_bool(term);
	readback_nat(term);
	readback_int(term);
}
