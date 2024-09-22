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
#include <stddef.h>

#include <util/message.h>

#include "beta.h"
#include "node.h"

struct subst {
	struct node *redex;	/* node being beta-reduced */
	int basedepth,		/* starting depth of beta-reduction */
	    shift;		/* amount to shift free variables */
};

static struct node_chain {
	struct node *lend, *rend;
} copy_node_rl(struct node *src, int var, struct subst *subst);

/*
 * By comparing this bound variable's up-value to the current height
 * of our traversal (which is adjusted to reflect the number of
 * abstractions and arguments traversed), we can determine whether
 * this bound variable references a value being substituted, was
 * free in the original abstraction, or was bound within the original
 * abstraction body.
 */
static enum slot_variety copy_bv(struct slot *dst,
				 int up, int across, int height,
				 const struct subst *subst)
{
	if (up == height) {
		/*
		 * Perform metalevel substitution: replace a bound
		 * variable with a substitution and increment the
		 * reference count of that substitution's target
		 * (i.e. the beta-redex value).
		 *
		 * We don't set a backreference even though we're
		 * allocating a substitution, since the referent
		 * of the substitution (the substitution variable
		 * of the redex) is to the right of the R-to-L
		 * traversal site.  Backreferences wouldn't work
		 * anyway as we might make multiple substitutions,
		 * so there's no unique referrer.
		 */
		assert(subst->redex->nslots > across + 1);
		assert(subst->redex->slots[across+1].variety == SLOT_SUBST);
		dst->subst = subst->redex->slots[across+1].subst;
		dst->subst->nref++;
		return SLOT_SUBST;
	}

	/*
	 * Variables originally locally-free get shifted as they get
	 * pulled deeper, while locally-bound variables stay as-is.
	 */
	dst->bv.up = up + (up > height ? subst->shift : 0);
	dst->bv.across = across;
	return SLOT_BOUND;
}

static unsigned copy_subst(struct slot *copy, const struct slot src)
{
	struct node *target = src.subst;
	if (target->forward) {
		/*
		 * Note that backref points to the actual slot in an
		 * allocated node... we use this to snap pointers in
		 * the 'rename' reduction step.  Backref uniqueness
		 * relies on the fact that the target has exactly one
		 * referrer (no sharing yet)--true as we haven't yet
		 * reduced this abstraction body (we only enter
		 * abstraction bodies after they're copied and we've
		 * switched from R-to-L to L-to-R).
		 */
		target = target->forward;
		assert(target->nref == 0);	/* we add 1 below */
		assert(!target->backref);
		target->backref = copy;
	}

	/*
	 * In a deviation from the ML reference code for the SCAM,
	 * I believe I need to increment the reference count when
	 * linking to a forwarded node; otherwise copying renders
	 * nodes with references eligible for garbage collection.
	 *
	 * This might be a bug in the ML implementation, which has
	 * a mock GC implementation.
	 */
	target->nref++;
	copy->subst = target;
	return SLOT_SUBST;
}

static struct node *copy_app(struct node *copy, const struct node *src,
			     int var, struct subst *subst)
{
	assert(copy->nslots == src->nslots);
	for (size_t i = copy->nslots; i--; /* nada */) {
		enum slot_variety variety = src->slots[i].variety;
		switch (variety) {
		case SLOT_BOUND:
			copy->slots[i].variety = copy_bv(&copy->slots[i],
				src->slots[i].bv.up, src->slots[i].bv.across,
				var, subst);
			break;
		case SLOT_FREE:
		case SLOT_NUM:
		case SLOT_PRIM:
			copy->slots[i] = src->slots[i];
			break;
		case SLOT_SUBST:
			copy->slots[i].variety = copy_subst(
				&copy->slots[i], src->slots[i]);
			break;
		default: panicf("Unhandled slot variety %d\n", variety);
		}
	}

	return copy;
}

