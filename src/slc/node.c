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
#include <stdio.h>

#include <util/message.h>

#include "heap.h"
#include "node.h"
#include "memloc.h"
#include "term.h"

static struct node *node_alloc(unsigned bits, struct node *prev, int depth)
{
	struct node *node = node_heap_alloc();
	node->bits = bits;
	node->depth = depth;
	node->nref = 0;
	node->prev = prev;
	node->forward = NULL;
	node->backref = NULL;
	return node;
}

struct node *NodeAbs(struct node *prev, int depth,
		     symbol_mt formal, struct node *body)
{
	struct node *node = node_alloc(NODE_BITS_ABS, prev, depth);
	node->lhs.name = formal;
	node->rhs.subst = body;
	return node;
}

struct node *NodeApp(struct node *prev, int depth)
{
	/*
	 * The caller will have to set bits after filling the slots.
	 */
	return node_alloc(NODE_INVALID, prev, depth);
}

struct node *NodeBoundVar(struct node *prev, int depth, int index)
{
	struct node *node = node_alloc(NODE_LHS_BOUND, prev, depth);
	node->lhs.index = index;
	node->rhs.subst = NULL;
	return node;
}

struct node *NodeFreeVar(struct node *prev, int depth, struct term *var)
{
	struct node *node = node_alloc(NODE_LHS_FREE, prev, depth);
	node->lhs.term = var;
	node->rhs.term = NULL;
	return node;
}

struct node *NodeSubst(struct node *prev, int depth, struct node *subst)
{
	struct node *node = node_alloc(NODE_LHS_SUBST, prev, depth);
	node->lhs.subst = subst;
	node->rhs.subst = NULL;
	return node;
}

void node_free(struct node *node)
{
	assert(node);
	if (node->bits == NODE_BITS_ABS)
		node_free_env(node_abs_body(node));
	node_heap_free(node);
}

void node_free_body(struct node *abs)
{
	assert(abs->bits == NODE_BITS_ABS);
	assert(abs->nref == 0);
	node_free_env(node_abs_body(abs));
	abs->rhs.subst = NULL;		/* wipe body */
}

void node_free_env(struct node *node)
{
	while (node) {
		struct node *tmp = node->prev;
		node_free(node);
		node = tmp;
	}
}

void node_free_shallow(struct node *node)
{
	assert(node);
	node_heap_free(node);
}

int node_abs_depth(const struct node *node)
{
	int depth;
	for (depth = 0; node->bits == NODE_LHS_SUBST; ++depth)
		node = node->lhs.subst;
	return node->bits == NODE_BITS_ABS ? depth : -1;
}

const struct node *node_chase(const struct node *node)
{
	while (node->bits == NODE_LHS_SUBST)
		node = node->lhs.subst;
	return node;
}

void node_move_contents(struct node *dst, struct node *src)
{
	assert(!src->forward && !dst->forward);	/* not copying */
	assert(!src->nref);			/* only copy STAR r.n. */
	assert(!src->backref);			/* only copy STAR r.n. */
	dst->bits = src->bits;
	dst->lhs = src->lhs;
	dst->rhs = src->rhs;
	src->bits = NODE_INVALID;

	/*
	 * If the destination node references others via substitutions,
	 * updated backreferences accordingly.
	 */
	struct node *target;
	if (dst->bits & NODE_LHS_SUBST) {
		target = dst->lhs.subst;
		if (target->backref == &src->lhs)
			target->backref = &dst->lhs;
	}
	if (dst->bits & NODE_RHS_SUBST) {
		target = dst->rhs.subst;
		if (target->backref == &src->rhs)
			target->backref = &dst->rhs;
	}
}

struct node *node_take_body(struct node *abs)
{
	assert(abs->bits == NODE_BITS_ABS);
	assert(abs->nref == 0);
	struct node *body = node_abs_body(abs);
	abs->rhs.subst = NULL;		/* wipe body */
	return body;
}

static inline void prindex(union slot slot)
	{ printf("%d", slot.index); }
static inline void prsubst(union slot slot)
	{ printf("^%s", memloc(slot.subst)); }
static inline void prterm(union slot slot)
	{ term_print(slot.term); }

static void node_print_lhs(const struct node *node)
{
	assert(node->bits & NODE_LHS_MASK);
	if (node->bits & NODE_LHS_BOUND) prindex(node->lhs);
	if (node->bits & NODE_LHS_FREE) prterm(node->lhs);
	if (node->bits & NODE_LHS_SUBST) prsubst(node->lhs);
}

static void node_print_rhs(const struct node *node)
{
	assert(node->bits & NODE_RHS_MASK);
	if (node->bits & NODE_RHS_BOUND) prindex(node->rhs);
	if (node->bits & NODE_RHS_FREE) prterm(node->rhs);
	if (node->bits & NODE_RHS_SUBST) prsubst(node->rhs);
}

static void node_print_contents(const struct node *node)
{
	if (node->bits == NODE_BITS_ABS) {
		printf("<%s>.", symtab_lookup(node_abs_formal(node)));
		/* body may have been wiped with node_free_body() */
		if (node_abs_body(node))
			node_print_rl(node_abs_body(node));
		else
			fputs("{collected}", stdout);
	} else if (node->bits & NODE_MASK_APP) {
		putchar('(');
		node_print_lhs(node);
		putchar(' ');
		node_print_rhs(node);
		putchar(')');
	} else
		node_print_lhs(node);
}

/*
 * The first node at toplevel and within each abstraction is a
 * "virtual substitution" for the value of the term as a whole,
 * denoted '*'.  Because it's associated with a nameless variable,
 * it can't be referenced--we confirm its reference count is 0 and
 * don't print its location.
 *
 * We can't check node->prev to determine whether this node is in
 * '*' position since we flip the direction of 'prev' pointers during
 * reduction and printing, so we have to be told by the caller.
 */
static void node_print(const struct node *node, bool star)
{
	if (star) {
		assert(node->nref == 0);
		printf("[+%d#* ", node->depth);
	} else
		printf("[@%s+%d#%d ", memloc(node), node->depth, node->nref);
	node_print_contents(node);
	putchar(']');
}

/*
 * To avoid recursing down a (potentially very long) node list and
 * blowing the stack, we use a pointer-reversing traversal as we do
 * in reduction.  First reverse, then print on the way back.
 */
void node_print_rl(struct node *node)
{
	struct node *rev, *tmp;
	for (rev = NULL; node;
	     tmp = node->prev, node->prev = rev, rev = node, node = tmp);
	for (bool first = true; rev; first = false,
	     tmp = rev->prev, rev->prev = node, node = rev, rev = tmp)
		node_print(rev, first);
	assert(rev == NULL);
}

/*
 * For printing and validity-checking reasons we need to know if the
 * first node is in '*' position, which is the case iff we're printing
 * a complete environment (e.g at the end of an R-to-L traversal).
 */
void node_print_lr(const struct node *node, bool complete)
{
	for (/* nada */; node; complete = false, node = node->prev)
		node_print(node, complete);
}
