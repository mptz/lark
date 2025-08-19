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
#include <stddef.h>

#include <util/symtab.h>

#define SLOT_ABS_BODY 0
#define SLOT_APP_FUNC 0
#define SLOT_BINDER_BODY 0
#define SLOT_TEST_PRED 0
#define SLOT_TEST_CSQ 1
#define SLOT_TEST_ALT 2

enum slot_variety {
	SLOT_INVALID,
	SLOT_BODY,	/* subexpression e.g. function body */
	SLOT_BOUND,	/* bound variable, De Bruijn indexed */
	SLOT_CONSTANT,	/* opaque constant in environment */
	SLOT_NULL,	/* placeholder for missing value */
	SLOT_NUM,	/* floating-point number */
	SLOT_PARAM,	/* formal parameter to abstraction */
	SLOT_PRIM,	/* primitive (built-in) function */
	SLOT_STRING,	/* nul-terminated string */
	SLOT_SYMBOL,	/* self-evaluating symbol */
	SLOT_SUBST,	/* explicit substitution, as node pointer */
} __attribute__ ((packed));

struct slot {
	enum slot_variety variety;
	union {
		struct { int up, across; } bv;	/* bound variable */
		size_t index;			/* constant from env */
		symbol_mt name;			/* formal parameter */
		double num;			/* floating-point number */
		const struct prim *prim;	/* primitive function */
		const char *str;		/* nul-terminated string */
		struct node *node;		/* body, extern, subst */
		symbol_mt sym;			/* self-evaluating symbol */
	};
};

/*
 * We essentially support three types of references: bound variables,
 * constants, and substitutions (pointers to nodes).  Application
 * nodes contain only references of these types--they don't directly
 * contain values (at present).  They could validly contain other
 * things which aren't useful to share (atomics, references to global
 * constants) but we're holding off from that complexity for now.
 */
static inline bool slot_is_ref(const struct slot slot)
	{ return slot.variety == SLOT_BOUND ||
		 slot.variety == SLOT_CONSTANT ||
		 slot.variety == SLOT_SUBST; }

/*
 * We have gotten away for a long time without node varieties, but the
 * more complex reduction gets the more we may want to classify nodes
 * this way.
 */
enum node_variety {
	NODE_INVALID,
	NODE_SENTINEL,
	NODE_ABS,
	NODE_APP,
	NODE_CELL,
	NODE_FIX,
	NODE_LET,
	NODE_TEST,
	NODE_VAL,
	NODE_VAR,
} __attribute__ ((packed));

/*
 * An n-ary node in the flattened (semicompiled) representation of our
 * calculus terms.  Whereas the classic lambda calculus has three types
 * of nodes (abstraction, application, and variable) we fundamentally
 * have only two, which are distinguished not by variant tags/enums but
 * by the types of slot they contain:
 *
 * An *abstraction* node contains:
 *	slots[0]	SLOT_BODY	function body
 *	slots[1..]	SLOT_PARAM	abstraction parameters
 *
 * There is always at least one parameter, since 0-ary abstractions
 * collapse to their bodies (we don't support substitutionless "thunks");
 * this ordinary parameter may be the self-reference.
 *
 * An *application* node contains one or more slots of the following
 * types: SLOT_BOUND, SLOT_CONSTANT, SLOT_NUM, SLOT_PRIM, SLOT_SUBST.  An
 * application node with a single slot represents an atomic value or
 * variable; with two or more slots slot[0] contains the function or
 * primitive operation and slots[1..] contain the arguments.
 */
struct node {
	enum node_variety variety;
	bool isfresh;		/* freshly allocated subst? (not a copy) */
	int depth,		/* abstraction depth */
	    nref;		/* reference count for gc */
	size_t nslots;		/* slot count for this node */
	struct node *prev, *next;	/* for doubly-linked node chains */
	union {
		struct node *forward,	/* forwarding pointer during copy */
			    *outer;	/* enclosing environment during
					   reduction under abstraction */
	};
	struct slot *backref;	/* unique slot referencing this node */
	struct slot slots[];
};

struct node_chain {
	struct node *next, *prev;
};

static inline bool node_is_abs(const struct node *node)
	{ return node->variety == NODE_ABS || node->variety == NODE_FIX; }
static inline bool node_is_binder(const struct node *node)
	{ return node->variety == NODE_ABS ||
		 node->variety == NODE_FIX ||
		 node->variety == NODE_LET; }
static inline bool node_is_prim(const struct node *node)
	{ return node->slots[0].variety == SLOT_PRIM; }
static inline struct node *node_abs_body(const struct node *abs)
	{ return abs->slots[SLOT_ABS_BODY].node; }
static inline size_t node_app_nargs(const struct node *app)
	{ return app->nslots - 1; }
static inline struct node *node_binder_body(const struct node *node)
	{ return node->slots[SLOT_BINDER_BODY].node; }
static inline bool done(const struct node *node)
	{ return node->variety == NODE_SENTINEL; }
static inline void node_pinch(struct node *node)
	{ node->prev = node->next = node; }
static inline void node_remove(struct node *node)
	{ node->prev->next = node->next; node->next->prev = node->prev; }

extern struct node *NodeAbs(struct node *prev, int depth, struct node *body,
			    size_t nparams, symbol_mt *params);
extern struct node *NodeApp(struct node *prev, int depth, size_t nargs);
extern struct node *NodeBoundVar(struct node *prev, int depth,
				 int up, int across);
extern struct node *NodeCell(struct node *prev, int depth, size_t n);
extern struct node *NodeConstant(struct node *prev, int depth, size_t index);
extern struct node *NodeGeneric(struct node *prev, int depth, size_t nslots);
extern struct node *NodeLet(struct node *prev, int depth, size_t ndefs);
extern struct node *NodeNum(struct node *prev, int depth, double num);
extern struct node *NodePrim(struct node *prev, int depth,
			     const struct prim *prim);
extern struct node *NodeSentinel(struct node *next, struct node *prev,
				 int depth);
extern struct node *NodeString(struct node *prev, int depth, const char *str);
extern struct node *NodeSubst(struct node *prev, int depth, struct node *subst);
extern struct node *NodeSymbol(struct node *prev, int depth, symbol_mt sym);
extern struct node *NodeTest(struct node *prev, int depth);

extern const struct node *node_chase_lhs(const struct node *node);
extern void node_deref(struct node *node);
extern void node_free(struct node *node);
extern void node_insert_after(struct node *node, struct node *dest);
extern void node_recycle(struct node *node);
extern void node_replace(struct node *node, struct node *dest);
extern int node_subst_depth(const struct node *node);
extern struct node *node_take_body(struct node *abs);
extern void node_wipe_body(struct node *abs);

extern void node_list_body(const struct node *node);
extern void node_print(const struct node *node);
extern void node_print_body(const struct node *node);
extern void node_print_after(const struct node *node);
extern void node_print_until(const struct node *node);

#endif /* LARK_MLC_NODE_H */
