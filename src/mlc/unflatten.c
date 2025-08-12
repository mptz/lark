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
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <util/memutil.h>
#include <util/message.h>
#include <util/symtab.h>
#include <util/wordtab.h>

#include "env.h"
#include "mlc.h"
#include "node.h"
#include "prim.h"
#include "term.h"
#include "unflatten.h"

/*
 * Unflattening is the opposite of flattening: reading back a tree from
 * the linear lists of explicit substitutions.  This undoes sharing,
 * which can drastically expand some terms (exponentially, in the worst
 * case) but yields tractable expansion for most terms in practical use.
 *
 * For the most part, we can walk the tree of nested abstractions &
 * applications depth-first, following node pointers at every option,
 * and be fine.
 *
 * The main trick is correcting De Bruijn indexes of bound variables.
 * Sometimes a 'subst' slot points to a node at a lower abstraction
 * depth (i.e. in an outer scope relative to the current term),
 * requiring us to shift bound variable indexes in order to "pull"
 * the tree we're substituting in down to a greater abstraction depth.
 *
 * As is usual with such shifting, as we descend to greater
 * abstraction depths in the tree we're substituting (copying in),
 * we need to track the boundary between variables which were bound
 * in the tree being copied--as we have also copied their binders,
 * we don't need to adjust their indexes--and variables which were
 * free in the tree being copied, which must be shifted.  The 'cutoff'
 * variable, which increases as we enter abstractions, performs this
 * role.
 *
 * A complication is that our copies are nested--at any point we can
 * encounter a node which points upwards--so we build a linked list
 * of cutoffs and deltas for shifting, allowing us to map a bound
 * variable back through all nested copies to the index it should
 * hold in the tree we're constructing.
 */

struct context {
	const struct context *outer;
	symbol_mt *formals;
	size_t nformals;
};

static void print_context(const struct context *context)
{
	putchar('{');
	for (/* nada */; context; context = context->outer) {
		putchar('<');
		for (size_t i = 1; i < context->nformals; ++i) {
			if (i > 1) putchar(',');
			fputs(symtab_lookup(context->formals[i]), stdout);
		}
		putchar('>');
	}
	putchar('}');
}

static symbol_mt name_lookup(int up, int across, const struct context *context)
{
	if (trace_unflatten) {
		fputs("name_lookup: ", stdout);
		print_context(context);
		printf(" up: %d across: %d\n", up, across);
	}
	for (assert(up >= 0); up--; assert(context))
		context = context->outer;
	assert(across < context->nformals);
	return context->formals[across];
}

/*
 * Even though well-behaved terms have tractable readbacks, stuck terms
 * (possibly due to minor bugs like misnamed variables) can lead to huge
 * pile-ups of nested, shared nodes; a node which can be printed in a
 * screen or two can turn into gigabytes or more of term output.  Since
 * the real calculation product is the node while the term & form are
 * for human-readable output, we clamp the expansion (by pruning terms)
 * when the term count would grow too much as a function of node count.
 */
#define UNSHARING_K 1000

struct unshare {
	struct wordtab nodes;
	size_t nnodes, nterms;
};

struct shift {
	struct shift *prev;
	int delta, cutoff;
};

static void print_shift(const struct shift *shift)
{
	putchar('(');
	for (bool first = true; shift->delta; shift = shift->prev) {
		if (first) first = false; else putchar(',');
		printf("/%d+%d", shift->cutoff, shift->delta);
	}
	putchar(')');
}

/*
 * Expanding on the above comment re the 'complication' of nested copies,
 * this is the key point at which we fix up bound variable indexes.
 *
 * When traversing an explicit substitution, in general our interpretation
 * of the term being substituted is invariant across the substitution,
 * i.e. an abstraction is still an abstraction, a primitive number is
 * still a primitive number, etc.  The exception is bound variable indexes
 * (specifically their 'up' portions), which might refer to abstractions
 * which are outside the node being substituted, and which are relative
 * to the abstraction depth at which the substituted node appears.  These
 * may need to be adjusted to still make sense at the substitution point.
 * For Lambda Calculus interpreters which work via term copying, this is
 * the well known "shifting" operation on De Bruijn indexes.
 *
 * When the abstraction depth of the substitution point and the
 * substituted node are identical, we have no issue.  The issue arises
 * when we're substituting a node from a lower abtraction depth (closer
 * to the root of the node tree than the substitution point).  In this
 * case, locally free variables from the node being substituted may need
 * to be increased to reflect the greater number of abstractions they'll
 * need to traverse to reach their binders.
 *
 * Substituting a node from a *higher* abstraction depth is not a thing.
 * This would mean referencing a node out of the context of its
 * abstraction binders, which is not a sensical operation.  So the shift
 * ('delta' below) is always positive.
 *
 * Since a node being substituted may have explicit substitutions of its
 * own, we conduct this process recursively.  If at any point a variable
 * is locally bound (below the abstraction depth cutoff point requiring
 * a shift) we're done--that variable's De Bruijn index and its binder
 * are both present in the subtree we're processing and are thus both
 * present in any larger tree containing that subtree.  But a chain of
 * substitutions, each of which pulls a locally free variable to a
 * greater abstraction depth, may need to repeatedly adjust the variable.
 *
 * An example which requires multiple shifts due to nested substitutions:
 *
 *	[a. [x. a] [y. [z. y]] [s. [t. [u. [v. s]]]]]
 *
 * The up-index of the bound variable 'a' in '[x. a]' is originally 1,
 * since it is bound in the 2nd surrounding abstraction (up-indexes are
 * 0-based).  When '[x. a]' is substituted for 'y' in '[y. [z. y]]',
 * the bound variable a is pushed 1 layer deeper (it needs to traverse
 * z's binder) so its index becomes 2.  Note that y's binder is no longer
 * relevant since it is eliminated via beta-reduction.
 *
 * Similarly, when '[z. [x. a]]' is substituted for 's' in the remaining
 * abstraction, 'a' is pushed 3 layers deeper (for t, u, and v) so its
 * up-index becomes 5.
 *
 * We don't actually perform any of these shifting operations during
 * reduction--substitution is an O(1) operation which doesn't require
 * deep term traversals.  All shifting is deferred to this unflattening
 * (term readback) stage, and we perform it on encountering each bound
 * variable by adjusting its index.
 *
 * A shift with delta == 0 marks the end of a substitution chain; we
 * start unflattening with one as a terminator, but only add shifts when
 * we link across abstraction depths (so elsewhere delta > 0).
 */
