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

#include <util/message.h>

#include "beta.h"
#include "node.h"

struct subst {
	struct node *redex,	/* node being beta-reduced */
		    *val;	/* value being substituted */
	int basedepth,		/* starting depth of beta-reduction */
	    shift;		/* amount to shift free variables */
};

static struct node *copy_node_rl(struct node *src,
				 int var, struct subst *subst);

/*
 * By comparing this variable's index to the given variable index
 * (which has been adjusted to reflect the number of abstractions
 * traversed), we can determine whether this variable references
 * the value being substituted, was free in the original abstraction,
 * or was bound within the original abstraction body.
 */
static unsigned copy_bv(union slot *dst, int index,
			int var, const struct subst *subst)
{
	if (index == var) {
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
		dst->subst = subst->val;
		subst->val->nref++;
		return NODE_LHS_SUBST;
	}

	/*
	 * Variables originally locally-free get shifted as they get
	 * pulled deeper, while locally-bound variables stay as-is.
	 */
	dst->index = index + (index > var ? subst->shift : 0);
	return NODE_LHS_BOUND;
}

static unsigned copy_subst(union slot *copy, const union slot src)
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
	return NODE_LHS_SUBST;
}

static struct node *copy_app(struct node *copy, const struct node *src,
			     int var, struct subst *subst)
{
	unsigned copybits, srcbits = src->bits;

	/* copy the LHS */
	copybits = 0;
	if (srcbits & NODE_LHS_BOUND)
		copybits |= copy_bv(&copy->lhs, src->lhs.index, var, subst);
	else if (srcbits & NODE_LHS_SUBST)
		copybits |= copy_subst(&copy->lhs, src->lhs);
	else
		copy->lhs = src->lhs, copybits |= NODE_LHS_FREE;
	copy->bits = copybits;

	/* copy the RHS */
	copybits = 0;
	if (srcbits & NODE_RHS_BOUND)
		copybits |= copy_bv(&copy->rhs, src->rhs.index, var, subst);
	else if (srcbits & NODE_RHS_SUBST)
		copybits |= copy_subst(&copy->rhs, src->rhs);
	else
		copy->rhs = src->rhs, copybits |= NODE_LHS_FREE;
			/* n.b. LHS correct above -----^, shifted below */
	copy->bits |= copybits << NODE_LHS_RHS_SHIFT;

	return copy;
}

static struct node *copy_node(struct node *prev, struct node *src,
			      int var, struct subst *subst)
{
	int depth = subst->basedepth + var;

	/* handle abstraction and application cases */
	if (src->bits == NODE_BITS_ABS) {
		/*
		 * Increment 'var' since we're descending into an
		 * abstraction, therefore there is one more layer of
		 * abstraction depth to reach var's binder.
		 */
		return NodeAbs(prev, depth, node_abs_formal(src),
			       copy_node_rl(node_abs_body(src), var+1, subst));
	}
	if (src->bits & NODE_MASK_APP)
		return copy_app(NodeApp(prev, depth), src, var, subst);

	/* the node is a variable */
	if (src->bits & NODE_LHS_BOUND) {
		union slot slot;
		unsigned bits = copy_bv(&slot, src->lhs.index, var, subst);
		return (bits == NODE_LHS_BOUND) ?
			NodeBoundVar(prev, depth, slot.index) :
			NodeSubst(prev, depth, slot.subst);
	}
	if (src->bits & NODE_LHS_FREE)
		return NodeFreeVar(prev, depth, src->lhs.term);
	assert(src->bits & NODE_LHS_SUBST);

	/*
	 * We need to allocate the copy first w/NULL referent because
	 * we need to pass its lhs to copy_subst for backreferencing
	 * if necessary.
	 */
	struct node *copy = NodeSubst(prev, depth, NULL);
	copy_subst(&copy->lhs, src->lhs);
	return copy;
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
static struct node *copy_node_rl(struct node *src, int var, struct subst *subst)
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

	/*
	 * In the special case in which we're copying the top level of a
	 * beta-redex, we want to overwrite the redex with the last copy
	 * in the chain.  We can detect this scenario when var == 0 (not
	 * copying within an abstraction); we have the redex stored in
	 * 'subst'.  Discard the prior last copy after copying it.
	 */
	if (var == 0) {
		assert(subst->redex->bits & NODE_MASK_APP);
		node_move_contents(subst->redex, copy);
		curr = subst->redex;
		tmp = copy, copy = copy->prev;
		node_free_shallow(tmp);
	} else
		curr = NULL;

	/* reverse copies to put them in correct order */
	while (copy)
		tmp = copy->prev, copy->prev = curr, curr = copy, copy = tmp;
	return curr;
}

