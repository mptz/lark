/*
 * Copyright (c) 2009-2025 Michael P. Touloumtzis.
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

#include "binder.h"
#include "env.h"
#include "num.h"
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
TermAbs(size_t nformals, symbol_mt *formals, struct term *body)
{
	struct term *term = term_alloc(TERM_ABS);
	assert(nformals > 0);
	term->abs.nformals = nformals;
	term->abs.formals = formals;	/* take ownership */
	term->abs.body = body;
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
TermCell(size_t nelts, struct term **elts)
{
	struct term *term = term_alloc(TERM_CELL);
	term->cell.nelts = nelts;
	term->cell.elts = elts;		/* take ownership */
	return term;
}

struct term *
TermConstant(const struct binder *binder)
{
	struct term *term = term_alloc(TERM_CONSTANT);
	term->constant.binder = binder;
	return term;
}

struct term *
TermFix(size_t nformals, symbol_mt *formals, struct term *body)
{
	struct term *term = term_alloc(TERM_FIX);
	assert(nformals > 0);
	term->abs.nformals = nformals;
	term->abs.formals = formals;	/* take ownership */
	term->abs.body = body;
	return term;
}

struct term *
TermLet(size_t ndefs, symbol_mt *vars,
	struct term **vals, struct term *body)
{
	struct term *term = term_alloc(TERM_LET);
	assert(ndefs > 0);
	term->let.ndefs = ndefs;
	term->let.vars = vars;		/* take ownership */
	term->let.vals = vals;		/* take ownership */
	term->let.body = body;		/* take ownership */
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
TermPrim(const struct prim *prim)
{
	struct term *term = term_alloc(TERM_PRIM);
	term->prim = prim;
	return term;
}

struct term *
TermPruned(void)
{
	static struct term *pruned;
	if (pruned) return pruned;
	return pruned = term_alloc(TERM_PRUNED);
}

struct term *
TermString(const char *str)
{
	struct term *term = term_alloc(TERM_STRING);
	term->str = str;		/* take ownership */
	return term;
}

struct term *
TermSymbol(symbol_mt sym)
{
	struct term *term = term_alloc(TERM_SYMBOL);
	term->sym = sym;
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

struct term *
TermVar(int up, int across, symbol_mt name)
{
	struct term *term = term_alloc(TERM_VAR);
	term->var.up = up;
	term->var.across = across;
	term->var.name = name;
	return term;
}

void term_print(const struct term *term)
{
	switch (term->variety) {
	case TERM_ABS:
		putchar('[');
		/* skip unused 0th (self-reference) formal */
		for (size_t i = 1; i < term->abs.nformals; ++i)
			printf("%s%s", i > 1 ? ", " : "",
			       symtab_lookup(term->abs.formals[i]));
		fputs(". ", stdout);
		term_print(term->abs.body);
		putchar(']');
		break;
	case TERM_APP:
		putchar('(');
		term_print(term->app.fun);
		fputs(") (", stdout);
		for (size_t i = 0; i < term->app.nargs; ++i) {
			if (i) fputs(", ", stdout);
			term_print(term->app.args[i]);
		}
		putchar(')');
		break;
	case TERM_CELL:
		putchar('[');
		for (size_t i = 0; i < term->cell.nelts; ++i) {
			if (i) fputs(" | ", stdout);
			term_print(term->cell.elts[i]);
		}
		putchar(']');
		break;
	case TERM_CONSTANT:
		printf("%s<%zu>",
		       symtab_lookup(term->constant.binder->name),
		       term->constant.binder->index);
		break;
	case TERM_FIX:
		printf("[%s! ", symtab_lookup(term->abs.formals[0]));
		for (size_t i = 1; i < term->abs.nformals; ++i)
			printf("%s%s", i > 1 ? ", " : "",
			       symtab_lookup(term->abs.formals[i]));
		fputs(". ", stdout);
		term_print(term->abs.body);
		putchar(']');
		break;
	case TERM_LET:
		fputs("let {", stdout);
		for (size_t i = 1; i < term->let.ndefs; ++i) {
			printf("%s%s := ", i > 1 ? ". " : "",
			       symtab_lookup(term->let.vars[i]));
			term_print(term->let.vals[i]);
		}
		fputs("} ", stdout);
		term_print(term->let.body);
		break;
	case TERM_NUM:
		num_print(term->num);
		break;
	case TERM_PRIM:
		printf("'%s'", term->prim->name);
		break;
	case TERM_PRUNED:
		fputs("$pruned", stdout);
		break;
	case TERM_STRING:
		printf("\"%s\"", term->str);
		break;
	case TERM_SYMBOL:
		printf("#%s", symtab_lookup(term->sym));
		break;
	case TERM_TEST:
		putchar('[');
		term_print(term->test.pred);
		for (size_t i = 0; i < term->test.ncsqs; ++i) {
			fputs(i ? ", " : "? ", stdout);
			term_print(term->test.csqs[i]);
		}
		for (size_t i = 0; i < term->test.nalts; ++i) {
			fputs(i ? ", " : " | ", stdout);
			term_print(term->test.alts[i]);
		}
		putchar(']');
		break;
	case TERM_VAR:
		printf("%s<%d.%d>", symtab_lookup(term->var.name),
		       term->var.up, term->var.across);
		break;
	default:
		panicf("Unhandled term variety %d\n", term->variety);
	}
}
