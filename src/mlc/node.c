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
#include <stdio.h>

#include <util/base64.h>
#include <util/message.h>

#include "heap.h"
#include "node.h"
#include "memloc.h"
#include "prim.h"
#include "term.h"

static struct node *node_alloc(enum node_variety variety, struct node *prev,
			       int depth, size_t nslots)
{
	struct node *node = node_heap_alloc(nslots);
	node->variety = variety;
	node->isfresh = false;
	node->depth = depth;
	node->nref = 0;
	node->prev = prev;
	node->next = NULL;
	node->forward = NULL;
	node->backref = NULL;
	return node;
}

struct node *NodeAbs(struct node *prev, int depth, struct node *body,
		     size_t nparams, symbol_mt *params)
{
	assert(body->variety == NODE_SENTINEL);
	assert(body->depth == depth + 1);

	struct node *node = node_alloc(NODE_ABS, prev, depth,
				       nparams + 1 /* for body */);
	node->slots[SLOT_ABS_BODY].variety = SLOT_BODY;
	node->slots[SLOT_ABS_BODY].subst = body;
	for (size_t i = 1; i <= nparams; ++i) {
		node->slots[i].variety = SLOT_PARAM;
		node->slots[i].name = params[i-1];
	}
	return node;
}

struct node *NodeAbsCopy(struct node *prev, int depth, struct node *body,
			 struct node *src)
{
	assert(src->variety == NODE_ABS || src->variety == NODE_FIX);
	assert(body->variety == NODE_SENTINEL);
	assert(body->depth == depth + 1);

	struct node *node = node_alloc(src->variety, prev, depth, src->nslots);
	node->slots[SLOT_ABS_BODY].variety = SLOT_BODY;
	node->slots[SLOT_ABS_BODY].subst = body;
	for (size_t i = 1; i < node->nslots; ++i) {
		node->slots[i].variety = SLOT_PARAM;
		node->slots[i].name = src->slots[i].name;
	}
	return node;
}

/*
 * NodeApp doesn't initialize slots--that's the caller's responsibility.
 */
struct node *NodeApp(struct node *prev, int depth, size_t nargs)
{
	assert(nargs);		/* sanity check, not a hard constraint */
	return node_alloc(NODE_APP, prev, depth, nargs + 1 /* for function */);
}

struct node *NodeBoundVar(struct node *prev, int depth, int up, int across)
{
	struct node *node = node_alloc(NODE_VAR, prev, depth, 1);
	node->slots[0].variety = SLOT_BOUND;
	node->slots[0].bv.up = up;
	node->slots[0].bv.across = across;
	return node;
}

struct node *NodeCell(struct node *prev, int depth, size_t n)
{
	return node_alloc(NODE_CELL, prev, depth, n);
}

struct node *NodeFreeVar(struct node *prev, int depth, struct term *var)
{
	struct node *node = node_alloc(NODE_VAR, prev, depth, 1);
	node->slots[0].variety = SLOT_FREE;
	node->slots[0].term = var;
	return node;
}

struct node *NodeGeneric(struct node *prev, int depth, size_t nslots)
{
	return node_alloc(NODE_INVALID, prev, depth, nslots);
}

struct node *NodeNum(struct node *prev, int depth, double num)
{
	struct node *node = node_alloc(NODE_VAL, prev, depth, 1);
	node->slots[0].variety = SLOT_NUM;
	node->slots[0].num = num;
	return node;
}

struct node *NodePrim(struct node *prev, int depth, unsigned prim)
{
	struct node *node = node_alloc(NODE_VAL, prev, depth, 1);
	node->slots[0].variety = SLOT_PRIM;
	node->slots[0].prim = prim;
	return node;
}

struct node *NodeSentinel(struct node *next, struct node *prev, int depth)
{
	struct node *node = node_alloc(NODE_SENTINEL, prev, depth, 1);

	/* finish connecting to doubly-linked list */
	node->next = next;
	next->prev = node;
	prev->next = node;

	/* a sentinel references the first node in l-to-r order */
	node->slots[0].variety = SLOT_SUBST;
	node->slots[0].subst = next;
	next->backref = &node->slots[0];
	next->nref++;

	return node;
}

struct node *NodeTest(struct node *prev, int depth)
{
	/*
	 * Test nodes have 3 slots: predicate, consequent, alternative.
	 * The first is a variable; the other two are subexpressions.
	 */
	const size_t nslots = 3;
	struct node *node = node_alloc(NODE_TEST, prev, depth, nslots);
	node->slots[1].variety = SLOT_BODY;
	node->slots[2].variety = SLOT_BODY;
	return node;
}

void node_deref(struct node *node)
{
	assert(!node->nref);
	for (size_t i = 0; i < node->nslots; ++i) {
		if (node->slots[i].variety == SLOT_SUBST) {
			assert(node->slots[i].subst->nref > 0);
			node->slots[i].subst->nref--;
		}
	}
}

