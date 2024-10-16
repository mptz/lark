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
#include <stdlib.h>

#include <util/memutil.h>
#include <util/message.h>
#include <util/wordbuf.h>

#include "env.h"
#include "form.h"
#include "memloc.h"
#include "node.h"
#include "prim.h"
#include "term.h"

/*
 * Bind (some) free variables in the given term.  No effect on already-
 * bound variables.  Binds free variables at a uniform abstraction height
 * (immediately outside this term) i.e. to the formal parameters of an
 * abstraction containing this term as its body.  Free variables which
 * don't have definitions in the global environment are left as-is.
 */
static struct term *term_bind(struct term *term, int depth,
			      size_t nformals, symbol_mt *formals)
{
	switch (term->variety) {
	case TERM_ABS:
	case TERM_FIX:
		for (size_t i = term->abs.nbodies; i--; /* nada */)
			term->abs.bodies[i] =
				term_bind(term->abs.bodies[i], depth + 1,
					  nformals, formals);
		break;
	case TERM_APP:
		term->app.fun =
			term_bind(term->app.fun, depth, nformals, formals);
		for (size_t i = term->app.nargs; i--; /* nada */)
			term->app.args[i] =
				term_bind(term->app.args[i], depth,
					  nformals, formals);
		break;
	case TERM_BOUND_VAR:
		assert(term->bv.up >= 0);
		assert(term->bv.up < depth);
		break;
	case TERM_CELL:
		for (size_t i = term->cell.nelts; i--; /* nada */)
			term->cell.elts[i] =
				term_bind(term->cell.elts[i], depth,
					  nformals, formals);
		break;
	case TERM_FREE_VAR:
		for (size_t i = nformals; i--; /* nada */)
			if (formals[i] == term->fv.name)
				return TermBoundVar(depth, i, term->fv.name);
		break;
	case TERM_LET:
		term->let.body = term_bind(term->let.body, depth + 1,
					   nformals, formals);
		for (size_t i = term->let.ndefs; i--; /* nada */)
			term->let.vals[i] =
				term_bind(term->let.vals[i], depth,
					  nformals, formals);
		break;
	case TERM_NUM:
	case TERM_PRIM:
	case TERM_STRING:
		break;
	case TERM_TEST:
		term->test.pred = term_bind(term->test.pred, depth,
					    nformals, formals);
		for (size_t i = term->test.ncsqs; i--; /* nada */)
			term->test.csqs[i] =
				term_bind(term->test.csqs[i], depth,
					  nformals, formals);
		for (size_t i = term->test.nalts; i--; /* nada */)
			term->test.alts[i] =
				term_bind(term->test.alts[i], depth,
					  nformals, formals);
		break;
	default:
		panicf("Unhandled term variety %d\n", term->variety);
	}
	return term;
}

/*
 * Lift terms to resolve references to the global environment.  They
 * don't become closed (this calculator handles open terms fine) but we
 * do substitute global definitions by constructing a beta-redex around
 * the given term.  Note that each global has been previously lifted
 * (we do so before installing in the global environment) so we don't
 * have to worry about references among the arguments.
 */
static struct term *lift(struct term *term, struct wordbuf *defs)
{
	/*
	 * Sort the definitions by environment index so definition
	 * precedes reference; this is not strictly necessary since
	 * each global term is closed, but lifting according to
	 * global definition order is predictable, and more
	 * importantly it allows us to deduplicate free variables.
	 */
	size_t ndefs = wordbuf_used(defs);
	qsort(defs->data, ndefs, sizeof defs->data[0], env_entry_cmp);

	/*
	 * Now that the referenced definitions are sorted, we can
	 * remove and free duplicate entries.
	 */
	symbol_mt last = the_empty_symbol;
	for (size_t i = 0, j = 0; i < wordbuf_used(defs); ++i) {
		struct env_entry *ee = (void*) wordbuf_at(defs, i);
		if (ee->name == last) {
			free(ee);
			ndefs--;
		} else {
			defs->data[j++] = defs->data[i];
			last = ee->name;
		}
	}
	wordbuf_popn(defs, wordbuf_used(defs) - ndefs);

