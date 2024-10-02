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
#include <string.h>

#include <util/message.h>
#include <util/wordbuf.h>

#include "env.h"
#include "form.h"
#include "readback.h"
#include "term.h"

/*
 * This is sensitive to the number of symbols generated in the past...
 * this makes testing fragile.  Should come up with a more stable approach.
 *	https://github.com/mptz/lark/issues/38
 */
static symbol_mt fresh_name(symbol_mt name, const struct wordbuf *names)
{
	size_t i, bound;
	if (env_test(name))
		goto freshen;
	for (i = 0, bound = wordbuf_used(names); i < bound; ++i)
		if (wordbuf_at(names, i) == name)
			goto freshen;
	return name;
freshen:
	return symtab_fresh(name);
}

static struct form *readback_term(const struct term *term,
				  struct wordbuf *names);

/*
 * We store variable names for bound variables in a wordbuf.  wordbufs
 * are 1-dimensional but bound variables are 2-dimensional; we push the
 * variable names at a given abtraction level followed by a count,
 * allowing us to walk upwards by skipping.
 */
static struct form *readback_abs(const struct term *abs, struct wordbuf *names)
{
	assert(abs->variety == TERM_ABS || abs->variety == TERM_FIX); /* XXX */
	struct form *params = NULL;
	for (size_t i = 0; i < abs->abs.nformals; ++i) {
		/*
		 * We don't need fresh names for placeholders; they
		 * aren't referenced by bound variables.
		 */
		symbol_mt formal =
			abs->abs.formals[i] == the_placeholder_symbol ?
			abs->abs.formals[i] :
			fresh_name(abs->abs.formals[i], names);
		wordbuf_push(names, formal);
		params = FormVarNext(formal, params);
	}
	wordbuf_push(names, abs->abs.nformals);

	struct form *bodies = NULL;
	for (size_t i = 0; i < abs->abs.nbodies; ++i) {
		struct form *body = readback_term(abs->abs.bodies[i], names);
		body->prev = bodies, bodies = body;
	}

	wordbuf_popn(names, abs->abs.nformals + 1 /* for formal count */);
	return FormAbs(params, bodies);
}

static struct form *readback_app(const struct term *app, struct wordbuf *names)
{
	assert(app->variety == TERM_APP);
	struct form *args = NULL;
	for (size_t i = 0; i < app->app.nargs; ++i) {
		struct form *arg = readback_term(app->app.args[i], names);
		assert(!arg->prev);
		arg->prev = args, args = arg;
	}
	if (app->app.fun->variety == TERM_PRIM) {
		assert(app->app.nargs == 1 || app->app.nargs == 2);
		return app->app.nargs == 1 ?
			FormOp1(app->app.fun->prim, args) :
			FormOp2(app->app.fun->prim, args->prev, args);
	}
	return FormApp(readback_term(app->app.fun, names), args,
		       FORM_SYNTAX_AUTO);
}

static struct form *readback_name(int up, int across, struct wordbuf *names)
{
	/*
	 * For each abstraction level we move up, skip over a list of
	 * parameters.  The current index always points to the count of
	 * parameters to skip; we add +1 when skipping to compensate for
	 * the count itself.
	 */
	assert(wordbuf_used(names) > 0);
	size_t index = wordbuf_used(names) - 1;
	while (up--) index -= wordbuf_at(names, index) + 1;
	/*
	 * At this point index is the index of the count of parameters
	 * at the correct abstraction depth; that count follows the last
	 * of those parameters in 'name'.  This count must be nonzero
	 * since we're looking up a variable at this depth.
	 *
	 *               index
	 *                 V
	 * /-+---+---+---+---+-/
	 * / | a | b | c | 3 | /
	 * /-+---+---+---+---+-/
	 */
	assert(index > 0);
	assert(wordbuf_at(names, index) > 0);
	assert(wordbuf_at(names, index) > across);
	assert(wordbuf_at(names, index) < wordbuf_used(names));
	symbol_mt name =
		wordbuf_at(names, index - wordbuf_at(names, index) + across);
	return FormVar(name);
}

static struct form *readback_test(const struct term *test,
				  struct wordbuf *names)
{
	assert(test->variety == TERM_TEST);
	struct form *csqs = NULL, *alts = NULL;
	for (size_t i = 0; i < test->test.ncsqs; ++i) {
		struct form *csq = readback_term(test->test.csqs[i], names);
		assert(!csq->prev);
		csq->prev = csqs, csqs = csq;
	}
	for (size_t i = 0; i < test->test.nalts; ++i) {
		struct form *alt = readback_term(test->test.alts[i], names);
		assert(!alt->prev);
		alt->prev = alts, alts = alt;
	}
	return FormTest(readback_term(test->test.pred, names), csqs, alts);
}

static struct form *readback_term(const struct term *term,
				  struct wordbuf *names)
{
	switch (term->variety) {
	case TERM_ABS: case TERM_FIX:	/* XXX? */
		return readback_abs(term, names);
	case TERM_APP:
		return readback_app(term, names);
	case TERM_BOUND_VAR:
		return readback_name(term->bv.up, term->bv.across, names);
	case TERM_FREE_VAR:
		return FormVar(term->fv.name);
	case TERM_NIL:
		return FormNil();
	case TERM_NUM:
		return FormNum(term->num);
	case TERM_PAIR:
		return FormPair(readback_term(term->pair.car, names),
				readback_term(term->pair.cdr, names));
	case TERM_PRIM:
		return FormPrim(term->prim);
	case TERM_TEST:
		return readback_test(term, names);
	default:
		panicf("Unhandled term variety %d\n", term->variety);
	}
}

struct form *readback(const struct term *term)
{
	struct wordbuf names;
	wordbuf_init(&names);
	struct form *retval = readback_term(term, &names);
	wordbuf_fini(&names);
	return retval;
}
