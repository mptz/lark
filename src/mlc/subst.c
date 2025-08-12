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
#include "subst.h"

static struct node *copy_node(struct node *prev, const struct node *src,
			      int var, struct subst *subst);

/*
 * Make a copy of each node in the environment referenced by src,
 * setting forwarding pointers as we go so later copies will be able
 * to follow them.  Clear the forwarding pointers then reverse the
 * copies' previous pointers, which were originally linked in reverse
 * order.
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
 * Copying an abstraction node recursively copies its body.  That's
 * a completely separate sub-invocation of copy_body which doesn't
 * interfere with this one.
 */
static struct node_chain
copy_body(const struct node *src, int var, struct subst *subst)
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
		assert(subst->vals->nslots > across);
		assert(subst->vals->slots[across].variety == SLOT_SUBST);
		struct node *ref = subst->vals->slots[across].subst;
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

/*
 * Copy a node's slots.  When copying bodies (subexpressions), increment
 * 'var' for binding terms since we're descending into an abstraction,
 * therefore there's another layer of abstractions to reach var's binder.
 */
static struct node *copy_slots(struct node *copy, const struct node *src,
			       int var, struct subst *subst)
{
	assert(copy->nslots == src->nslots);
	for (size_t i = copy->nslots; i--; /* nada */) {
		enum slot_variety variety = src->slots[i].variety;
		switch (variety) {
		case SLOT_BODY: {
			int bvar = var + !!node_is_binder(src);
			struct node_chain chain =
				copy_body(src->slots[i].subst, bvar, subst);
			copy->slots[i].variety = SLOT_BODY;
			copy->slots[i].subst = NodeSentinel(
				chain.next, chain.prev,
				subst->basedepth + bvar /* depth */);
			break;
		}
		case SLOT_BOUND:
			copy->slots[i] = copy_bv(src->slots[i].bv.up,
						 src->slots[i].bv.across,
						 var, subst);
			break;
		case SLOT_CONSTANT:
		case SLOT_NUM:
		case SLOT_PARAM:
		case SLOT_PRIM:
		case SLOT_SYMBOL:
			copy->slots[i] = src->slots[i];
			break;
		case SLOT_STRING:
			copy->slots[i].variety = SLOT_STRING;
			copy->slots[i].str = xstrdup(src->slots[i].str);
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

static struct node *copy_node(struct node *prev, const struct node *src,
			      int var, struct subst *subst)
{
	int depth = subst->basedepth + var;
	struct node *dst = NodeGeneric(prev, depth, src->nslots);
	copy_slots(dst, src, var, subst);
	dst->variety = src->variety;
	return dst;
}

void subst_copy(const struct node *body, struct subst *subst)
{
	/* 0 is current substitution bound variable depth */
	struct node_chain chain = copy_body(body, 0, subst);
	subst->next = chain.next, subst->prev = chain.prev;
}

static void edit_node(struct node *src, int var, struct subst *subst);

static void edit_body(struct node *src, int var, struct subst *subst)
{
	assert(src);
	assert(src->variety == NODE_SENTINEL);
	src->depth = subst->basedepth + var;	/* update sentinel depth */
	for (struct node *curr = src->prev; !done(curr); curr = curr->prev) {
		assert(curr->nref == 1);
		edit_node(curr, var, subst);
		assert(curr->depth == src->depth);
	}
}

/*
 * When substituting without copying, the only slots we change are
 * bound variables.  Each bound variable might be shifted (or left
 * unshifted if below the cutoff defined by 'var'), or might be
 * substituted, in which case the slot changes from BOUND to SUBST.
 * Pre-existing substitutions and free variables are left unchanged.
 *
 * The copy_bv() function defined above suffices for this... we don't
 * need to create an alternative edit_bv().
 *
 * We also have to descend into bodies, increasing the abstraction
 * depth for binding terms but leaving it unchanged for non-binding
 * subexpressions.
 */
static void edit_node(struct node *src, int var, struct subst *subst)
{
	/* update depth on every node we traverse */
	src->depth = subst->basedepth + var;

	/* handle each slot */
	for (size_t i = src->nslots; i--; /* nada */) {
		struct slot *slot = &src->slots[i];
		if (slot->variety == SLOT_BODY) {
			edit_body(slot->subst, var + !!node_is_binder(src),
				  subst);
		} else if (slot->variety == SLOT_BOUND) {
			src->slots[i] = copy_bv(slot->bv.up, slot->bv.across,
						var, subst);
		}
	}
}

void subst_edit(struct node *body, struct subst *subst)
{
	edit_body(body, 0 /* current substitution bv depth */, subst);
}
