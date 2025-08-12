/*
 * Copyright (c) 2009-2025 Michael P. Touloumtzis.
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

#include <util/memutil.h>
#include <util/message.h>

#include "heap.h"
#include "node.h"
#include "num.h"
#include "memloc.h"
#include "prim.h"

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

	assert(nparams);
	enum node_variety variety = params[0] == the_empty_symbol ?
		NODE_ABS : NODE_FIX;
	struct node *node = node_alloc(variety, prev, depth, nparams);
	node->slots[SLOT_ABS_BODY].variety = SLOT_BODY;
	node->slots[SLOT_ABS_BODY].subst = body;
	for (size_t i = 1; i < nparams; ++i) {
		node->slots[i].variety = SLOT_PARAM;
		node->slots[i].name = params[i];
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

struct node *NodeConstant(struct node *prev, int depth, size_t index)
{
	struct node *node = node_alloc(NODE_VAR, prev, depth, 1);
	node->slots[0].variety = SLOT_CONSTANT;
	node->slots[0].index = index;
	return node;
}

struct node *NodeGeneric(struct node *prev, int depth, size_t nslots)
{
	return node_alloc(NODE_INVALID, prev, depth, nslots);
}

struct node *NodeLet(struct node *prev, int depth, size_t ndefs)
{
	assert(ndefs);
	return node_alloc(NODE_LET, prev, depth, ndefs);
}

struct node *NodeNum(struct node *prev, int depth, double num)
{
	struct node *node = node_alloc(NODE_VAL, prev, depth, 1);
	node->slots[0].variety = SLOT_NUM;
	node->slots[0].num = num;
	return node;
}

struct node *NodePrim(struct node *prev, int depth, const struct prim *prim)
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

struct node *NodeString(struct node *prev, int depth, const char *str)
{
	struct node *node = node_alloc(NODE_VAL, prev, depth, 1);
	node->slots[0].variety = SLOT_STRING;
	node->slots[0].str = xstrdup(str);
	return node;
}

struct node *NodeSubst(struct node *prev, int depth, struct node *subst)
{
	struct node *node = node_alloc(NODE_VAR, prev, depth, 1);
	node->slots[0].variety = SLOT_SUBST;
	node->slots[0].subst = subst;
	subst->nref++;
	return node;
}

struct node *NodeSymbol(struct node *prev, int depth, symbol_mt sym)
{
	struct node *node = node_alloc(NODE_VAL, prev, depth, 1);
	node->slots[0].variety = SLOT_SYMBOL;
	node->slots[0].sym = sym;
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
			struct node *subst = node->slots[i].subst;
			assert(subst->nref > 0);
			subst->nref--;
		}
	}
}

static void node_free_body(struct node *node)
{
	/*
	 * First decrement reference count via the substitution.
	 */
	assert(node->variety == NODE_SENTINEL);
	assert(node->nslots == 1);
	assert(node->slots[0].variety == SLOT_SUBST);
	node->slots[0].subst->nref--;

	/*
	 * Then garbage-collect linked nodes.  These should all have
	 * zero reference count if we properly collect left-to-right.
	 */
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
	if (!node) return;
	assert(!node->nref);

	switch (node->variety) {
	case NODE_SENTINEL:
		node_free_body(node);
		break;
	case NODE_ABS:
	case NODE_FIX:
		node_free(node_abs_body(node));
		break;
	case NODE_LET:
		/* let bodies aren't connected to chains; free here */
		assert(node->slots[0].variety == SLOT_BODY);
		node_free(node->slots[0].subst);
		break;
	case NODE_TEST:
		node_free(node->slots[SLOT_TEST_CSQ].subst);
		node_free(node->slots[SLOT_TEST_ALT].subst);
		break;
	case NODE_VAL:
		if (node->slots[0].variety == SLOT_STRING)
			xfree(node->slots[0].str);
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

void node_recycle(struct node *node)
{
	assert(node->nref == 1);
	node->nref--, node_deref(node), node->nref++;
	for (size_t i = 0; i < node->nslots; ++i)
		node->slots[i].variety = SLOT_NULL;
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
	assert(node_is_binder(abs));
	assert(abs->variety == NODE_LET || abs->nref == 0);
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

static void node_list_indent(unsigned depth)
{
	fputs("          ", stdout);
	for (unsigned i = 0; i < depth; ++i)
		fputs(".   ", stdout);
}

static void node_list_slot(struct slot slot)
{
	switch (slot.variety) {
	case SLOT_BOUND:	printf("bound[%d.%d]",
				       slot.bv.up, slot.bv.across);
				break;
	case SLOT_CONSTANT:	printf("constant[%zu]", slot.index);
				break;
	case SLOT_NUM:		fputs("num[", stdout);
				num_print(slot.num);
				fputs("]", stdout);
				break;
	case SLOT_PARAM:	fputs(symtab_lookup(slot.name), stdout); break;
	case SLOT_PRIM:		printf("prim[%s]", slot.prim->name); break;
	case SLOT_STRING:	printf("str[%s]", slot.str); break;
	case SLOT_SYMBOL:	printf("#%s", symtab_lookup(slot.sym)); break;
	case SLOT_BODY: case SLOT_SUBST:
				printf("^%s", memloc(slot.subst));
				break;
	default:	panicf("Unhandled slot variety %d\n", slot.variety);
	}
}

static void
node_list_contents(const struct node *node)
{
	switch (node->variety) {
	case NODE_ABS:
	case NODE_FIX:
	case NODE_LET:
		for (size_t i = 1; i < node->nslots; ++i) {
			putchar(i == 1 ? '<' : ',');
			node_list_slot(node->slots[i]);
		}
		fputs(">\n", stdout);
		node_list_body(node->slots[0].subst);
		return;
	case NODE_CELL:
		for (size_t i = 0; i < node->nslots; ++i) {
			fputs(i == 0 ? "[" : " | ", stdout);
			node_list_slot(node->slots[i]);
		}
		fputs("]\n", stdout);
		return;
	case NODE_TEST:
		assert(node->nslots == 3);
		putchar('[');
		node_list_slot(node->slots[0]);
		fputs("? ", stdout);
		node_list_slot(node->slots[1]);
		fputs(" | ", stdout);
		node_list_slot(node->slots[2]);
		fputs("]\n", stdout);
		node_list_body(node->slots[1].subst);
		node_list_body(node->slots[2].subst);
		return;
	case NODE_SENTINEL:
	case NODE_VAL:
	case NODE_VAR:
		assert(node->nslots == 1);
		node_list_slot(node->slots[0]);
		putchar('\n');
		return;
	default:
		/* fall through... */;
	}

	putchar('\n');
	for (size_t i = 0; i < node->nslots; ++i) {
		node_list_indent(node->depth);
		node_list_slot(node->slots[i]);
		putchar('\n');
	}
}

static void node_list(const struct node *node)
{
	printf("%8s: ", memloc(node));
	for (unsigned i = 0; i < node->depth; ++i)
		fputs(node->variety == NODE_SENTINEL ? ".>>>" : ".___", stdout);

	printf("@+%d#%d ", node->depth, node->nref);
	node_list_contents(node);
}

void node_list_body(const struct node *node)
{
	if (!node) {
		fputs("{collected}\n", stdout);	  /* see node_wipe_body() */
		return;
	}

	assert(node->variety == NODE_SENTINEL);
	node_list(node);
	for (node = node->next; !done(node); node = node->next)
		node_list(node);
	printf("%8s: ", memloc(node));
	for (unsigned i = 0; i < node->depth; ++i)
		fputs(".<<<", stdout);
	putchar('\n');
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

static void node_print_slot(struct slot slot)
{
	switch (slot.variety) {
	case SLOT_BODY:		putchar('(');
				node_print_body_contents(slot.subst);
				putchar(')');
				break;
	case SLOT_BOUND:	printf("$%d.%d", slot.bv.up, slot.bv.across);
				break;
	case SLOT_CONSTANT:	printf("$%zu", slot.index); break;
	case SLOT_NUM:		num_print(slot.num); break;
	case SLOT_PRIM:		printf("'%s'", slot.prim->name); break;
	case SLOT_STRING:	printf("\"%s\"", slot.str); break;
	case SLOT_SYMBOL:	printf("#%s", symtab_lookup(slot.sym)); break;
	case SLOT_SUBST:	printf("^%s", memloc(slot.subst)); break;
	default:	panicf("Unhandled slot variety %d\n", slot.variety);
	}
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

void node_print(const struct node *node)
{
	printf("[@%s+%d#%d ", memloc(node), node->depth, node->nref);
	node_print_contents(node);
	putchar(']');
}

void node_print_body(const struct node *node)
{
	assert(node->variety == NODE_SENTINEL);
	node_print(node);
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
