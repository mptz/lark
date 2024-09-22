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
#include <stdbool.h>
#include <stdio.h>

#include <util/memutil.h>
#include <util/message.h>

#include "form.h"
#include "prim.h"

static struct form *form_alloc(enum form_variety variety)
{
	struct form *form = xmalloc(sizeof *form);
	form->variety = variety;
	form->syntax = FORM_SYNTAX_AUTO;
	form->prev = NULL;
	return form;
}

struct form *FormAbs(struct form *params, struct form *bodies)
{
	struct form *form = form_alloc(FORM_ABS);
	form->abs.self = NULL;
	form->abs.params = params;
	form->abs.bodies = bodies;
	return form;
}

struct form *FormApp(struct form *fun, struct form *args,
		     enum form_syntax syntax)
{
	struct form *form = form_alloc(FORM_APP);
	form->syntax = syntax;
	form->app.fun = fun;
	form->app.args = args;
	return form;
}

struct form *FormFix(struct form *self, struct form *params,
		     struct form *bodies)
{
	struct form *form = form_alloc(FORM_FIX);
	form->abs.self = self;
	form->abs.params = params;
	form->abs.bodies = bodies;
	return form;
}

struct form *FormNum(double num)
{
	struct form *form = form_alloc(FORM_NUM);
	form->num = num;
	return form;
}

struct form *FormOper(int op, struct form *lhs, struct form *rhs)
{
	struct form *form = form_alloc(FORM_OPER);
	form->oper.op = op;
	form->oper.lhs = lhs;
	form->oper.rhs = rhs;
	return form;
}

struct form *FormTest(struct form *pred, struct form *csq, struct form *alt)
{
	struct form *form = form_alloc(FORM_TEST);
	assert(form_length(pred) == 1);
	form->test.pred = pred;
	form->test.csq = csq;
	form->test.alt = alt;
	return form;
}

struct form *FormVar(symbol_mt name)
{
	struct form *form = form_alloc(FORM_VAR);
	form->var.name = name;
	return form;
}

static void form_free_rl(struct form *form)
{
	while (form) {
		struct form *tmp = form;
		form = form->prev;
		xfree(tmp);
	}
}

void form_free(struct form *form)
{
	switch (form->variety) {
	case FORM_ABS:	form_free_rl(form->abs.params);
			form_free_rl(form->abs.bodies); break;
	case FORM_APP:	form_free(form->app.fun);
			form_free_rl(form->app.args); break;
	case FORM_FIX:	form_free(form->abs.self);
			form_free_rl(form->abs.params);
			form_free_rl(form->abs.bodies); break;
	case FORM_NUM:	/* nada */; break;
	case FORM_OPER:	form_free(form->oper.lhs);
			form_free(form->oper.rhs); break;
	case FORM_TEST:	form_free(form->test.pred);
			form_free_rl(form->test.csq);
			form_free_rl(form->test.alt); break;
	case FORM_VAR:	/* nada */; break;
	default: panicf("Unhandled form variety %d\n", form->variety);
	}
	form->variety = FORM_INVALID;	/* in case of accidental reuse */
	xfree(form);
}

/*
 * Use a pointer-reversing traversal, printing on the way back.
 */
void form_print_lr(struct form *form)
{
	struct form *rev, *tmp;
	for (rev = NULL; form;
	     tmp = form->prev, form->prev = rev, rev = form, form = tmp);
	for (bool first = true; rev; first = false,
	     tmp = rev->prev, rev->prev = form, form = rev, rev = tmp) {
		if (!first) fputs(", ", stdout);
		form_print(rev);
	}
	assert(rev == NULL);
}

/*
 * Print the arguments parenthesized if their valence is greater than
 * 1 (or is 0, in which case we print empty parens), or if 'next' is
 * true which indicates we need to nest the current form in parens.
 */
static void form_print_args(struct form *args, bool nest)
{
	if (!args) {
		fputs("()", stdout);
		return;
	}
	bool wrap = nest || !!args->prev;
	if (wrap) putchar('(');
	form_print_lr(args);
	if (wrap) putchar(')');
}

