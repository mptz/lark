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

#include <stdbool.h>
#include <stdio.h>

#include <util/memutil.h>
#include <util/message.h>

#include "form.h"

static struct form *form_alloc(enum form_variety variety)
{
	struct form *form = xmalloc(sizeof *form);
	form->variety = variety;
	return form;
}

struct form *FormAbs(symbol_mt formal, struct form *body)
{
	struct form *form = form_alloc(FORM_ABS);
	form->abs.formal = formal;
	form->abs.body = body;
	return form;
}

struct form *FormApp(struct form *fun, struct form *arg)
{
	struct form *form = form_alloc(FORM_APP);
	form->app.fun = fun;
	form->app.arg = arg;
	return form;
}

struct form *FormVar(symbol_mt name)
{
	struct form *form = form_alloc(FORM_VAR);
	form->var.name = name;
	return form;
}

void form_free(struct form *form)
{
	switch (form->variety) {
	case FORM_ABS: form_free(form->abs.body); break;
	case FORM_APP: form_free(form->app.fun);
		       form_free(form->app.arg); break;
	case FORM_VAR: /* nada */; break;
	default: panicf("Unhandled form variety %d\n", form->variety);
	}
	form->variety = FORM_INVALID;	/* in case of accidental reuse */
	xfree(form);
}

static void form_print_wrapped(const struct form *form)
{
	putchar('(');
	form_print(form);
	putchar(')');
}

static void form_print_helper(const struct form *form, bool nest)
{
	switch (form->variety) {
	case FORM_ABS:
		if (!nest) putchar('\\');
		fputs(symtab_lookup(form->abs.formal), stdout);
		bool nestdown = form->abs.body->variety == FORM_ABS;
		if (!nestdown) putchar('.');
		putchar(' ');
		form_print_helper(form->abs.body, nestdown);
		break;
	case FORM_APP:
		if (form->app.fun->variety == FORM_ABS)
			form_print_wrapped(form->app.fun);
		else
			form_print(form->app.fun);
		putchar(' ');
		if (form->app.arg->variety == FORM_ABS ||
		    form->app.arg->variety == FORM_APP)
			form_print_wrapped(form->app.arg);
		else
			form_print(form->app.arg);
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
	form_print_helper(form, false);
}
