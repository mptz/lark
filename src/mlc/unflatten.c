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
#include <math.h>
#include <stdlib.h>

#include <util/memutil.h>
#include <util/message.h>
#include <util/symtab.h>
#include <util/wordtab.h>

#include "node.h"
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
 * A slight complication is that our copies are nested--at any point
 * we can encounter a node which points upwards--so we build a linked
 * list of cutoffs and 'deltas' for shifting, allowing us to map a
 * bound variable back through all nested copies to the index it
 * should hold in the tree we're constructing.
 */

struct context {
	const struct context *outer;
	symbol_mt *formals;
	size_t nformals;
};

static symbol_mt name_lookup(int up, int across, const struct context *context)
{
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

static int shift_index(int index, const struct shift *shift)
{
	/*
	 * A shift with delta == 0 marks the end of the line; we
	 * start with one as a terminator but only add shifts when
	 * we link across abstraction depths (so delta > 0).
	 */
	for (assert(shift); shift->delta; shift = shift->prev)
		if (index >= shift->cutoff)
			index += shift->delta;
	return index;
}

static struct term *unflatten_node(const struct node *node, int cutoff,
				   const struct context *context,
				   struct shift *shift,
				   struct unshare *unshare);

static struct term *unflatten_body(const struct node *node, int cutoff,
				   const struct context *context,
				   struct shift *shift,
				   struct unshare *unshare)
{
	assert(node->variety == NODE_SENTINEL);
	return unflatten_node(node->next, cutoff, context, shift, unshare);
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

	/* node can only have one body for now */
	struct term **bodies = xmalloc(sizeof *bodies);
	bodies[0] = unflatten_body(body, cutoff + 1, &scope, shift, unshare);
	return node->variety == NODE_FIX ?
		TermFix(node->nslots, formals, 1, bodies) :
		TermAbs(node->nslots, formals, 1, bodies);
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
		return TermBoundVar(shifted, slot.bv.across,
				    name_lookup(shifted, slot.bv.across,
						context));
	}
	case SLOT_FREE: return slot.term;
	case SLOT_NUM: return TermNum(slot.num);
	case SLOT_PRIM: return TermPrim(slot.prim);
	case SLOT_STRING: return TermString(xstrdup(slot.str));
	default: /* handled below... */;
	}

	assert(slot.variety == SLOT_SUBST);

	struct node *target = slot.subst;
	assert(target->nref > 0);
	assert(target->depth <= depth);
	if (target->depth < depth) {
		shift->cutoff = cutoff;
		struct shift nextshift = {
			.prev = shift,
			.delta = depth - target->depth,
			.cutoff = 0,
		};
		assert(nextshift.delta);
		return unflatten_node(target, 0, context, &nextshift, unshare);
	}
	return unflatten_node(target, cutoff, context, shift, unshare);
}

static struct term *unflatten_node(const struct node *node, int cutoff,
				   const struct context *context,
				   struct shift *shift, struct unshare *unshare)
{
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
