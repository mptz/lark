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

#include <stdio.h>

#include <util/memutil.h>
#include <util/message.h>

#include "memloc.h"
#include "term.h"

static struct term *
term_alloc(enum term_variety variety)
{
	struct term *term = xmalloc(sizeof *term);
	term->variety = variety;
	return term;
}

struct term *
TermAbs(symbol_mt formal, struct term *body)
{
	struct term *term = term_alloc(TERM_ABS);
	term->abs.formal = formal;
	term->abs.body = body;
	return term;
}

struct term *
TermApp(struct term *fun, struct term *arg)
{
	struct term *term = term_alloc(TERM_APP);
	term->app.fun = fun;
	term->app.arg = arg;
	return term;
}

struct term *
TermBoundVar(int index, symbol_mt name)
{
	struct term *term = term_alloc(TERM_BOUND_VAR);
	term->bv.index = index;
	term->bv.name = name;
	return term;
}

struct term *
TermFreeVar(symbol_mt name)
{
	struct term *term = term_alloc(TERM_FREE_VAR);
	term->fv.name = name;
	return term;
}

void term_print(const struct term *term)
{
	switch (term->variety) {
	case TERM_ABS:
		printf("\\%s. ", symtab_lookup(term->abs.formal));
		term_print(term->abs.body);
		break;
	case TERM_APP:
		putchar('(');
		term_print(term->app.fun);
		fputs(") (", stdout);
		term_print(term->app.arg);
		putchar(')');
		break;
	case TERM_BOUND_VAR:
		printf("%d<%s>", term->bv.index, symtab_lookup(term->bv.name));
		break;
	case TERM_FREE_VAR:
		printf("%s@%s", symtab_lookup(term->fv.name), memloc(term));
		break;
	default:
		panicf("Unhandled term variety %d\n", term->variety);
	}
}
