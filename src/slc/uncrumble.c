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
#include <stddef.h>

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
	symbol_mt binder;
};

static symbol_mt name_lookup(int index, const struct context *context)
{
	for (assert(index >= 0); index--; assert(context))
		context = context->outer;
	return context->binder;
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
static struct term *uncrumble_rl(const struct node *node, int cutoff,
				 const struct context *context,
				 struct shift *shift);

static struct term *uncrumble_slot(unsigned bits, union slot slot,
				   int depth, int cutoff,
				   const struct context *context,
				   struct shift *shift)
{
	assert((bits & ~NODE_LHS_MASK) == 0);

	if (!bits)
		return NULL;
	if (bits & NODE_LHS_BOUND) {
		shift->cutoff = cutoff;
		int shifted = shift_index(slot.index, shift);
		return TermBoundVar(shifted, name_lookup(shifted, context));
	}
	if (bits & NODE_LHS_FREE)
		return slot.term;

	assert(bits & NODE_LHS_SUBST);
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
	if (node->bits == NODE_BITS_ABS) {
		struct node *target = node_abs_body(node);
		assert(!target->prev || target->nref > 0);
		assert(node->depth + 1 == target->depth);
		struct context scope = {
			.outer = context, 
			.binder = node_abs_formal(node),
		};
		return TermAbs(node_abs_formal(node),
			uncrumble_rl(target, cutoff + 1, &scope, shift));
	}

	struct term *lhs = uncrumble_slot(node->bits & NODE_LHS_MASK,
					  node->lhs, node->depth, cutoff,
					  context, shift),
		    *rhs = uncrumble_slot((node->bits & NODE_RHS_MASK) >>
						NODE_LHS_RHS_SHIFT,
					  node->rhs, node->depth, cutoff,
					  context, shift);
	return rhs ? TermApp(lhs, rhs) : lhs;
}

static struct term *uncrumble_rl(const struct node *node, int cutoff,
				 const struct context *context,
				 struct shift *shift)
{
	while (node->prev) node = node->prev;
	return uncrumble_node(node, cutoff, context, shift);
}

struct term *uncrumble(const struct node *node)
{
	assert(node->depth == 0);
	struct shift shift = { .prev = NULL, .delta = 0, .cutoff = 0 };
	return uncrumble_rl(node, 0, NULL, &shift);
}
