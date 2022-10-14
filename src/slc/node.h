#ifndef LARK_SLC_NODE_H
#define LARK_SLC_NODE_H
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

#include <stdbool.h>

#include <util/symtab.h>

/* abstractions have no bits set */
#define NODE_BITS_ABS	0

/* LHS bits are set both for both variables and applications */
#define NODE_LHS_BOUND	0x01
#define NODE_LHS_FREE	0x02
#define NODE_LHS_SUBST	0x04
#define NODE_LHS_MASK	0x07

/* RHS bits are set only for applications */
#define NODE_RHS_BOUND	0x10
#define NODE_RHS_FREE	0x20
#define NODE_RHS_SUBST	0x40
#define NODE_RHS_MASK	0x70	/* aka... */
#define NODE_MASK_APP	0x70

/* left-shift by this amount to convert LHS bits to RHS */
#define NODE_LHS_RHS_SHIFT	4

/* temporary bits during application initialization */
#define NODE_INVALID	0x88

union slot {
	int index;
	symbol_mt name;
	struct node *subst;
	struct term *term;
};

struct node {
	unsigned bits;		/* contents flag bits */
	int depth,		/* abstraction depth */
	    nref;		/* reference count for gc */
	struct node *prev;	/* previous (or next when reversing) */
	union {
		struct node *forward,	/* forwarding pointer during copy */
			    *outer;	/* enclosing environment during
					   reduction under abstraction */
	};
	union slot lhs, rhs,	/* contents of node */
		   *backref;	/* unique slot referencing this node */
};

static inline symbol_mt node_abs_formal(const struct node *abs)
	{ return abs->lhs.name; }
static inline struct node *node_abs_body(const struct node *abs)
	{ return abs->rhs.subst; }

struct node *NodeAbs(struct node *prev, int depth,
		     symbol_mt formal, struct node *body);
struct node *NodeApp(struct node *prev, int depth);
struct node *NodeBoundVar(struct node *prev, int depth, int index);
struct node *NodeFreeVar(struct node *prev, int depth, struct term *var);
struct node *NodeSubst(struct node *prev, int depth, struct node *subst);

extern int node_abs_depth(const struct node *node);
extern const struct node *node_chase(const struct node *node);
extern void node_free(struct node *node);
extern void node_free_body(struct node *abs);
extern void node_free_env(struct node *node);
extern void node_free_shallow(struct node *node);
extern void node_move_contents(struct node *dst, struct node *src);
extern void node_print_rl(struct node *node); /* non-const b/c ptr reversing */
extern void node_print_lr(const struct node *node, bool complete);
extern struct node *node_take_body(struct node *abs);

#endif /* LARK_SLC_NODE_H */
