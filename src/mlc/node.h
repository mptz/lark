#ifndef LARK_MLC_NODE_H
#define LARK_MLC_NODE_H
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

#include <stdbool.h>

#include <util/symtab.h>

#define SLOT_ABS_BODY 0
#define SLOT_APP_FUNC 0

enum slot_variety {
	SLOT_INVALID,
	SLOT_BOUND,
	SLOT_FREE,
	SLOT_FUNC,	/* function body for abstraction */
	SLOT_NULL,	/* placeholder for missing value */
	SLOT_NUM,
	SLOT_PARAM,	/* formal parameter to abstraction */
	SLOT_PRIM,
	SLOT_SELF,	/* self-reference for recursive function */
	SLOT_SUBST,
} __attribute__ ((packed));

struct slot {
	enum slot_variety variety;
	union {
		struct { int up, across; } bv;
		symbol_mt name;		/* free vars & formal params */
		unsigned prim;
		double num;
		struct node *subst;
		struct term *term;
	};
};

/*
 * We essentially support three types of references: bound variables,
 * free variables, and substitutions (pointers to nodes).  Application
 * nodes contain only references of these types--they don't directly
 * contain values (at present).
 */
static inline bool slot_is_ref(const struct slot slot)
	{ return slot.variety == SLOT_BOUND ||
		 slot.variety == SLOT_FREE ||
		 slot.variety == SLOT_SUBST; }

/*
 * We have gotten away for a long time without node varieties, but the
 * more complex reduction gets the more we may want to classify nodes
 * this way.
 */
enum node_variety {
	NODE_INVALID,
	NODE_TEST,
} __attribute__ ((packed));

/*
 * An n-ary node in the crumbled (semicompiled) representation of our
 * calculus terms.  Whereas the classic lambda calculus has three types
 * of nodes (abstraction, application, and variable) we fundamentally
 * have only two, which are distinguished not by variant tags/enums but
 * by the types of slot they contain:
 *
 * An *abstraction* node contains:
 *	slots[0]	SLOT_FUNC	function body
 *	slots[1]	SLOT_SELF	self-reference parameter OR
 *			SLOT_NULL	nothing (non-recursive)
 *	slots[2..]	SLOT_PARAM	ordinary abstraction parameters
 *
 * There is always at least one parameter, since 0-ary abstractions
 * collapse to their bodies (we don't support substitutionless "thunks");
 * this ordinary parameter may be the self-reference.
 *
 * An *application* node contains one or more slots of the following
 * types: SLOT_BOUND, SLOT_FREE, SLOT_NUM, SLOT_PRIM, SLOT_SUBST.  An
 * application node with a single slot represents an atomic value or
 * variable; with two or more slots slot[0] contains the function or
 * primitive operation and slots[1..] contain the arguments.
 */
struct node {
	enum node_variety variety;
	bool isfresh,		/* freshly allocated subst? (not a copy) */
	     isvalue;		/* a value? (abstraction or atom) */
	int depth,		/* abstraction depth */
	    nref;		/* reference count for gc */
	size_t nslots;		/* slot count for this node */
	struct node *prev;	/* previous (or next when reversing) */
	union {
		struct node *forward,	/* forwarding pointer during copy */
			    *outer;	/* enclosing environment during
					   reduction under abstraction */
	};
	struct slot *backref;	/* unique slot referencing this node */
	struct slot slots[];
};

static inline bool node_is_abs(const struct node *node)
	{ return node->slots[0].variety == SLOT_FUNC; }
static inline bool node_is_prim(const struct node *node)
	{ return node->slots[0].variety == SLOT_PRIM; }
static inline struct node *node_abs_body(const struct node *abs)
	{ return abs->slots[0].subst; }
/* XXX this keeps getting messier. Overhaul coming */
static inline bool node_is_app(const struct node *node)
	{ return !node->isvalue && (node->nslots > 1) &&
		 node->variety != NODE_TEST; }
static inline size_t node_app_nargs(const struct node *app)
	{ return app->nslots - 1; }

struct node *NodeAbs(struct node *prev, int depth, struct node *body,
		     size_t nparams, symbol_mt *params);
struct node *NodeAbsCopy(struct node *prev, int depth, struct node *body,
			 struct node *src);	/* src for params only */
struct node *NodeApp(struct node *prev, int depth, size_t nargs);
struct node *NodeBoundVar(struct node *prev, int depth, int up, int across);
struct node *NodeFreeVar(struct node *prev, int depth, struct term *var);
struct node *NodeNum(struct node *prev, int depth, double num);
struct node *NodePrim(struct node *prev, int depth, unsigned prim);
struct node *NodeSubst(struct node *prev, int depth, struct node *subst);
struct node *NodeTest(struct node *prev, int depth);

extern int node_abs_depth(const struct node *node);
extern const struct node *node_chase_lhs(const struct node *node);
extern bool node_check_root(const struct node *node);
extern void node_free(struct node *node);
extern void node_free_body(struct node *abs);
extern void node_free_env(struct node *node);
extern void node_free_shallow(struct node *node);
	/* {list,print}_rl are non-const b/c ptr reversing */
extern void node_list_rl(struct node *node);
extern void node_print_rl(struct node *node);
extern void node_print_lr(const struct node *node, bool complete);
extern struct node *node_take_body(struct node *abs);

#endif /* LARK_MLC_NODE_H */