static struct node *copy_test(struct node *copy, const struct node *src,
			     int var, struct subst *subst)
{
	assert(src->variety == NODE_TEST);
	assert(src->nslots == 5);
	assert(copy->nslots == src->nslots);
	copy_subst(&copy->slots[0], src->slots[0]);	/* predicate */

	/*
	 * XXX do I need to invoke the machinery in copy_subst for these?
	 */
	struct node_chain chain;
	chain = copy_node_rl(src->slots[2].subst, var, subst); /* consequent */
	copy->slots[1].subst = chain.lend;
	copy->slots[2].subst = chain.rend;
	chain = copy_node_rl(src->slots[4].subst, var, subst); /* alternative */
	copy->slots[3].subst = chain.lend;
	copy->slots[4].subst = chain.rend;

	return copy;
}

/*
 * Copy a node.  For abstractions, increment 'var' since we're descending
 * into an abstraction, therefore there's one more layer of abstraction
 * depth to reach var's binder.
 */
static struct node *copy_node(struct node *prev, struct node *src,
			      int var, struct subst *subst)
{
	int depth = subst->basedepth + var;
	switch (src->variety) {
	case NODE_TEST:
		return copy_test(NodeTest(prev, depth), src, var, subst);
	default:
		/* fall through... */
	}

	return node_is_abs(src) ?
		NodeAbsCopy(
			prev, depth,
			copy_node_rl(node_abs_body(src), var + 1, subst).rend,
			src) :
		copy_app(NodeApp(prev, depth, node_app_nargs(src)),
			 src, var, subst);
}

/*
 * Make a copy of each node in the environment beginning with src,
 * setting forwarding pointers as we go so later copies will be able
 * to follow them.  Clear the forwarding pointers then reverse the
 * copies, which were originally linked in reverse order.
 *
 *                  src
 * SOURCE:           v
 *       +-----+  #=====#  +-----+  +-----+
 * ... <-|-prev|<-|-prev|<-|-prev|<-|-prev|
 *       |slot |  |slot |  |     |  |     |
 *       |  |  |  |  |  |  |forw |  |forw |
 *       +--|--+  #==|==#  +--|--+  +--|--+
 *          |        +-----^  |     ^  |
 *          +-----------------|-----+  |
 * COPY:                      v        v
 *                         +-----+  +-----+
 *                   prev->|prev-|->|prev-|-> ...
 *                         |     |  |     |
 *                         |     |  |     |
 *                         +-----+  +-----+
 *
 * Copying an abstraction node recursively copies its body.  That's a
 * completely separate sub-invocation of copy_node_rl which doesn't
 * interfere with this one.
 */
static struct node_chain
copy_node_rl(struct node *src, int var, struct subst *subst)
{
	struct node *copy, *curr, *tmp;

	/* perform copies r-to-l, linking copies l-to-r */
	for (curr = src, copy = NULL; curr; curr = curr->prev) {
		assert(curr->nref == 1 || !curr->prev);
		copy = copy_node(copy, curr, var, subst);
		assert(!curr->forward);
		curr->forward = copy;
	}

	/* clear forwarding pointers in originals */
	for (curr = src; curr; curr = curr->prev) {
		/*
		 * Having completed the recursive copy, we should see that
		 * the copies and sources have matching reference counts.
		 */
		assert(curr->forward);
		assert(curr->nref == curr->forward->nref);
		curr->forward = NULL;
	}

	/* reverse copies to put them in correct order */
	struct node_chain chain = { .lend = copy };
	while (copy)
		tmp = copy->prev, copy->prev = curr, curr = copy, copy = tmp;
	chain.rend = curr;
	return chain;
}

/*
 * Connect the left-hand end of the reduct chain (the 'star' at the top
 * level of the reduced term) to the redex's referent.  Necessary since
 * the redex itself is disappearing--it should be replaced with the
 * reduct.  This includes updating the redex's parent pointer (backref)
 * if it exists.
 */
