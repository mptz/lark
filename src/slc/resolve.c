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
#include <stdio.h>
#include <stdlib.h>

#include <util/memutil.h>
#include <util/message.h>
#include <util/wordbuf.h>

#include "env.h"
#include "form.h"
#include "memloc.h"
#include "node.h"
#include "term.h"

struct def_bindings {
	size_t ndefs;
	struct term **vars;
	int *subs;		/* bound variable indexes for vars */
};

static struct term *term_bind(struct term *term,
			      const struct def_bindings *bindings,
			      int depth)
{
	switch (term->variety) {
	case TERM_ABS:
		term->abs.body = term_bind(term->abs.body, bindings, depth+1);
		break;
	case TERM_APP:
		term->app.fun = term_bind(term->app.fun, bindings, depth);
		term->app.arg = term_bind(term->app.arg, bindings, depth);
		break;
	case TERM_BOUND_VAR:
		break;
	case TERM_FREE_VAR:
		for (size_t i = bindings->ndefs; i--; /* nada */)
			if (bindings->vars[i] == term)
				return TermBoundVar(bindings->subs[i] + depth,
						    term->fv.name);
		break;
	default:
		panicf("Unhandled term variety %d\n", term->variety);
	}
	return term;
}

static struct term *lift(struct term *term, struct wordbuf *defs)
{
	/*
	 * Sort the definitions by environment index so we can ensure
	 * definition precedeces reference.
	 */
	struct def_bindings bindings = { .ndefs = wordbuf_used(defs) };
	qsort(defs->data, bindings.ndefs, sizeof defs->data[0], env_entry_cmp);
	bindings.vars = xmalloc(sizeof *bindings.vars * bindings.ndefs);
	bindings.subs = xmalloc(sizeof *bindings.subs * bindings.ndefs);

	size_t i, j;
	for (i = bindings.ndefs, j = 0; i--; j++) {
		const struct env_entry *ee = (void*) wordbuf_at(defs, i);
		assert(ee->var->variety == TERM_FREE_VAR);
		assert(ee->val);
		bindings.vars[i] = ee->var;
		bindings.subs[i] = j;
	}

	struct term *innermost = NULL, *hole = term;
	assert(j == bindings.ndefs);
	while (j--) {
		struct term *abs = TermAbs(bindings.vars[j]->fv.name, term);
		if (!innermost) innermost = abs;
		const struct env_entry *ee = (void*) wordbuf_at(defs, j);
		term = TermApp(abs, ee->val);
	}
	hole = term_bind(hole, &bindings, 0);
	if (innermost)
		innermost->abs.body = hole;

	free(bindings.vars);
	free(bindings.subs);
	return term;
}

struct context {
	struct context *prev;
	symbol_mt binder;
};

static int context_lookup(const struct context *context, symbol_mt name)
{
	int height;
	for (height = 0; context; ++height, context = context->prev)
		if (context->binder == name)
			return height;
	return -1;
}

/*
 * form_convert, as its name suggestions converts forms to terms.  It
 * determines which variables are free vs. bound, extending the global
 * environment as necessary.  It doesn't perform any substitutions of
 * global definitions, however; it simply gathers the environment
 * entries of global definitions referenced by 'form' in 'defs'.
 */
static struct term *form_convert(const struct form *form,
				 struct wordbuf *defs,
				 struct context *context)
{
	switch (form->variety) {
	case FORM_ABS: {
		struct context link = { .prev = context,
					.binder = form->abs.formal };
		return TermAbs(form->abs.formal,
			form_convert(form->abs.body, defs, &link));
	}
	case FORM_APP:
		return TermApp(form_convert(form->app.fun, defs, context),
			       form_convert(form->app.arg, defs, context));
	case FORM_VAR: {
		int height = context_lookup(context, form->var.name);
		if (height >= 0)
			return TermBoundVar(height, form->var.name);

		struct env_entry ee = env_declare(form->var.name);
		assert(ee.var);
		assert(ee.var->variety == TERM_FREE_VAR);
		if (ee.val) {
			struct env_entry *pe = xmalloc(sizeof *pe);
			*pe = ee;
			wordbuf_push(defs, (word) pe);
		}
		return ee.var;
	}
	default:
		panicf("Unhandled form variety %d\n", form->variety);
	}
}

struct term *resolve(const struct form *form)
{
	struct wordbuf defs;
	wordbuf_init(&defs);

	struct term *term = form_convert(form, &defs, NULL);
	if (!term) goto done;	/* error message already printed */
	if (wordbuf_used(&defs) > 0) term = lift(term, &defs);
done:
	wordbuf_free_clear(&defs);
	wordbuf_fini(&defs);
	return term;
}
