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
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include <util/message.h>

#include "alloc.h"
#include "heap.h"
#include "term.h"

struct term the_error_term = { .type = ERR };

struct term *
Abs(symbol_mt formal, struct term *body)
{
	struct term *term = term_alloc(body, NULL);
	term->type = ABS;
	term->abs.formal = formal;
	term->abs.body = body;
	return term;
}

struct term *
App(struct term *fun, struct term *arg)
{
	struct term *term = term_alloc(fun, arg);
	term->type = APP;
	term->app.fun = fun;
	term->app.arg = arg;
	return term;
}

struct term *
Sym(symbol_mt name)
{
	struct term *term = term_alloc(NULL, NULL);
	term->type = SYM;
	term->sym.name = name;
	term->sym.body = NULL;
	return term;
}

struct term *
Var(symbol_mt name)
{
	struct term *term = term_alloc(NULL, NULL);
	term->type = VAR;
	term->var.name = name;
	term->var.index = 0;
	return term;
}

struct term *
VarI(int index)
{
	struct term *term = term_alloc(NULL, NULL);
	term->type = VAR;
	term->var.name = 0;
	term->var.index = index;
	return term;
}

static void term_print_wrapped(const struct term *term)
{
	putchar('(');
	term_print(term);
	putchar(')');
}

static void term_print_helper(const struct term *term, bool nest)
{
	switch (term->type) {
	case ERR:
		fputs("#error", stdout);
		break;
	case ABS:
		if (!nest) putchar('\\');
		fputs(symtab_lookup(term->abs.formal), stdout);
		bool nestdown = term->abs.body->type == ABS;
		if (!nestdown) putchar('.');
		putchar(' ');
		term_print_helper(term->abs.body, nestdown);
		break;
	case APP:
		if (term->app.fun->type == ABS)
			term_print_wrapped(term->app.fun);
		else
			term_print(term->app.fun);
		putchar(' ');
		if (term->app.arg->type == ABS ||
		    term->app.arg->type == APP)
			term_print_wrapped(term->app.arg);
		else
			term_print(term->app.arg);
		break;
	case GBG:
		fputs("#garbage", stdout);
		break;
	case SYM:
		printf("%s := ", symtab_lookup(term->sym.name));
		term_print(term->sym.body);
		break;
	case VAR:
		if (term->var.name)
			fputs(symtab_lookup(term->var.name), stdout);
		else
			printf("#%d", term->var.index);
		break;
	default:
		panic("Invalid term type while printing\n");
	}
}

void term_print(const struct term *term)
{
	term_print_helper(term, false);
}

void term_print_indexed(const struct term *term)
{
	switch (term->type) {
	case ABS:
		putchar('(');
		putchar('\\');
		putchar(' ');
		term_print_indexed(term->abs.body);
		putchar(')');
		break;
	case APP:
		putchar('(');
		term_print_indexed(term->app.fun);
		putchar(' ');
		term_print_indexed(term->app.arg);
		putchar(')');
		break;
	case VAR:
		printf("%d", term->var.index);
		break;
	default:
		fputs("Invalid term type while printing index\n", stderr);
		exit(EXIT_FAILURE);
	}
}

/*
 * Perform the work of converting a term to de Bruijn indexing.
 */
static int
term_index_helper(struct term *term, struct term *context)
{
	switch (term->type) {
	case ERR:
		return -1;
	case ABS: {
		struct term link = { .type = ABS };
		link.abs.formal = term->abs.formal;
		link.abs.body = context;
		return term_index_helper(term->abs.body, &link);
	}
	case APP:
		return (term_index_helper(term->app.fun, context) ||
			term_index_helper(term->app.arg, context))
			? -1 : 0;
	case VAR: {
		struct term *p;
		unsigned i;
		for (p = context, i = 0; p; p = p->abs.body, ++i)
			if (term->var.name == p->abs.formal) {
				term->var.index = i;
				return 0;
			}

		fprintf(stderr, "can't index free variable: %s\n",
			symtab_lookup(term->var.name));
		return -1;
	}
	default: panic("Invalid term type while indexing\n");
	}
}

void term_index(struct term *term)
{
	if (term_index_helper(term, NULL))
		term->type = ERR;
}
