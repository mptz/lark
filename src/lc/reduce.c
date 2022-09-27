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

#include <util/message.h>

#include "alloc.h"
#include "heap.h"
#include "reduce.h"
#include "term.h"

/*
 * A recursive evaluator which is much more readable than the iterative
 * version; its GC-unsafeness and its coupling of C stack and evaluator
 * spine allow for some significant simplifications.  It's included here
 * as a reference.
 */
#ifdef REFERENCE_RECURSIVE_IMPLEMENTATION

struct term *shift(struct term *term, int delta, int cutoff)
{
	switch (term->type) {
	case ABS: return Abs(term->abs.formal,
			     shift(term->abs.body, delta, cutoff + 1));
	case APP: return App(shift(term->app.fun, delta, cutoff),
			     shift(term->app.arg, delta, cutoff));
	case VAR: return term->var.index < cutoff ? term : 
			VarI(term->var.index + delta);
	default: panic("Unhandled case in shift\n");
	}
}

struct term *subst(struct term *term, int var, struct term *value)
{
	switch (term->type) {
	case ABS: return Abs(term->abs.formal,
			     subst(term->abs.body, var + 1, value));
	case APP: return App(subst(term->app.fun, var, value),
			     subst(term->app.arg, var, value));
	case VAR: return term->var.index == var ?
		(var == 0 ? value : shift(value, var, 0)) :
		(term->var.index < var ? term : VarI(term->var.index - 1));
	default: panic("Unhandled case in subst\n");
	}
}

struct term *eval(struct term *term, bool spine);

/*
 * Application: if the term in function position is an abstraction or
 * reduces to an abstraction, perform beta-reduction.  Whether we move to
 * further reduce the resulting expression depends on whether we're on the
 * 'spine' of function arguments to applications.  If so, and if we've just
 * created an abstraction, we don't want to evaluate because we're part of
 * a larger redex, and our aim is to evaluate only the outermost redex.
 *
 * If we lack an abstraction in function position even after reduction,
 * only then do we reduce the argument.
 */
struct term *apply(struct term *fun, struct term *arg, bool spine)
{
	if (fun->type == ABS || (fun = eval(fun, true))->type == ABS) {
		struct term *reduct = subst(fun->abs.body, 0, arg); /* beta */
		return (spine && reduct->type == ABS) ?
			reduct : eval(reduct, spine);
	}
	return App(fun, eval(arg, false));
}

/*
 * Evaluation: choose strategy based on term type.  Since we're evaluating
 * all the way to normal form, not just to WHNF, we penetrate abstractions.
 * Also, note that there's no lookup to be performed for variables, since
 * this reduction engine is based on full copying beta-reductions rather
 * than on environments or explicit substitutions.
 */
struct term *eval(struct term *term, bool spine)
{
	switch (term->type) {
	case ABS: return Abs(term->abs.formal, eval(term->abs.body, false));
	case APP: return apply(term->app.fun, term->app.arg, spine);
	case VAR: return term;
	default: panic("Unhandled case in eval\n");
	}
}

struct term *reduce(struct term *term)
{
	return eval(term, false);
}

#else /* REFERENCE_RECURSIVE_IMPLEMENTATION */

/*
 * Now the "industrial strength" version.
 *
 * To make GC safety easier, we're using a slightly more complex iterative
 * approach with an explicit stack, which can be registered with the garbage
 * collector.
 */

union payload {
	int delta;
	struct term *value;
};

typedef struct term *handler(struct allocator *spine, struct term *term,
			     int depth, union payload *payload);

static struct term *
shift(struct allocator *spine, struct term *term, int delta);

static struct term *
traverse(struct allocator *spine, struct term *term, bool betareduce,
	 handler *handler, union payload *payload);

static struct term *
donoop(struct allocator *spine, struct term *term,
       int depth, union payload *payload)
{
	return term;
}

static struct term *
doshift(struct allocator *spine, struct term *term,
	int depth, union payload *payload)
{
	assert(term->type == VAR);
	return term->var.index < depth ? term :
		VarI(term->var.index + payload->delta);
}

/*
 * By comparing this variable's index to the given variable index (which
 * has been adjusted to reflect the number of abstractions traversed), we
 * can determine whether this variable references the value being
 * substituted, was free in the original abstraction, or was bound within
 * the original abstraction body.
 *
 * If this variable references the value we're substituting, perform the
 * substitution after shifting the value's free variables to reflect the
 * number of abstractions traversed.  The amount by which we shift is
 * identical to the current index of 'var'.  If we traversed no
 * abstractions, don't bother shifting.
 *
 * If this variable was bound in the original abstraction's body, we can
 * leave it unchanged.
 *
 * If this variable was free in the original abstraction, downshift by one
 * to reflect the abstraction elimination.
 */
static struct term *
dosubst(struct allocator *spine, struct term *term,
	int depth, union payload *payload)
{
	assert(term->type == VAR);
	int tvar = term->var.index;
	if (tvar == depth)
		return (depth == 0) ? payload->value :
			shift(spine, payload->value, depth);
	return tvar < depth ? term : VarI(tvar - 1);
}

/*
 * Apply a function to an argument by substituting the argument in the
 * function abstraction, i.e. perform a beta-reduction.
 */