static void node_free_body(struct node *node)
{
	assert(node->variety == NODE_SENTINEL);
	if (node->next == node) return;		/* already pinched */

	/* if the body is intact, first dereference next... */
	assert(node->nslots);
	assert(node->slots[0].variety == SLOT_SUBST);
	assert(node->slots[0].subst == node->next);
	node->slots[0].subst = NULL, node->next->nref--;

	/* then decrement references as we go */
	node = node->next;
	while (!done(node)) {
		struct node *tmp = node->next;
		node_deref(node);
		node_free(node);
		node = tmp;
	}
}

void node_free(struct node *node)
{
	if (!node)
		return;

	switch (node->variety) {
	case NODE_SENTINEL:
		node_free_body(node);
		break;
	case NODE_ABS:
	case NODE_FIX:
		node_free(node_abs_body(node));
		break;
	case NODE_TEST:
		node_free(node->slots[SLOT_TEST_CSQ].subst);
		node_free(node->slots[SLOT_TEST_ALT].subst);
		break;
	default:
		/* nada */;
	}
	node_heap_free(node);
}

int node_abs_depth(const struct node *node)
{
	int depth;
	for (depth = 0; node->variety == NODE_VAR &&
	     node->slots[0].variety == SLOT_SUBST; ++depth)
		node = node->slots[0].subst;
	return node_is_abs(node) ? depth : -1;
}

const struct node *node_chase_lhs(const struct node *node)
{
	while (node->slots[0].variety == SLOT_SUBST)
		node = node->slots[0].subst;
	return node;
}

void node_insert_after(struct node *node, struct node *dest)
{
	node->prev = dest;
	node->next = dest->next;
	node->prev->next = node;
	node->next->prev = node;
}

void node_replace(struct node *node, struct node *dest)
{
	node->prev = dest->prev;
	node->next = dest->next;
	node->prev->next = node;
	node->next->prev = node;
}

struct node *node_take_body(struct node *abs)
{
	assert(node_is_abs(abs));
	assert(abs->nref == 0);
	struct node *body = node_abs_body(abs);
	abs->slots[SLOT_ABS_BODY].subst = NULL;		/* wipe body */
	return body;
}

void node_wipe_body(struct node *abs)
{
	assert(node_is_abs(abs));
	assert(abs->nref == 0);
	node_free(node_abs_body(abs));
	abs->slots[SLOT_ABS_BODY].subst = NULL;		/* wipe body */
}

static void node_list(const struct node *node, uintptr_t base,
		      unsigned depth, bool star);
static void node_list_helper(struct node *node, uintptr_t base, unsigned depth);

static void node_list_addr(const struct node *node, intptr_t base,
			   unsigned char *dst, size_t dstsize)
{
	intptr_t diff = ((intptr_t) (struct node *) node) - base;
	diff = diff < 0 ? diff * -2 - 1 : diff * 2;
	assert(diff >= 0);
	base64_convert(diff, dst, dstsize);
}

static void
node_list_header(const struct node *node, intptr_t base, unsigned depth)
{
	assert(depth >= node->depth);	/* we indent non-abstraction depth */
	/*
	 * XXX the node->nref test doesn't work for tests since the
	 * consequent and alternative have nref 0, but we want to print
	 * their addresses.  Brute forcing it until I have a better
	 * solution.
	 */
	if (1 || node->nref) {
		unsigned char buf [BASE64_CONVERT_BUFSIZE];
		node_list_addr(node, base, buf, sizeof buf);
		printf("%12s: ", buf);
	} else
		fputs("              ", stdout);
	for (unsigned i = 0; i < depth; ++i)
		fputs("____", stdout);
}

static void node_list_indent(unsigned depth)
{
	fputs("              ", stdout);
	for (unsigned i = 0; i < depth; ++i)
		fputs("    ", stdout);
}

static void node_list_slot(struct slot slot, intptr_t base)
{
	unsigned char buf [BASE64_CONVERT_BUFSIZE];
	switch (slot.variety) {
	case SLOT_BOUND:	printf("bound[%d.%d]",
				       slot.bv.up, slot.bv.across);
				break;
	case SLOT_FREE:		fputs("free[", stdout);
				term_print(slot.term);
				fputs("]", stdout);
				break;
	case SLOT_NUM:		printf("num[%g]", slot.num);
				break;
	case SLOT_PRIM:		printf("prim[%s]", prim_name(slot.prim));
				break;
	case SLOT_SUBST:	node_list_addr(slot.subst, base,
					       buf, sizeof buf);
				printf("^%s", buf);
				break;
	default:	panicf("Unhandled slot variety %d\n", slot.variety);
	}
}

