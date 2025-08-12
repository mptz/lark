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

#include <util/memutil.h>
#include <util/message.h>
#include <util/wordbuf.h>
#include <util/wordtab.h>

#include "binder.h"
#include "env.h"
#include "form.h"
#include "library.h"
#include "mlc.h"
#include "prim.h"
#include "sourcefile.h"
#include "term.h"

/*
 * Bind (specified) constants in the given term, i.e. convert them to
 * bound variables.  Binds free variables at a uniform abstraction height
 * (immediately outside this term) i.e. to the binders of a let-expression
 * containing this term as its body.  Free variables not included in 'vars'
 * are left as-is.
 */
static struct term *term_bind(struct term *term, int depth,
			      size_t nrefs, symbol_mt *vars)
{
	switch (term->variety) {
	case TERM_ABS:
	case TERM_FIX:
		term->abs.body = term_bind(term->abs.body, depth + 1,
					   nrefs, vars);
		break;
	case TERM_APP:
		term->app.fun =
			term_bind(term->app.fun, depth, nrefs, vars);
		for (size_t i = term->app.nargs; i--; /* nada */)
			term->app.args[i] =
				term_bind(term->app.args[i], depth,
					  nrefs, vars);
		break;
	case TERM_CELL:
		for (size_t i = term->cell.nelts; i--; /* nada */)
			term->cell.elts[i] =
				term_bind(term->cell.elts[i], depth,
					  nrefs, vars);
		break;
	case TERM_CONSTANT:
		for (size_t i = nrefs; i--; /* nada */)
			if (vars[i] == term->constant.binder->name)
				return TermVar(depth, i, vars[i]);
		break;
	case TERM_LET:
		term->let.body = term_bind(term->let.body, depth + 1,
					   nrefs, vars);
		for (size_t i = term->let.ndefs; i--; /* nada */)
			term->let.vals[i] =
				term_bind(term->let.vals[i], depth,
					  nrefs, vars);
		break;
	case TERM_NUM:
	case TERM_PRIM:
	case TERM_STRING:
	case TERM_SYMBOL:
		break;
	case TERM_TEST:
		term->test.pred = term_bind(term->test.pred, depth,
					    nrefs, vars);
		for (size_t i = term->test.ncsqs; i--; /* nada */)
			term->test.csqs[i] =
				term_bind(term->test.csqs[i], depth,
					  nrefs, vars);
		for (size_t i = term->test.nalts; i--; /* nada */)
			term->test.alts[i] =
				term_bind(term->test.alts[i], depth,
					  nrefs, vars);
		break;
	case TERM_VAR:
		assert(term->var.up >= 0);
		assert(term->var.up < depth);
		break;
	default:
		panicf("Unhandled term variety %d\n", term->variety);
	}
	return term;
}

/*
 * Lift certain terms to resolve references to the global environment.
 * We do this by wrapping the given term in a let-expression by which
 * we provide the terms it references.  Global environment constants
 * contained in 'refs' are self-contained; we don't have to worry
 * about inter-references among 'refs'.
 */
static struct term *lift(struct term *term, const struct wordtab *refs)
{
	/*
	 * Sort references by environment index so definition precedes
	 * reference; this is not strictly necessary since each global
	 * constant is closed, but lifting according to definition
	 * order is consistent unlike hash-table order.
	 */
	struct wordbuf wb;
	wordbuf_init(&wb);

	struct wordtab_iter iter;
	wordtab_iter_init(refs, &iter);
	struct wordtab_entry *entry;
	while ((entry = wordtab_iter_next(&iter)))
		wordbuf_push(&wb, (word) entry->data);

	size_t nrefs = wordbuf_used(&wb);
	qsort(wb.data, nrefs, sizeof wb.data[0], binder_ptr_cmp);

	/*
	 * Extract the names and values of global constants into formal
	 * parameter & argument lists.  The unused 0th entry of 'vars'
	 * and 'vals' is the self-reference slot.
	 */
	symbol_mt *vars = xmalloc(sizeof *vars * nrefs + 1);
	struct term **vals = xmalloc(sizeof *vals * nrefs + 1);
	for (size_t i = nrefs; i--; /* nada */) {
		const struct binder *binder = (void*) wordbuf_at(&wb, i);
		assert(binder->term);
		assert(!binder->val);
		assert(binder->flags & BINDING_LIFTING);
		vars[i+1] = binder->name;
		vals[i+1] = binder->term;
	}
	vars[0] = the_empty_symbol;
	vals[0] = TermPrim(&prim_undefined);

	/*
	 * Create an let expression wrapping the given term, with one
	 * definition for each substitution from the global environment.
	 * Note that we don't need to free 'vars' or 'vals' since term
	 * construction transfers ownership to the callee.
	 */
	term = TermLet(nrefs+1, vars, vals, term_bind(term, 0, nrefs+1, vars));
	assert(term);
	wordbuf_fini(&wb);
	return term;
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
				 struct context *context,
				 struct wordtab *refs);

/*
 * Abstractions and fixpoint abstractions have the same structure
 * with the exception of the self-reference, which if present
 * becomes the 0th parameter of the construction abstraction term.
 */
static struct term *form_convert_abs(const struct form *form,
				     struct context *context,
				     struct wordtab *refs)
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

	/* transfer ownership of 'formals' */
	struct term *body = form_convert(form->abs.body, &link, refs);
	return form->abs.self ?
		TermFix(nformals, formals, body) :
		TermAbs(nformals, formals, body);
}

