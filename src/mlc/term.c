/*
 * Copyright (c) 2009-2024 Michael P. Touloumtzis.
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

#include <util/memutil.h>
#include <util/message.h>

#include "memloc.h"
#include "prim.h"
#include "term.h"

static struct term *
term_alloc(enum term_variety variety)
{
	struct term *term = xmalloc(sizeof *term);
	term->variety = variety;
	return term;
}

struct term *
TermAbs(size_t nformals, symbol_mt *formals,
	size_t nbodies, struct term **bodies)
{
	struct term *term = term_alloc(TERM_ABS);
	assert(nformals > 0);
	term->abs.nformals = nformals;
	term->abs.formals = formals;	/* take ownership */
	term->abs.nbodies = nbodies;
	term->abs.bodies = bodies;	/* take ownership */
	return term;
}

struct term *
TermApp(struct term *fun, size_t nargs, struct term **args)
{
	struct term *term = term_alloc(TERM_APP);
	term->app.fun = fun;
	term->app.nargs = nargs;
	term->app.args = args;		/* take ownership */
	return term;
}

struct term *
TermBoundVar(int up, int across, symbol_mt name)
{
	struct term *term = term_alloc(TERM_BOUND_VAR);
	term->bv.up = up;
	term->bv.across = across;
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

struct term *
TermFix(size_t nformals, symbol_mt *formals,
	size_t nbodies, struct term **bodies)
{
	struct term *term = term_alloc(TERM_FIX);
	assert(nformals > 0);
	term->abs.nformals = nformals;
	term->abs.formals = formals;	/* take ownership */
	term->abs.nbodies = nbodies;
	term->abs.bodies = bodies;	/* take ownership */
	return term;
}

struct term *
TermNum(double num)
{
	struct term *term = term_alloc(TERM_NUM);
	term->num = num;
	return term;
}

struct term *
TermPrim(unsigned prim)
{
	struct term *term = term_alloc(TERM_PRIM);
	term->prim = prim;
	return term;
}

struct term *
TermTest(struct term *pred,
	 size_t ncsqs, struct term **csqs,
	 size_t nalts, struct term **alts)
{
	struct term *term = term_alloc(TERM_TEST);
	term->test.pred = pred;
	term->test.ncsqs = ncsqs;
	term->test.csqs = csqs;		/* take ownership */
	term->test.nalts = nalts;
	term->test.alts = alts;		/* take ownership */
	return term;
}

void term_print(const struct term *term)
{
	switch (term->variety) {
	case TERM_ABS:
		putchar('[');
		for (size_t i = 0; i < term->abs.nformals; ++i)
			printf("%s%s", i > 0 ? ", " : "",
			       symtab_lookup(term->abs.formals[i]));
		putchar('?');
		for (size_t i = 0; i < term->abs.nbodies; ++i) {
			fputs(i > 0 ? ", " : " ", stdout);
			term_print(term->abs.bodies[i]);
		}
		putchar(']');
		break;
	case TERM_APP:
		putchar('(');
		term_print(term->app.fun);
		fputs(") (", stdout);
		for (size_t i = 0; i < term->app.nargs; ++i) {
			if (i > 0) fputs(", ", stdout);
			term_print(term->app.args[i]);
		}
		putchar(')');
		break;
	case TERM_BOUND_VAR:
		printf("%s<%d.%d>", symtab_lookup(term->bv.name),
		       term->bv.up, term->bv.across);
		break;
	case TERM_FREE_VAR:
		/*
		 * XXX why print memloc() for a free variable?  Aren't
		 * all free instances of e.g. 'x' the same?
		 */
		printf("%s@%s", symtab_lookup(term->fv.name), memloc(term));
		break;
	case TERM_FIX:
		printf("[%s! ", symtab_lookup(term->abs.formals[0]));
		for (size_t i = 1; i < term->abs.nformals; ++i)
			printf("%s%s", i > 1 ? ", " : "",
			       symtab_lookup(term->abs.formals[i]));
		putchar('?');
		for (size_t i = 0; i < term->abs.nbodies; ++i) {
			fputs(i > 0 ? ", " : " ", stdout);
			term_print(term->abs.bodies[i]);
		}
		putchar(']');
		break;
	case TERM_NUM:
		printf("%g", term->num);
		break;
	case TERM_PRIM:
		printf("<%s>", prim_symbol(term->prim));
		break;
	case TERM_TEST:
		putchar('[');
		term_print(term->test.pred);
		for (size_t i = 0; i < term->test.ncsqs; ++i) {
			fputs(i > 0 ? ", " : ". ", stdout);
			term_print(term->test.csqs[i]);
		}
		for (size_t i = 0; i < term->test.nalts; ++i) {
			fputs(i > 0 ? ", " : " | ", stdout);
			term_print(term->test.alts[i]);
		}
		putchar(']');
		break;
	default:
		panicf("Unhandled term variety %d\n", term->variety);
	}
}
