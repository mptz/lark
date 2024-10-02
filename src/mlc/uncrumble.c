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
	assert(context->abs->slots[across+1].variety == SLOT_PARAM ||
	       context->abs->slots[across+1].variety == SLOT_SELF);
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
static struct term *uncrumble_rl(const struct node *node, int cutoff,
				 const struct context *context,
				 struct shift *shift);

static struct term *uncrumble_slot(struct slot slot, int depth, int cutoff,
				   const struct context *context,
				   struct shift *shift)
{
	switch (slot.variety) {
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

static struct term *uncrumble_value(const struct node *node, int cutoff,
				    const struct context *context,
				    struct shift *shift)
{
	assert(node->nslots);
	switch (node->slots[0].variety) {
	case SLOT_BODY: {
		struct node *body = node_abs_body(node);
		assert(body->variety == NODE_SENTINEL);
		assert(body->depth == node->depth + 1);
		size_t nformals = node->nslots - 1;	/* omit body */
		assert(nformals);
		symbol_mt *formals = xmalloc(sizeof *formals * nformals);
		for (size_t i = 0; i < nformals; ++i) {
			assert(node->slots[i+1].variety == SLOT_PARAM ||
			       node->slots[i+1].variety == SLOT_SELF);
			formals[i] = node->slots[i+1].name;
		}
		struct context scope = {
			.outer = context, 
			.abs = node,
		};
		/* node can only have one body for now */
		struct term **bodies = xmalloc(sizeof *bodies);
		bodies[0] = uncrumble_rl(body->prev, cutoff + 1, &scope, shift);
		return node->slots[1].variety == SLOT_SELF ?
			TermFix(nformals, formals, 1, bodies) :
			TermAbs(nformals, formals, 1, bodies);
	}
	case SLOT_NUM:
		assert(node->nslots == 1);
		return TermNum(node->slots[0].num);
	case SLOT_PRIM:
		assert(node->nslots == 1);
		return TermPrim(node->slots[0].prim);
	default:
		panicf("Unhandled slot variety %d\n", node->slots[0].variety);
	}
}

static struct term *uncrumble_node(const struct node *node, int cutoff,
				   const struct context *context,
				   struct shift *shift)
{
	assert(node->nref);
	switch (node->variety) {
	case NODE_ABS:
	case NODE_FIX:
	case NODE_VAL:
		return uncrumble_value(node, cutoff, context, shift);
	case NODE_CELL:
		assert(node->nslots == 0 || node->nslots == 2);	/* nil/pair */
		return	node->nslots == 0 ? TermNil() :
			TermPair(uncrumble_slot(node->slots[0], node->depth,
						cutoff, context, shift),
				 uncrumble_slot(node->slots[1], node->depth,
						cutoff, context, shift));
	case NODE_TEST: {
		struct term **csqs = xmalloc(sizeof *csqs),
			    **alts = xmalloc(sizeof *alts);
		csqs[0] = uncrumble_rl(node->slots[1].subst->prev, cutoff,
				       context, shift);
		alts[0] = uncrumble_rl(node->slots[2].subst->prev, cutoff,
				       context, shift);
		return TermTest(uncrumble_slot(node->slots[0], node->depth,
					       cutoff, context, shift),
				1, csqs, 1, alts);
	}
	case NODE_APP:
	case NODE_VAR:
		break;
	default:
		panicf("Unhandled node variety %d\n", node->variety);
	}

	struct term *lhs, **args;
	lhs = uncrumble_slot(node->slots[0], node->depth, cutoff,
			     context, shift);
	if (node->nslots == 1)
		return lhs;

	/* an application */
	assert(node->variety == NODE_APP);
	size_t nargs = node->nslots - 1;
	args = xmalloc(sizeof *args * nargs);
	for (size_t i = 0; i < nargs; ++i)
		args[i] = uncrumble_slot(node->slots[i+1], node->depth,
					 cutoff, context, shift);
	return TermApp(lhs, nargs, args);
}

static struct term *uncrumble_rl(const struct node *node, int cutoff,
				 const struct context *context,
				 struct shift *shift)
{
	while (node->prev && node->prev->variety != NODE_SENTINEL) node = node->prev;
	return uncrumble_node(node, cutoff, context, shift);
}

struct term *uncrumble(const struct node *node)
{
	assert(node->depth == 0);
	struct shift shift = { .prev = NULL, .delta = 0, .cutoff = 0 };
	return uncrumble_rl(node, 0, NULL, &shift);
}