/*
 * 'spine': Are we on the application spine?
 * 'nest': Should we nest an application in parens?
 */
static void form_print_helper(const struct form *form, bool spine, bool nest)
{
	switch (form->variety) {
	case FORM_ABS:
		assert(form->abs.self == NULL);
		putchar('[');
		form_print_lr(form->abs.params);
		putchar('?');
		putchar(' ');
		form_print_lr(form->abs.bodies);
		putchar(']');
		break;
	case FORM_APP:
		/*
		 * Applications can be in prefix notation:
		 *	f (x)
		 * ...or in postfix notation:
		 *	x; f
		 * ...which has a variant for a literal abstraction:
		 *	x [y? y]
		 * If the application was parsed from source, use the
		 * original syntax; otherwise make an educated guess.
		 */
		if (form->syntax == FORM_SYNTAX_PREFIX)
			goto app_print_prefix;
		if (form->syntax == FORM_SYNTAX_POSTFIX) {
			if (form->app.fun->variety == FORM_ABS)
				goto app_print_postfix_abs;
			goto app_print_postfix;
		}

		/*
		 * If the function is a literal abstraction, use the []
		 * style of printing the application.  'nest' is true
		 * since we'll need parentheses if we choose to print
		 * a sequenced application using ';'.
		 */
		if (form->app.fun->variety == FORM_ABS)
			goto app_print_postfix_abs;

		/*
		 * Otherwise we have to choose between f (x) and x; f.
		 */
#ifdef FORMS_PREFIX_FOR_NONUNARY
		/*
		 * Trying out a variation that only looks at the spine
		 * and will thus potentially use postfix form with
		 * non-unary applications like "(x, y); f"
		 */
		if (!spine || form_length(form->app.args) > 1)
#else
		if (!spine)
#endif	/* FORMS_PREFIX_FOR_NONUNARY */
			goto app_print_prefix;
		goto app_print_postfix;

		/*
		 * This business with the gotos is messy but I'm not
		 * convinced that I've got the grammar, parsing, and
		 * printing right yet so the incentive to clean it up
		 * is low.
		 */
	app_print_prefix:
		form_print_helper(form->app.fun, true, true);
		putchar(' ');
		form_print_args(form->app.args, true);
		break;
	app_print_postfix_abs:
		form_print_args(form->app.args, false);
		putchar(' ');
		form_print_helper(form->app.fun, true, true);
		break;
	app_print_postfix:
		if (nest) putchar('(');
		form_print_args(form->app.args, false);
		putchar(';');
		putchar(' ');
		form_print_helper(form->app.fun, false, false);
		if (nest) putchar(')');
		break;
	case FORM_FIX:
		putchar('[');
		form_print(form->abs.self);
		putchar('!');
		putchar(' ');
		form_print_lr(form->abs.params);
		putchar('?');
		putchar(' ');
		form_print_lr(form->abs.bodies);
		putchar(']');
		break;
	case FORM_NUM:
		printf("%g", form->num);
		break;
	case FORM_OPER:
		form_print_helper(form->oper.lhs, false, false);
		printf(" %s ", prim_symbol(form->oper.op));
		form_print_helper(form->oper.rhs, false, false);
		break;
	case FORM_TEST:
		putchar('[');
		form_print(form->test.pred);
		fputs(". ", stdout);
		form_print_lr(form->test.csq);
		fputs(" | ", stdout);
		form_print_lr(form->test.alt);
		putchar(']');
		break;
	case FORM_VAR:
		fputs(symtab_lookup(form->var.name), stdout);
		break;
	default:
		panicf("Unhandled form variety %d\n", form->variety);
	}
}

void form_print(const struct form *form)
{
	form_print_helper(form, true, false);
}

struct form *form_splice(struct form *a, struct form *b)
{
	if (!b) return a;
	struct form *curr = b, *prev;
	for (prev = curr->prev; prev; curr = prev, prev = prev->prev);
	curr->prev = a;
	return b;
}