static void
replace_redex(struct node *redex, struct node *lend)
{
	assert(node_check_root(lend));
	assert(lend->depth == redex->depth);
	if (redex->backref) {
		lend->backref = redex->backref;
		lend->backref->subst = lend;
	}
	lend->nref = redex->nref;
	lend->prev = redex->prev;
	if (redex->prev) {
		assert(redex->nref);
		assert(redex->backref);
		redex->nref--;
	}
	assert(redex->nref == 0);
}

struct node *beta_reduce(struct node *redex, struct node *body,
			 int depth, int delta)
{
	assert(depth >= 0);
	assert(delta >= 0);
	struct subst subst = {
		.redex = redex,
		.basedepth = depth,
		.shift = delta - 1,	/* extra -1 for abstraction elim */
	};
	struct node_chain chain = copy_node_rl(body, 0, &subst);
	replace_redex(redex, chain.lend);
	return chain.rend;
}

/*
 * When substituting in an inert term (without copying), the only
 * slots we change are bound variables.  Each bound variable might be
 * shifted (or left unshifted if below the cutoff defined by 'var'),
 * or might be substituted, in which case the slot changes from BOUND
 * to SUBST.  Pre-existing substitutions and free variables are left
 * unchanged.
 *
 * The copy_bv() function defined above suffices for this... we don't
 * need to create an alternative subst_bv().
 */
static void subst_inert(struct node *src, int var, struct subst *subst)
{
	for (size_t i = src->nslots; i--; /* nada */) {
		struct slot *slot = &src->slots[i];
		if (slot->variety != SLOT_BOUND)
			continue;
		enum slot_variety v =
			copy_bv(slot, slot->bv.up, slot->bv.across, var, subst);
		assert(v == SLOT_BOUND || v == SLOT_SUBST);
		slot->variety = v;
	}
}

static struct node_chain
subst_node_rl(struct node *src, int var, struct subst *subst);

/*
 * The predicate in slots[0] of a test is guaranteed to be a SUBST,
 * so there's nothing to do for that slot here.  We do, however, have
 * to recursively substitute in both consequent and alternative.
 */
static void subst_test(struct node *src, int var, struct subst *subst)
{
	assert(src->nslots == 5);
	assert(src->slots[2].variety == SLOT_SUBST);
	subst_node_rl(src->slots[2].subst, var, subst);
	assert(src->slots[4].variety == SLOT_SUBST);
	subst_node_rl(src->slots[4].subst, var, subst);
}

/*
 * As above, the only interesting case is when the node is a bound
 * variable which we substitute (index == var).  We don't allocate
 * a new node in this situation, just modify the existing one.
 */
static void subst_node(struct node *src, int var, struct subst *subst)
{
	/* update depth on every node we traverse */
	src->depth = subst->basedepth + var;

	/* recursively handle abstraction and application cases */
	if (node_is_abs(src))
		/*
		 * Increment 'var' since we're descending into an
		 * abstraction, therefore there is one more layer of
		 * abstraction depth to reach var's binder.
		 */
		subst_node_rl(node_abs_body(src), var + 1, subst);
	else if (src->variety == NODE_TEST)
		subst_test(src, var, subst);
	else
		subst_inert(src, var, subst);
}

static struct node_chain
subst_node_rl(struct node *src, int var, struct subst *subst)
{
	struct node *curr, *prior;
	for (curr = src, prior = NULL; curr; prior = curr, curr = curr->prev) {
		assert(curr->nref == 1 || !curr->prev);
		subst_node(curr, var, subst);
	}
	return (struct node_chain) { .lend = prior, .rend = src };
}

struct node *beta_nocopy(struct node *redex, struct node *body,
			 int depth, int delta)
{
	assert(depth >= 0);
	assert(delta >= 0);
	struct subst subst = {
		.redex = redex,
		.basedepth = depth,
		.shift = delta - 1,	/* extra -1 for abstraction elim */
	};
	struct node_chain chain = subst_node_rl(body, 0, &subst);
	replace_redex(redex, chain.lend);
	return chain.rend;
}