static int shift_index(int index, const struct shift *shift)
{
	while (shift->delta && index >= shift->cutoff)
		index += shift->delta, shift = shift->prev;
	return index;
}

static struct term *unflatten_node(const struct node *node, int cutoff,
				   const struct context *context,
				   struct shift *shift,
				   struct unshare *unshare);

static struct term *unflatten_slot(struct slot slot, int depth, int cutoff,
				  const struct context *context,
				  struct shift *shift, struct unshare *unshare);

static struct term *unflatten_subst(const struct node *subst,
				    int depth, int cutoff,
				    const struct context *context,
				    struct shift *shift,
				    struct unshare *unshare);

static struct term *unflatten_body(const struct node *node, int cutoff,
				   const struct context *context,
				   struct shift *shift,
				   struct unshare *unshare)
{
	assert(node->variety == NODE_SENTINEL);
	assert(node->nslots == 1);
	assert(node->slots[0].variety == SLOT_SUBST);
	return unflatten_subst(node->slots[0].subst, node->depth, cutoff,
			       context, shift, unshare);
}

static struct term *unflatten_abs(const struct node *node, int cutoff,
				  const struct context *context,
				  struct shift *shift, struct unshare *unshare)
{
	assert(node_is_abs(node));
	assert(node->nslots);

	struct node *body = node_abs_body(node);
	assert(body->variety == NODE_SENTINEL);
	assert(body->depth == node->depth + 1);
	assert(node->nslots);
	symbol_mt *formals = xmalloc(sizeof *formals * node->nslots);
	formals[0] = node->variety == NODE_FIX ?
		symtab_fresh(symtab_intern("self")) :
		the_empty_symbol;
	for (size_t i = 1; i < node->nslots; ++i) {
		assert(node->slots[i].variety == SLOT_PARAM);
		formals[i] = node->slots[i].name;
	}
	struct context scope = {
		.outer = context, 
		.formals = formals,
		.nformals = node->nslots,
	};

	struct term *ub =
		unflatten_body(body, cutoff + 1, &scope, shift, unshare);
	return node->variety == NODE_FIX ?
		TermFix(node->nslots, formals, ub) :
		TermAbs(node->nslots, formals, ub);
}

static struct term *unflatten_let(const struct node *node, int cutoff,
				  const struct context *context,
				  struct shift *shift, struct unshare *unshare)
{
	assert(node->variety == NODE_LET);

	assert(node->nslots);
	assert(node->slots[0].variety == SLOT_BODY);
	struct node *body = node->slots[0].subst;
	assert(body->variety == NODE_SENTINEL);
	assert(body->depth == node->depth + 1);

	symbol_mt *vars = xmalloc(sizeof *vars * node->nslots);
	struct term **vals = xmalloc(sizeof *vals * node->nslots);
	vars[0] = the_empty_symbol;
	vals[0] = TermPrim(&prim_undefined);
	for (size_t i = 1; i < node->nslots; ++i) {
		vars[i] = symtab_gensym();
		vals[i] = unflatten_slot(node->slots[i], node->depth,
					 cutoff, context, shift, unshare);
	}

	struct context scope = {
		.outer = context, 
		.formals = vars,
		.nformals = node->nslots,
	};
	return TermLet(node->nslots, vars, vals, unflatten_body(
		body, cutoff + 1, &scope, shift, unshare));
}