static struct term *
apply(struct allocator *spine, struct term *fun, struct term *arg)
{
	assert(fun->type == ABS);
	union payload payload = { .value = arg };
	return traverse(spine, fun->abs.body, false, &dosubst, &payload);
}

struct term *
reduce(struct term *term)
{
	struct allocator spine = { .name = "Reduction spine" };
	allocator_init(&spine, ALLOCATOR_DEFAULT_SLOTS);
	term = traverse(&spine, term, true, &donoop, NULL);
	assert(allocator_empty(&spine));
	allocator_fini(&spine);
	return term;
}

static struct term *
shift(struct allocator *spine, struct term *term, int delta)
{
	union payload payload = { .delta = delta };
	return traverse(spine, term, false, &doshift, &payload);
}

/*
 * Attempt to make a generic traversal function which will handle all of
 * the interesting cases.
 */
static struct term *
traverse(struct allocator *spine,	/* term's surrounding context */
	 struct term *term,		/* current term */
	 bool betareduce,		/* perform beta reduction? */
	 handler *handler,		/* leaf/variable handler */
	 union payload *payload)	/* value for handler */
{
	/*
	 * A marker placed on the spine to indicate that we're in the right
	 * hand side of an application.  Without this the assumption is
	 * that we have made a leftmost traversal into function (operator)
	 * position.  The VAR type is used for this marker since we can't
	 * actually descend into a real variable (it's a leaf).
	 */
	static struct term rhs_marker = { .type = VAR };

	/*
	 * Mark the current position on the stack--when we encounter this
	 * marker again, we know we're done.  Unlike the preceding marker,
	 * the address of this one should be different for each recursive
	 * traversal invocation, so it's not static.
	 */
	struct term shift_marker = { .type = VAR };
	allocator_push(spine, &shift_marker);

	/*
	 * Top term on the spine stack, i.e. innermost term in the
	 * surrounding context of 'term'.  Used during ascent and beta-
	 * reduction, but not during descent.
	 */
	struct term *top;

	/*
	 * Abstraction depth, i.e. number of abstractions traversed.
	 * Descending into an application doesn't count against this depth
	 * since we only care about changes variable indexes.
	 */
	int depth = 0;

descend:
	/*
	 * Descend into abstractions and into the left (function) branches
	 * of applications.  We don't enter the argument branches of
	 * applications here--instead we bounce down into them when we
	 * re-encounter these applications during ascent.  During descent,
	 * 'term' is the next term to be handled.
	 */
	switch (term->type) {
	case ABS:
		allocator_push(spine, term);
		term = term->abs.body;
		++depth;
		goto descend;
	case APP:
		allocator_push(spine, term);
		if (betareduce && term->app.fun->type == ABS) {
			top = term;
			term = term->app.fun;
			goto apply;
		}
		term = term->app.fun;
		goto descend;
	case VAR:
		term = (*handler)(spine, term, depth, payload);
		goto ascend;
	default:
		panicf("Traversal: Invalid type in descent: %d\n", term->type);
	}
	panic("Traversal: Should never get here during descent\n");

apply:
	/*
	 * Beta reduction expects the function in 'term' and the argument
	 * in the RHS of the top of the spine as well as in 'top'.
	 */
	assert(term->type == ABS);
	assert(top == allocator_top(spine));
	assert(top->type == APP);
	term = apply(spine, term, top->app.arg);
	allocator_pop(spine);
	if (term->type == ABS && !allocator_empty(spine) &&
	    (top = allocator_top(spine))->type == APP)
		goto apply;
	goto descend;

ascend:
	/*
	 * Ascend the spine.  
	 */
	top = allocator_top(spine);
	switch (top->type) {
	case ABS:
		term = (top->abs.body == term) ? top :
			Abs(top->abs.formal, term);
		allocator_pop(spine);
		--depth;
		goto ascend;
	case APP:
		if (betareduce && term->type == ABS)
			/* goto apply; */
			panic("Redex went undetected\n");
		allocator_push(spine, term);
		allocator_push(spine, &rhs_marker);
		term = top->app.arg;
		goto descend;
	case VAR:
		/*
		 * We can't actually be ascending to a real variable since
		 * those are leaves.  This variable must be one of the
		 * markers we put on the spine to track different states.
		 */
		if (top == &shift_marker)
			goto done;

		/*
		 * We're reducing the RHS (argument position) of a function
		 * application.  In this case the term above the marker is
		 * the already-reduced LHS (function) and above that is the
		 * original application term.
		 */
		assert(top == &rhs_marker);
		allocator_pop(spine);
		{
			struct term *arg = term;
			struct term *fun = allocator_pop(spine);
			term = allocator_pop(spine);
			assert(term->type == APP);
			if (term->app.fun != fun || term->app.arg != arg)
				term = App(fun, arg);
		}
		goto ascend;
	default:
		panicf("Traversal: Invalid type in ascent: %d\n", term->type);
	}
	panic("Traversal: Should never get here during ascent\n");

done:
	assert(allocator_top(spine) == &shift_marker);
	allocator_pop(spine);
	return term;
}

#endif /* REFERENCE_RECURSIVE_IMPLEMENTATION */