struct node *beta_reduce(struct node *redex, struct node *body,
			 struct node *val, int depth, int delta)
{
	assert(depth >= 0);
	assert(delta >= 0);
	struct subst subst = {
		.redex = redex,
		.val = val,
		.basedepth = depth,
		.shift = delta - 1,	/* extra -1 for abstraction elim */
	};
	return copy_node_rl(body, 0, &subst);
}

/*
 * When substituting in an application (without copying), the only
 * changes we make are when the LHS or RHS is a bound variable.  The
 * bound variable might be shifted (or left unshifted if it's below
 * the cutoff defined by 'var'), or it might be substituted, in which
 * case it changes from {L,R}HS_BOUND to {L,R}HS_SUBST.  Existing
 * substitutions and free variables are left unchanged.
 *
 * The copy_bv() function defined above suffices for this... we don't
 * need to create an alternative subst_bv().
 */
void subst_app(struct node *src, int var, struct subst *subst)
{
	unsigned newbits;

	/* update the LHS */
	if (src->bits & NODE_LHS_BOUND) {
		newbits = copy_bv(&src->lhs, src->lhs.index, var, subst);
		assert(newbits == NODE_LHS_BOUND || newbits == NODE_LHS_SUBST);
		if (newbits != NODE_LHS_BOUND)
			src->bits = (src->bits & ~NODE_LHS_BOUND) | newbits;
	}

	/* update the RHS */
	if (src->bits & NODE_RHS_BOUND) {
		newbits = copy_bv(&src->rhs, src->rhs.index, var, subst);
		assert(newbits == NODE_LHS_BOUND || newbits == NODE_LHS_SUBST);
		if (newbits != NODE_LHS_BOUND)
			src->bits = (src->bits & ~NODE_RHS_BOUND) |
				    (newbits << NODE_LHS_RHS_SHIFT);
	}
}

static struct node *subst_node_rl(struct node *src, int var,
				  struct subst *subst);

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
	if (src->bits == NODE_BITS_ABS)
		/*
		 * Increment 'var' since we're descending into an
		 * abstraction, therefore there is one more layer of
		 * abstraction depth to reach var's binder.
		 */
		subst_node_rl(node_abs_body(src), var + 1, subst);
	else if (src->bits & NODE_MASK_APP)
		subst_app(src, var, subst);
	else if (src->bits & NODE_LHS_BOUND) {
		unsigned bit = copy_bv(&src->lhs, src->lhs.index, var, subst);
		assert(bit == NODE_LHS_BOUND || bit == NODE_LHS_SUBST);
		if (bit != NODE_LHS_BOUND)
			src->bits = (src->bits & ~NODE_LHS_BOUND) | bit;
	}
}

static struct node *subst_node_rl(struct node *src, int var,
				  struct subst *subst)
{
	/* n.b. we don't substitute the last node in the list yet... */
	struct node *curr, *prior;
	for (curr = src, prior = NULL; curr->prev;
	     prior = curr, curr = curr->prev) {
		assert(curr->nref == 1 || !curr->prev);
		subst_node(curr, var, subst);
	}
	/* ...now we do, leaving prior intact */
	assert(curr);
	subst_node(curr, var, subst);
	assert(!prior || prior->prev == curr);

	/*
	 * In the special case in which we're copying the top level of a
	 * beta-redex, we want to overwrite the redex with the last copy
	 * in the chain.  We can detect this scenario when var == 0 (not
	 * copying within an abstraction); we have the redex stored in
	 * 'subst'.  In this scenario 'prior' points to the node linking
	 * to 'curr'; we need to update its 'prev' pointer to the new
	 * destination.
	 */
	if (var == 0) {
		assert(subst->redex->bits & NODE_MASK_APP);
		assert(curr->depth == subst->redex->depth);
		node_move_contents(subst->redex, curr);
		if (prior) prior->prev = subst->redex;
		node_free_shallow(curr);
		/*
		 * The only case in which subst_node_rl() returns a node
		 * other than 'src' is when we were passed a single-node
		 * environment at the outermost level (var == 0), in
		 * which case we have just freed 'src' and need to return
		 * its replaclement, 'redex', instead.
		 */
		if (src == curr) return subst->redex;
	}

	return src;
}

struct node *beta_nocopy(struct node *redex, struct node *body,
			 struct node *val, int depth, int delta)
{
	assert(depth >= 0);
	assert(delta >= 0);
	struct subst subst = {
		.redex = redex,
		.val = val,
		.basedepth = depth,
		.shift = delta - 1,	/* extra -1 for abstraction elim */
	};
	return subst_node_rl(body, 0, &subst);
}