static struct term *unflatten_slot(struct slot slot, int depth, int cutoff,
				   const struct context *context,
				   struct shift *shift, struct unshare *unshare)
{
	switch (slot.variety) {
	case SLOT_BODY:
		return unflatten_body(slot.subst, cutoff, context,
				      shift, unshare);
	case SLOT_BOUND: {
		shift->cutoff = cutoff;
		int shifted = shift_index(slot.bv.up, shift);
		if (trace_unflatten) {
			printf("SLOT_BOUND bv.up:%d cutoff:%d "
				"shifted:%d shift:",
				slot.bv.up, cutoff, shifted);
			print_shift(shift);
			putchar('\n');
		}
		return TermVar(shifted, slot.bv.across,
			       name_lookup(shifted, slot.bv.across, context));
	}
	case SLOT_CONSTANT: return TermConstant(env_at(slot.index));
	case SLOT_NUM: return TermNum(slot.num);
	case SLOT_PRIM: return TermPrim(slot.prim);
	case SLOT_STRING: return TermString(xstrdup(slot.str));
	case SLOT_SYMBOL: return TermSymbol(slot.sym);
	default: /* handled below... */;
	}

	assert(slot.variety == SLOT_SUBST);
	return unflatten_subst(slot.subst, depth, cutoff,
			       context, shift, unshare);
}

static struct term *unflatten_subst(const struct node *subst,
				    int depth, int cutoff,
				    const struct context *context,
				    struct shift *shift,
				    struct unshare *unshare)
{
	assert(subst->nref > 0);
	assert(subst->depth <= depth);
	if (subst->depth < depth) {
		shift->cutoff = cutoff;
		struct shift nextshift = {
			.prev = shift,
			.delta = depth - subst->depth,
			.cutoff = 0,
		};
		assert(nextshift.delta);
		return unflatten_node(subst, 0, context, &nextshift, unshare);
	}
	return unflatten_node(subst, cutoff, context, shift, unshare);
}

static struct term *unflatten_node(const struct node *node, int cutoff,
				   const struct context *context,
				   struct shift *shift, struct unshare *unshare)
{
	if (trace_unflatten) {
		fputs("unflatten_node: ", stdout);
		print_context(context);
		print_shift(shift);
		printf("/%d ", cutoff);
		node_print(node);
		putchar('\n');
	}

	/*
	 * Do we need to truncate due to superlinear expansion?  We
	 * actually allow an expansion factor of O(N log N), while
	 * pathological cases due to stuck terms exhibit exponential
	 * growth.
	 */
	if (unshare->nnodes * log (unshare->nnodes + M_E) * UNSHARING_K <
	    unshare->nterms)
		return TermPruned();
	if (!wordtab_test(&unshare->nodes, (word) node)) {
		wordtab_set(&unshare->nodes, (word) node);
		unshare->nnodes++;
	}
	unshare->nterms++;

	assert(node->nref);
	switch (node->variety) {
	case NODE_ABS:
	case NODE_FIX:
		return unflatten_abs(node, cutoff, context, shift, unshare);
	case NODE_LET:
		return unflatten_let(node, cutoff, context, shift, unshare);
	case NODE_APP:
	case NODE_CELL:
	case NODE_TEST:
	case NODE_VAL:
	case NODE_VAR:
		break;
	default:
		panicf("Unhandled node variety %d\n", node->variety);
	}

	/*
	 * Handle nodes whose slots can be handled uniformly.  Convert
	 * those slots to subterms, then assemble them into terms below.
	 */
	struct term **slotvals = xmalloc(sizeof *slotvals * node->nslots);
	for (size_t i = 0; i < node->nslots; ++i)
		slotvals[i] = unflatten_slot(node->slots[i], node->depth,
					     cutoff, context, shift, unshare);

	struct term *retval = NULL;
	switch (node->variety) {
	case NODE_APP: {
		assert(node->nslots > 1);
		size_t nargs = node->nslots - 1;
		struct term **args = xmalloc(sizeof *args * nargs);
		for (size_t i = 0; i < nargs; ++i)
			args[i] = slotvals[i+1];
		retval = TermApp(slotvals[0], nargs, args);
		break;
	}
	case NODE_CELL: {
		size_t nelts = node->nslots;
		struct term **elts = xmalloc(sizeof *elts * nelts);
		for (size_t i = 0; i < nelts; ++i)
			elts[i] = slotvals[i];
		retval = TermCell(nelts, elts);
		break;
	}
	case NODE_TEST: {
		struct term **csqs = xmalloc(sizeof *csqs),
			    **alts = xmalloc(sizeof *alts);
		csqs[0] = slotvals[1];
		alts[0] = slotvals[2];
		retval = TermTest(slotvals[0], 1, csqs, 1, alts);
		break;
	}
	case NODE_VAL:
	case NODE_VAR:
		assert(node->nslots == 1);
		retval = slotvals[0];
		break;
	default:
		panicf("Unhandled node variety %d\n", node->variety);
	}
	free(slotvals);
	return retval;
}

struct term *unflatten(const struct node *node)
{
	assert(node->depth == 0);
	struct shift shift = { .prev = NULL, .delta = 0, .cutoff = 0 };
	struct unshare unshare = { .nnodes = 0, .nterms = 0 };
	wordtab_init(&unshare.nodes, 100 /* size hint */);
	struct term *retval = unflatten_body(node, 0, NULL, &shift, &unshare);
	wordtab_fini(&unshare.nodes);
	return retval;
}