static struct term *form_convert_let(const struct form *form,
				     struct context *context,
				     struct wordtab *refs)
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
		vals[i] = form_convert(def->def.val, context, refs);
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
		       form_convert(form->let.body, &link, refs));
}

/*
 * form_convert, as its name suggests, converts forms to terms.
 */
static struct term *form_convert(const struct form *form,
				 struct context *context,
				 struct wordtab *refs)
{
	switch (form->variety) {
	case FORM_ABS: case FORM_FIX:
		return form_convert_abs(form, context, refs);
	case FORM_APP: {
		size_t nargs = form_length(form->app.args);
		/*
		 * 0-ary applications also collapse.
		 */
		if (nargs == 0)
			return form_convert(form->app.fun, context, refs);

		struct term **args = xmalloc(sizeof *args * nargs),
			    **dst = args + nargs;
		const struct form *arg;
		for (arg = form->app.args; arg; arg = arg->prev)
			*--dst = form_convert(arg, context, refs);
		assert(dst == args);
		return TermApp(form_convert(form->app.fun, context, refs),
			       nargs, args);
	}
	case FORM_CELL: {
		const size_t nelts = form_length(form->cell.elts);
		struct term **elts = xmalloc(sizeof *elts * nelts),
			    **dst = elts + nelts;
		for (const struct form *elt = form->cell.elts;
		     elt; elt = elt->prev)
			*--dst = form_convert(elt, context, refs);
		assert(dst == elts);
		return TermCell(nelts, elts);
	}
	case FORM_LET:
		return form_convert_let(form, context, refs);
	case FORM_NUM:
		return TermNum(form->num);
	case FORM_OP1: {
		const size_t nargs = 1;
		struct term **args = xmalloc(sizeof *args * nargs),
			    **dst = args + nargs;
		*--dst = form_convert(form->op1.arg, context, refs);
		assert(dst == args);
		return TermApp(TermPrim(form->op1.prim), nargs, args);
	}
	case FORM_OP2: {
		const size_t nargs = 2;
		struct term **args = xmalloc(sizeof *args * nargs),
			    **dst = args + nargs;
		*--dst = form_convert(form->op2.rhs, context, refs);
		*--dst = form_convert(form->op2.lhs, context, refs);
		assert(dst == args);
		return TermApp(TermPrim(form->op2.prim), nargs, args);
	}
	case FORM_PRIM:
		/*
		 * Special case $undefined, which can be represented
		 * syntactically (using primitive syntax) but not
		 * semantically (so there is no corresponding term).
		 */
		if (form->prim == &prim_undefined) {
			errf("Syntactic primitive %s has no semantic "
			     "representation\n", form->prim->name);
			wordtab_set(refs, the_undefined_symbol);
			return NULL;
		}
		return TermPrim(form->prim);
	case FORM_STRING:
		return TermString(xstrdup(form->str));
	case FORM_SYMBOL:
		return TermSymbol(form->id);
	case FORM_TEST: {
		size_t ncsq = form_length(form->test.csq),
		       nalt = form_length(form->test.alt);
		assert(ncsq > 0 && nalt > 0);
		struct term **csq = xmalloc(sizeof *csq * ncsq),
			    **alt = xmalloc(sizeof *alt * nalt),
			    **cdst = csq + ncsq, **adst = alt + nalt;
		const struct form *p;
		for (p = form->test.csq; p; p = p->prev)
			*--cdst = form_convert(p, context, refs);
		for (p = form->test.alt; p; p = p->prev)
			*--adst = form_convert(p, context, refs);
		assert(cdst == csq && adst == alt);
		return TermTest(form_convert(form->test.pred, context, refs),
				ncsq, csq /* transfer ownership */,
				nalt, alt /* transfer ownership */);
	}
	case FORM_VAR: {
		struct binding b = context_lookup(context, form->var.name);
		if (b.up >= 0)
			return TermVar(b.up, b.across, form->var.name);

		/*
		 * If it's not a bound local variable, it must be a global
		 * variable.  Find it in the global environment and return
		 * its binder as a constant, or return NULL if it's not
		 * found.  If the binder is flagged LIFTING, track this
		 * reference in 'refs'.
		 */
		assert(the_current_sourcefile);
		struct binder *binder = env_lookup(form->var.name,
				&the_current_sourcefile->namespaces);
		if (!binder)
			wordtab_set(refs, the_undefined_symbol);
		else if (binder->flags & BINDING_LIFTING)
			wordtab_put(refs, binder->name, binder);
		return binder ? TermConstant(binder) : NULL;
	}
	default:
		panicf("Unhandled form variety %d\n", form->variety);
	}
}

struct term *resolve(const struct form *form)
{
	/*
	 * Track global constant references which require lifting.
	 * We insert the undefined symbol during conversion to
	 * indicate failure due to dangling global reference.
	 */
	struct wordtab refs;
	wordtab_init(&refs, 10 /* size hint */);

	struct term *term = form_convert(form, NULL, &refs);
	if (wordtab_test(&refs, the_undefined_symbol)) {
		err("Open form cannot be resolved to a term\n");
		term = NULL;
		goto done;
	}
	if (!wordtab_is_empty(&refs))
		term = lift(term, &refs);
	assert(term);
done:
	wordtab_fini(&refs);
	return term;
}