	/*
	 * Extract the names and values of definitions from the global
	 * environment into formal parameter & argument lists.
	 * XXX comment the +1 for formals (self-ref)
	 */
	symbol_mt *formals = xmalloc(sizeof *formals * ndefs + 1);
	struct term **args = xmalloc(sizeof *args * ndefs);
	for (size_t i = ndefs; i--; /* nada */) {
		const struct env_entry *ee = (void*) wordbuf_at(defs, i);
		assert(ee->var->variety == TERM_FREE_VAR);
		assert(ee->val);
		formals[i+1] = ee->var->fv.name;
		args[i] = ee->val;
	}
	formals[0] = the_empty_symbol;

	/*
	 * Create an abstraction wrapping the given term, with one formal
	 * parameter for each substitution from the global environment.
	 * Then wrap *that* in an application of the args to make a redex.
	 *
	 * Note that we don't need to free 'formals', 'args', or 'bodies'
	 * since term construction transfers ownership to the callee.
	 */
	struct term **bodies = xmalloc(sizeof *bodies);
	bodies[0] = term_bind(term, 0, ndefs+1, formals);
	return TermApp(TermAbs(ndefs+1, formals, 1, bodies), ndefs, args);
}

struct context {
	struct context *prev;
	size_t nbinders;
	symbol_mt *binders;
};

struct binding {
	int up, across;
};

static struct binding context_lookup(const struct context *context,
				     symbol_mt name)
{
	int up, across;
	for (up = 0; context; ++up, context = context->prev)
		for (across = 0; across < context->nbinders; ++across)
			if (context->binders[across] == name)
				return (struct binding)
				       { .up = up, .across = across };
	return (struct binding) { .up = -1, .across = -1 };;
}

static struct term *form_convert(const struct form *form,
				 struct wordbuf *defs,
				 struct context *context);

/*
 * Abstractions and fixpoint abstractions have the same structure
 * with the exception of the self-reference, which if present
 * becomes the 0th parameter of the construction abstraction term.
 */
static struct term *form_convert_abs(const struct form *form,
				     struct wordbuf *defs,
				     struct context *context)
{
	assert((form->variety == FORM_ABS && !form->abs.self) ||
	       (form->variety == FORM_FIX &&  form->abs.self));

	/* the +1 is for self-reference (parameter 0 is self) */
	size_t nformals = form_length(form->abs.params) + 1;

	symbol_mt *formals = xmalloc(sizeof *formals * nformals),
		  *fdst = formals + nformals;
	const struct form *param;
	for (param = form->abs.params; param; param = param->prev)
		*--fdst = param->var.name;
	*--fdst = form->abs.self ?
		form->abs.self->var.name :
		the_empty_symbol;
	assert(fdst == formals);

	struct context link = {
		.prev = context,
		.nbinders = nformals,
		.binders = formals,	/* freed when term is freed */
	};

	size_t nbodies = form_length(form->abs.bodies);
	assert(nbodies > 0);
	struct term **bodies = xmalloc(sizeof *bodies * nbodies),
		    **bdst = bodies + nbodies;
	const struct form *body;
	for (body = form->abs.bodies; body; body = body->prev)
		*--bdst = form_convert(body, defs, &link);
	assert(bdst == bodies);

	/* transfer ownership of 'formals' and 'bodies' */
	return form->abs.self ?
		TermFix(nformals, formals, nbodies, bodies) :
		TermAbs(nformals, formals, nbodies, bodies);
}

static struct term *form_convert_let(const struct form *form,
				     struct wordbuf *defs,
				     struct context *context)
{
	assert(form->variety == FORM_LET);

