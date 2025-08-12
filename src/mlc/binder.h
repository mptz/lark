#ifndef LARK_MLC_BINDER_H
#define LARK_MLC_BINDER_H
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

#include <stddef.h>		/* size_t */

#include <util/symtab.h>	/* symbol_mt */

struct node;
struct term;

/*
 * Binders for the global environment.  Handle global constant ordering,
 * namespacing, and parameterization (via binding flags).  Not all flag
 * combinations make sense, but their meanings are mostly orthogonal so
 * we allow independent specification.  Binding flags request non-default
 * behavior--the default is the absence of all flags.
 */
#define BINDING_DEFAULT 0

/*
 * Opaque bindings aren't expanded into nodes.  In contrast to atomic
 * values such as numbers and symbols, they are still (conceptually)
 * references to nodes and thus don't falsify subject reduction, but
 * their reduction is 'stuck' behind their opacity.  There may in fact
 * be no meaningful value behind them i.e. they may be entirely free
 * (axioms/existence assertions/dangling references) and impossible to
 * 'un-stick'.  By default, bindings are 'transparent': a reference to
 * a transparent global constant is replaced by the (node) value of
 * that constant prior to reduction.
 */
#define BINDING_OPAQUE 0x1

/*
 * Literal bindings are not reduced in the course of definition.  This
 * allows the definition of global constants which resolve to arbitrary
 * terms, not just to the reduced or normal forms of those terms.  By
 * default, bindings are 'reduced': as part of definition, their (node)
 * values are reduced before being installed as global constants.  A
 * reduced constant defined as the term '1 + 1' will first be reduced
 * to '2', whereas a literal constant will be installed as a redex
 * containing the primitive addition of two 1s.
 */
#define BINDING_LITERAL 0x2

/*
 * Deep bindings are reduced 'under abstractions' as well as under other
 * deferred subexpressions.  This means that reduction enters into
 * unevaluated bodies of abstractions, tests, and other subexpressions
 * which don't need to be reduced in order to yield a value.  For example,
 * a constant defined as the function '[x. 1 + 1]' will be deep-reduced
 * to '[x. 2]', a function returning a literal 2, even though this
 * function is not applied.  By default, bindings are 'surface', i.e.
 * they respect abstraction: their shallow reduction does not extend into
 * the bodies of unapplied abstractions and other subexpression bodies.
 *
 * We use this deep/surface terminology in contrast to technical terms
 * such as strong/weak, 'normal form' and 'weak head normal form'
 * for correctness reasons (even deep reduction yields normal forms only
 * under specific circumstances) as well as to stress the primacy of the
 * deep/surface reduction concept over the technical characteristics of
 * the reduction's result.
 *
 * This flag parameterizes reduction so has no effect on literal bindings.
 */
#define BINDING_DEEP 0x4

/*
 * Lifting bindings' values are provided to terms which reference them as
 * arguments to be evaluated.  Specifically, when a term references one
 * or more lifting bindings, that term is wrapped in a (syntactically
 * visible) 'let' expression which provides the values of those bindings
 * to the original term, now the body of the let-expression.  This allows
 * lifting bindings to be fully evaluated (to normal form) within terms
 * referencing them even if they were previously unevaluated. 
 *
 * By default, bindings are 'substituted': the value of a global constant
 * is referenced from a term via explicit substitution, and is not
 * re-reduced when the term referencing it is reduced.  Terms which
 * reference global constants via explicit substitution may therefore
 * not be reduced in their entirety to normal form even via traversed
 * reduction.
 *
 * Currently lifting bindings must also be literal, i.e. previously
 * unevaluated.  This is because our reduction mechanism does not
 * support the reevaluation of previously-evaluated nodes.  In fact our
 * implementation does not even convert lifting bindings to nodes at
 * definition time but preserves them as terms until time of reference.
 */
#define BINDING_LIFTING 0x8

struct binder {
	size_t index;
	symbol_mt name, space;
	struct term *term;
	struct node *val;
	unsigned flags;
};

extern int binder_ptr_cmp(const void *a, const void *b); /* for qsort */

#endif /* LARK_MLC_BINDER_H */