static void
node_list_contents(const struct node *node, uintptr_t base, unsigned depth)
{
	switch (node->variety) {
	case NODE_CELL:
		for (size_t i = 0; i < node->nslots; ++i) {
			fputs(i == 0 ? "[" : " | ", stdout);
			node_list_slot(node->slots[i], base);
		}
		fputs("]\n", stdout);
		return;
	case NODE_TEST:
		assert(node->nslots == 5);
		putchar('[');
		node_list_slot(node->slots[0], base);
		fputs("? ", stdout);
		node_list_slot(node->slots[2], base);
		fputs(" | ", stdout);
		node_list_slot(node->slots[4], base);
		fputs("]\n", stdout);
		return;
	default:
		/* fall through... */;
	}

	assert(depth >= node->depth);	/* we indent non-abstraction depth */
	if (node_is_abs(node)) {
		for (size_t i = 1; i < node->nslots; ++i)
			printf("%s%s", i == 1 ? "<" : ",",
			       symtab_lookup(node->slots[i].name));
		fputs(">\n", stdout);
		/* body may have been wiped with node_wipe_body() */
		if (node_abs_body(node))
			node_list_helper(node_abs_body(node), base, depth + 1);
		else
			fputs("{collected}\n", stdout);
		return;
	}
	putchar('\n');

	for (size_t i = 0; i < node->nslots; ++i) {
		node_list_indent(depth);
		node_list_slot(node->slots[i], base);
		putchar('\n');
	}
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
static void node_list(const struct node *node, uintptr_t base,
		      unsigned depth, bool star)
{
	assert(depth >= node->depth);	/* we indent non-abstraction depth */
	node_list_header(node, base, depth);
	if (star) {
		assert(node->nref == 0);
		printf("*+%d\t", node->depth);
	} else
		printf("@+%d#%d\t", node->depth, node->nref);
	node_list_contents(node, base, depth);
}

/*
 * To avoid recursing down a (potentially very long) node list and
 * blowing the stack, we use a pointer-reversing traversal as we do
 * in reduction.  First reverse, then print on the way back.
 */
static void node_list_helper(struct node *node, uintptr_t base, unsigned depth)
{
	assert(depth >= node->depth);	/* we indent non-abstraction depth */
	struct node *rev, *tmp;
	for (rev = NULL; node;
	     tmp = node->prev, node->prev = rev, rev = node, node = tmp);
	for (bool first = true; rev; first = false,
	     tmp = rev->prev, rev->prev = node, node = rev, rev = tmp)
		node_list(rev, base, depth, first);
	assert(rev == NULL);
}

void node_list_rl(struct node *node)
{
	node_list_helper(node, (intptr_t) node, 0);
}

static void node_print_slot(struct slot slot)
{
	switch (slot.variety) {
	case SLOT_BOUND:	printf("$%d.%d", slot.bv.up, slot.bv.across);
				break;
	case SLOT_FREE:		term_print(slot.term); break;
	case SLOT_NUM:		printf("%g", slot.num); break;
	case SLOT_PRIM:		printf("'%s'", prim_name(slot.prim)); break;
	case SLOT_SUBST:	printf("^%s", memloc(slot.subst)); break;
	default:	panicf("Unhandled slot variety %d\n", slot.variety);
	}
}

static void node_print_body_contents(const struct node *node)
{
	/* body may have been wiped with node_wipe_body() */
	if (node) {
		assert(node->variety == NODE_SENTINEL);
		node_print_body(node);
	} else
		fputs("{collected}", stdout);
}

static void node_print_contents(const struct node *node)
{
	switch (node->variety) {
	case NODE_CELL:
		for (size_t i = 0; i < node->nslots; ++i) {
			if (i) fputs(" | ", stdout);
			node_print_slot(node->slots[i]);
		}
		return;
	case NODE_TEST:
		assert(node->nslots == 3);
		node_print_slot(node->slots[0]);	/* predicate */
		fputs("? ", stdout);
		node_print_body_contents(node->slots[SLOT_TEST_CSQ].subst);
		fputs(" | ", stdout);
		node_print_body_contents(node->slots[SLOT_TEST_ALT].subst);
		return;
	default:
		/* fall through... */;
	}

	if (node_is_abs(node)) {
		for (size_t i = 1; i < node->nslots; ++i)
			printf("%s%s", i == 1 ? "<" : ",",
			       symtab_lookup(node->slots[i].name));
		fputs(">.", stdout);
		node_print_body_contents(node_abs_body(node));
		return;
	}

	assert(node->nslots);
	node_print_slot(node->slots[0]);
	for (size_t i = 1; i < node->nslots; ++i) {
		fputs(i == 1 ? " (" : ", ", stdout);
		node_print_slot(node->slots[i]);
	}
	if (node->nslots > 1) putchar(')');
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
static void node_print(const struct node *node)
{
	if (false /* XXX */) {
		assert(node->nref == 0);
		printf("[*+%d ", node->depth);
	} else
		printf("[@%s+%d#%d ", memloc(node), node->depth, node->nref);
	node_print_contents(node);
	putchar(']');
}

void node_print_body(const struct node *node)
{
	assert(node->variety == NODE_SENTINEL);
	for (node = node->next; !done(node); node = node->next)
		node_print(node);
}

void node_print_after(const struct node *node)
{
	for (/* nada */; !done(node); node = node->next)
		node_print(node);
}

void node_print_until(const struct node *node)
{
	/* XXX this is inelegant */
	if (done(node)) return;
	const struct node *curr;
	for (curr = node; !done(curr); curr = curr->prev);
	assert(curr->variety == NODE_SENTINEL);
	do {
		curr = curr->next;
		node_print(curr);
	} while (curr != node);
}
