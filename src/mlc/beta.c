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
#include "heap.h"	/* XXX awkward */
#include "node.h"

struct subst {
	struct node *redex;	/* node being beta-reduced */
	int basedepth,		/* starting depth of beta-reduction */
	    shift;		/* amount to shift free variables */
};

static struct node_chain
copy_chain(struct node *src, int var, struct subst *subst);

static struct node *
copy_body(struct node *src, int var, struct subst *subst)
{
	int depth = subst->basedepth + var;
	struct node_chain chain = copy_chain(src, var, subst);
	return NodeSentinel(chain.next, chain.prev, depth);
}

/*
 * By comparing this bound variable's up-value to the current height
 * of our traversal (which is adjusted to reflect the number of
 * abstractions and arguments traversed), we can determine whether
 * this bound variable references a value being substituted, was
 * free in the original abstraction, or was bound within the original
 * abstraction body.
 */
static struct slot
copy_bv(int up, int across, int height, const struct subst *subst)
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
		struct node *ref = subst->redex->slots[across + 1].subst;
		ref->nref++;
		return (struct slot) { .variety = SLOT_SUBST, .subst = ref };
	}

	/*
	 * Variables originally locally-free get shifted as they get
	 * pulled deeper, while locally-bound variables stay as-is.
	 */
	return (struct slot) {
		.variety = SLOT_BOUND,
		.bv.up = up + (up > height ? subst->shift : 0),
		.bv.across = across,
	};
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

static struct node *copy_slots(struct node *copy, const struct node *src,
			       int var, struct subst *subst)
{
	assert(copy->nslots == src->nslots);
	for (size_t i = copy->nslots; i--; /* nada */) {
		enum slot_variety variety = src->slots[i].variety;
		switch (variety) {
		case SLOT_BODY:
			copy->slots[i].variety = SLOT_BODY;
			copy->slots[i].subst =
				copy_body(src->slots[i].subst, var, subst);
			break;
		case SLOT_BOUND:
			copy->slots[i] = copy_bv(src->slots[i].bv.up,
						 src->slots[i].bv.across,
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
	case NODE_ABS: case NODE_FIX: {
		struct node_chain chain =
			copy_chain(node_abs_body(src), var + 1, subst);
		assert(chain.next->depth == depth + 1);
		assert(chain.prev->depth == depth + 1);
		return NodeAbsCopy(prev, depth,
			NodeSentinel(chain.next, chain.prev, depth + 1),
			src);
	}
	case NODE_APP:
	case NODE_CELL:
	case NODE_TEST:
	case NODE_VAL:
	case NODE_VAR: {
		struct node *dst = NodeGeneric(prev, depth, src->nslots);
		copy_slots(dst, src, var, subst);
		dst->variety = src->variety;
		return dst;
	}
	default:
		panicf("Unhandled node variety %d\n", src->variety);
	}
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
 * completely separate sub-invocation of copy_chain which doesn't
 * interfere with this one.
 */
static struct node_chain
copy_chain(struct node *src, int var, struct subst *subst)
{
	struct node *copy, *curr, *tmp;

	assert(src);
	assert(src->variety == NODE_SENTINEL);

	/* perform copies r-to-l, linking copies l-to-r */
	for (curr = src->prev, copy = NULL; !done(curr); curr = curr->prev) {
		assert(curr->nref == 1);
		copy = copy_node(copy, curr, var, subst);
		assert(!curr->forward);
		curr->forward = copy;
		copy->next = copy->prev;	/* correct order */
	}
	assert(curr == src);

	/* clear forwarding pointers in originals */
	for (curr = src->prev; !done(curr); curr = curr->prev) {
		/*
		 * Having completed the recursive copy, we should see that
		 * the copies and sources have matching reference counts--
		 * which should all be 1 since we haven't reduced yet.
		 * The exception is the last (leftmost) copy, which isn't
		 * hooked to a sentinel so has 0 references.
		 */
		assert(curr->forward);
		if (curr->forward->nref == 0) {
			assert(curr->prev->variety == NODE_SENTINEL);
		} else {
			assert(curr->forward->nref == 1);
			assert(curr->forward->nref == curr->nref);
		}
		curr->forward = NULL;
	}
	assert(curr == src);

	/* reverse copies to put them in correct order */
	struct node_chain chain = { .next = copy };
	while (copy)
		tmp = copy->prev, copy->prev = curr, curr = copy, copy = tmp;
	chain.prev = curr;
	return chain;
}

/*
 * Replace the redex (which is disappearing) with the chain resulting
 * from substitution.  Update references and list structure as needed.
 */
static void
replace_redex(struct node *redex, struct node *next, struct node *prev)
{
	/* depths */
	assert(next->depth == redex->depth);
	assert(prev->depth == redex->depth);

	/* backref and references */
	assert(redex->backref);
	next->backref = redex->backref;
	next->backref->subst = next;
	assert(redex->nref == 1);
	next->nref = redex->nref;
	redex->nref--;

	/* linked-list structure */
	next->prev = redex->prev;
	prev->next = redex->next;
	next->prev->next = next;
	prev->next->prev = prev;
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
	assert(body->variety == NODE_SENTINEL);
	struct node_chain chain = copy_chain(body, 0, &subst);
	replace_redex(redex, chain.next, chain.prev);
	return chain.prev;
}

static void subst_node(struct node *src, int var, struct subst *subst);

void subst_body(struct node *src, int var, struct subst *subst)
{
	assert(src);
	assert(src->variety == NODE_SENTINEL);
	src->depth = subst->basedepth + var;	/* update sentinel depth */
	for (struct node *curr = src->prev; !done(curr); curr = curr->prev) {
		assert(curr->nref == 1);
		subst_node(curr, var, subst);
		assert(curr->depth == src->depth);
	}
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
 *
 * We also have to descend into bodies... but don't handle abstraction
 * bodies here as those require a 'var' (abstraction-depth) adjustment.
 */
static void subst_inert(struct node *src, int var, struct subst *subst)
{
	for (size_t i = src->nslots; i--; /* nada */) {
		struct slot *slot = &src->slots[i];
		if (slot->variety == SLOT_BODY) {
			assert(!node_is_abs(src));
			subst_body(slot->subst, var, subst);
		} else if (slot->variety == SLOT_BOUND) {
			src->slots[i] = copy_bv(slot->bv.up, slot->bv.across,
						var, subst);
		}
	}
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
		subst_body(node_abs_body(src), var + 1, subst);
	else
		subst_inert(src, var, subst);
}

/*
 * A subtle point... reduce() will free the redex, and we don't want
 * to free the body since it's put into use (spliced into the evaluation
 * chain), but the *sentinel* for the body is no longer in use; free that.
 */
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
	assert(body->variety == NODE_SENTINEL);
	subst_body(body, 0, &subst);
	replace_redex(redex, body->next, body->prev);
	struct node *tmp = body->prev;
	node_heap_free(body);
	return tmp;
}