	const size_t ndefs = form_length(form->let.defs) + 1;
	symbol_mt *vars = xmalloc(sizeof *vars * ndefs);
	struct term **vals = xmalloc(sizeof *vals * ndefs);
	const struct form *def;
	size_t i;
	for (i = ndefs, def = form->let.defs; i-- > 1; def = def->prev) {
		assert(def->def.var->variety == FORM_VAR);
		vars[i] = def->def.var->var.name;
		vals[i] = form_convert(def->def.val, defs, context);
	}
	assert(!i);
	vars[i] = the_empty_symbol;
	vals[i] = TermPrim(&prim_undefined);
	assert(!def);

	struct context link = {
		.prev = context,
		.nbinders = ndefs,
		.binders = vars,	/* freed when term is freed */
	};

	/* transfer ownership of 'vars' and 'vals' */
	return TermLet(ndefs, vars, vals,
		       form_convert(form->let.body, defs, &link));
}

/*
 * form_convert, as its name suggests, converts forms to terms.  It
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
	case FORM_ABS: case FORM_FIX:
		return form_convert_abs(form, defs, context);
	case FORM_APP: {
		size_t nargs = form_length(form->app.args);
		/*
		 * 0-ary applications also collapse.
		 */
		if (nargs == 0)
			return form_convert(form->app.fun, defs, context);

		struct term **args = xmalloc(sizeof *args * nargs),
			    **dst = args + nargs;
		const struct form *arg;
		for (arg = form->app.args; arg; arg = arg->prev)
			*--dst = form_convert(arg, defs, context);
		assert(dst == args);
		return TermApp(form_convert(form->app.fun, defs, context),
			       nargs, args);
	}
	case FORM_CELL: {
		const size_t nelts = form_length(form->cell.elts);
		struct term **elts = xmalloc(sizeof *elts * nelts),
			    **dst = elts + nelts;
		for (const struct form *elt = form->cell.elts;
		     elt; elt = elt->prev)
			*--dst = form_convert(elt, defs, context);
		assert(dst == elts);
		return TermCell(nelts, elts);
	}
	case FORM_LET:
		return form_convert_let(form, defs, context);
	case FORM_NUM:
		return TermNum(form->num);
	case FORM_OP1: {
		const size_t nargs = 1;
		struct term **args = xmalloc(sizeof *args * nargs),
			    **dst = args + nargs;
		*--dst = form_convert(form->op1.arg, defs, context);
		assert(dst == args);
		return TermApp(TermPrim(form->op1.prim), nargs, args);
	}
	case FORM_OP2: {
		const size_t nargs = 2;
		struct term **args = xmalloc(sizeof *args * nargs),
			    **dst = args + nargs;
		*--dst = form_convert(form->op2.rhs, defs, context);
		*--dst = form_convert(form->op2.lhs, defs, context);
		assert(dst == args);
		return TermApp(TermPrim(form->op2.prim), nargs, args);
	}
	case FORM_PRIM:
		return TermPrim(form->prim);
	case FORM_STRING:
		return TermString(xstrdup(form->str));
	case FORM_TEST: {
		size_t ncsq = form_length(form->test.csq),
		       nalt = form_length(form->test.alt);
		assert(ncsq > 0 && nalt > 0);
		struct term **csq = xmalloc(sizeof *csq * ncsq),
			    **alt = xmalloc(sizeof *alt * nalt),
			    **cdst = csq + ncsq, **adst = alt + nalt;
		const struct form *p;
		for (p = form->test.csq; p; p = p->prev)
			*--cdst = form_convert(p, defs, context);
		for (p = form->test.alt; p; p = p->prev)
			*--adst = form_convert(p, defs, context);
		assert(cdst == csq && adst == alt);
		return TermTest(form_convert(form->test.pred, defs, context),
				ncsq, csq /* transfer ownership */,
				nalt, alt /* transfer ownership */);
	}
	case FORM_VAR: {
		struct binding b = context_lookup(context, form->var.name);
		if (b.up >= 0)
			return TermBoundVar(b.up, b.across, form->var.name);

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
