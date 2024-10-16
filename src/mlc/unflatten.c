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
#include <stdlib.h>

#include <util/memutil.h>
#include <util/message.h>

#include "node.h"
#include "term.h"
#include "uncrumble.h"

/*
 * Uncrumbling is the opposite of crumbling: reading back a tree from
 * the flattened lists of explicit substitutions.  This undoes sharing,
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
	const struct node *abs;
};

static symbol_mt name_lookup(int up, int across, const struct context *context)
{
	for (assert(up >= 0); up--; assert(context))
		context = context->outer;
	assert(node_is_abs(context->abs));
	/* the +1 skips over the function body */
	assert(slot_is_name(context->abs->slots[across+1]));
	return context->abs->slots[across+1].name;
}

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

static struct term *uncrumble_node(const struct node *node, int cutoff,
				   const struct context *context,
				   struct shift *shift);

static struct term *uncrumble_body(const struct node *node, int cutoff,
				   const struct context *context,
				   struct shift *shift)
{
	assert(node->variety == NODE_SENTINEL);
	return uncrumble_node(node->next, cutoff, context, shift);
}

static struct term *uncrumble_abs(const struct node *node, int cutoff,
				  const struct context *context,
				  struct shift *shift)
{
	assert(node_is_abs(node));
	assert(node->nslots);

	struct node *body = node_abs_body(node);
	assert(body->variety == NODE_SENTINEL);
	assert(body->depth == node->depth + 1);
	size_t nformals = node->nslots - 1;	/* omit body */
	assert(nformals);
	symbol_mt *formals = xmalloc(sizeof *formals * nformals);
	for (size_t i = 0; i < nformals; ++i) {
		assert(slot_is_name(node->slots[i+1]));
		formals[i] = node->slots[i+1].name;
	}
	struct context scope = {
		.outer = context, 
		.abs = node,
	};

	/* node can only have one body for now */
	struct term **bodies = xmalloc(sizeof *bodies);
	bodies[0] = uncrumble_body(body, cutoff + 1, &scope, shift);
	return node->slots[1].variety == SLOT_SELF ?
		TermFix(nformals, formals, 1, bodies) :
		TermAbs(nformals, formals, 1, bodies);
}

static struct term *uncrumble_slot(struct slot slot, int depth, int cutoff,
				   const struct context *context,
				   struct shift *shift)
{
	switch (slot.variety) {
	case SLOT_BODY:
		return uncrumble_body(slot.subst, cutoff, context, shift);
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
		return uncrumble_node(target, 0, context, &nextshift);
	}
	return uncrumble_node(target, cutoff, context, shift);
}

static struct term *uncrumble_node(const struct node *node, int cutoff,
				   const struct context *context,
				   struct shift *shift)
{
	assert(node->nref);
	switch (node->variety) {
	case NODE_ABS:
	case NODE_FIX:
		return uncrumble_abs(node, cutoff, context, shift);
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
		slotvals[i] = uncrumble_slot(node->slots[i], node->depth,
					     cutoff, context, shift);

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
	case NODE_CELL:
		assert(node->nslots == 0 || node->nslots == 2);	/* nil/pair */
		retval = node->nslots == 0 ? TermNil() :
			TermPair(slotvals[0], slotvals[1]);
		break;
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

struct term *uncrumble(const struct node *node)
{
	assert(node->depth == 0);
	struct shift shift = { .prev = NULL, .delta = 0, .cutoff = 0 };
	return uncrumble_body(node, 0, NULL, &shift);
}
