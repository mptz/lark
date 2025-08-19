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

#include <util/message.h>

#include "binder.h"
#include "flatten.h"
#include "node.h"
#include "term.h"

/*
 * The global environment contains sentinel nodes; the actual node to
 * which we link via substitution is referenced by a substitution in
 * the sentinel's 0th slot.  This removal of the indirection through
 * the global constant resembles a rename-during-reduction.
 */
static struct node *constant_of(const struct term *term)
{
	struct node *n = term->constant.binder->node;
	assert(n);
	assert(n->variety == NODE_SENTINEL);
	assert(n->nref == 0);
	assert(n->backref == NULL);
	n = n->slots[0].subst;

	/*
	 * Note that n's predecessor, n->prev, may not be the same term
	 * as the binder's value, if for example one global variable
	 * is simply an alias for another.  Nevertheless n->prev should
	 * be *a* sentinel since we should never see an global binder
	 * referencing an interior portion of an environment node.
	 */
	assert(n->prev->variety == NODE_SENTINEL);
	assert(n->prev->backref == NULL);

	/*
	 * The value of a global constant must be:
	 *  - a regular/real node (not a sentinel)
	 *  - at depth 0 (there are no surrounding abstractions)
	 *  - referenced (at least by its sentinel, possibly by more)
	 */
	assert(n->variety != NODE_SENTINEL);
	assert(n->depth == 0);
	assert(n->nref > 0);
	return n;
}

static struct node_chain
flatten_term(const struct term *term, struct node *prev, unsigned depth);

/*
 * When flattening, we assemble a node chain, linking each node to its
 * predecessor.  After the chain is complete, we fix up successors and
 * bundle the doubly-linked list endpoints into a sentinel node.
 */
struct node *flatten_chain(const struct term *term, unsigned depth)
{
	struct node_chain chain = flatten_term(term, NULL, depth);
	assert(chain.next->depth == depth);
	assert(chain.prev->depth == depth);
	for (struct node *curr = chain.prev, *next = NULL;
	     curr != NULL; next = curr, curr = curr->prev)
		curr->next = next;
	return NodeSentinel(chain.next, chain.prev, depth);
}

/*
 * flatten_hoist is called multiple times when we're flattening a
 * term with nested subterms (e.g. an application, cell, or test).
 * The intent is suggested by the name: we lift *children* to *peers*
 * (which you could admittedly view as another kind of nesting, but
 * which are also linearized into the environment alongside the parent
 * term so we can visit every term at a given abstraction depth with
 * right/left traversals).  Once that is done we only need to "go
 * deeper" to enter into abstractions, tests, and other nodes with
 * unevaluated subexpressions.
 *
 * We eliminate nesting by returning a reference (to a bound variable,
 * constant, or explicit substitution); in the substitution case we
 * flatten the nested subterm and attach it to the linear environment
 * we're constructing (link it to 'prev').  We return the slot to write
 * into the parent node along with the current head of the environment,
 * 'prev'.
 */
static struct slot_and_prev {
	struct slot slot;
	struct node *prev;
} flatten_hoist(const struct term *term, struct node *prev, unsigned depth)
{
	switch (term->variety) {
	case TERM_CONSTANT:
		/*
		 * Reference to the global environment.  If opaque,
		 * we create a constant slot; otherwise we create an
		 * explicit subtitution, mimicing the way such a term
		 * would be referenced post-evaluation.  We don't set
		 * up backreferences (we can't mutate the global
		 * environment and don't include it in R-to-L eval)
		 * but we do bump its reference count to keep reference
		 * counting uniform.
		 */
		if (term->constant.binder->flags & BINDING_OPAQUE)
			return (struct slot_and_prev) {
				.slot.variety = SLOT_CONSTANT,
				.slot.index = term->constant.binder->index,
				.prev = prev,
			};
		for (struct node *k = constant_of(term); k->nref++; /* nada */)
			return (struct slot_and_prev) {
				.slot.variety = SLOT_SUBST,
				.slot.subst = k,
				.prev = prev,
			};
	case TERM_VAR:
		/*
		 * Since we only flatten top-level terms, bound variable
		 * indexes are bounded by the current abstraction depth.
		 * Outside any abstractions (depth == 0) we shouldn't
		 * encounter a bound variable at all.
		 */
		assert(term->var.up < depth);
		return (struct slot_and_prev) {
			.slot.variety = SLOT_BOUND,
			.slot.bv.up = term->var.up,
			.slot.bv.across = term->var.across,
			.prev = prev,
		};
	default: /* fall through... */;
	}

	/*
	 * Since flattened nodes only contain indirections via variables
	 * (handled above) and substitutions, any non-variable yields a
	 * separately-flattened substitution.  We confirm that the chain
	 * has extended 'prev' and that the new chain is unreferenced;
	 * callers will look for this to determine whether to establish
	 * a backreference.
	 */
	struct node_chain chain = flatten_term(term, prev, depth);
	assert(chain.next->nref == 0);
	assert(chain.prev != prev);
	return (struct slot_and_prev) {
		.slot.variety = SLOT_SUBST,
		.slot.subst = chain.next,
		.prev = chain.prev,
	};
}

/*
 * This is the aforementioned check to determine whether a backreference
 * from a freshly allocated node chain is necessary.
 */
static inline bool
is_fresh_subst(const struct slot slot)
{
	return slot.variety == SLOT_SUBST && slot.subst->nref == 0;
}

static struct node_chain
flatten_term(const struct term *term, struct node *prev, unsigned depth)
{
	struct node_chain retval;

	switch (term->variety) {
	case TERM_ABS:
	case TERM_FIX:
		assert(term->abs.nformals > 0);
		retval.next = retval.prev =
			NodeAbs(prev, depth,
				flatten_chain(term->abs.body, depth + 1),
				term->abs.nformals, term->abs.formals);
		break;
	case TERM_APP: {
		/*
		 * Crumbling an application involves replacing its non-
		 * variable components with explicit substitutions.
		 *
		 * When we do so, we set 'backref' in the referent to the
		 * referring slot.  Since non-star nodes generated by
		 * flattening always have a reference count of 1, there's
		 * a unique referrer for each referent; this property will
		 * be lost as additional references accumulate during
		 * R-to-L traversal, but after we pass a node in R-to-L
		 * traversal we're done using its backreference anyway.
		 * 
		 *       +------------+
		 *       |   +--------|-----------------+
		 *       |   |        |                 |
		 *       |   |        v                 v
		 * ... [^X (^Y)] ... [@X, backref] ... [@Y, backref]
		 *       ^   ^               |                 |
		 *       |   |               |                 |
		 *	 +---|---------------+                 |
		 *	     +---------------------------------+
		 *
		 * We also set 'nref' in the referenced node to 1 to
		 * reflect the single application node referencing it.
		 */
		retval.next = prev = NodeApp(prev, depth, term->app.nargs);
		struct slot_and_prev sap = { .prev = prev };
		for (size_t i = 0; i <= term->app.nargs; ++i) {
			sap = flatten_hoist(
				i == 0 ? term->app.fun : term->app.args[i-1],
				sap.prev, depth);
			if (is_fresh_subst(sap.slot)) {
				sap.slot.subst->nref = 1;
				sap.slot.subst->backref = &prev->slots[i];
			}
			prev->slots[i] = sap.slot;
		}
		retval.prev = sap.prev;
		break;
	}
	case TERM_CELL: {
		/*
		 * This is nearly identical to TERM_APP, but it's a little
		 * more uniform--we don't have to distinguish between the
		 * function and arguments when calling flatten_hoist().
		 */
		retval.next = prev = NodeCell(prev, depth, term->cell.nelts);
		struct slot_and_prev sap = { .prev = prev };
		for (size_t i = 0; i < term->cell.nelts; ++i) {
			sap = flatten_hoist(
				term->cell.elts[i], sap.prev, depth);
			if (is_fresh_subst(sap.slot)) {
				sap.slot.subst->nref = 1;
				sap.slot.subst->backref = &prev->slots[i];
			}
			prev->slots[i] = sap.slot;
		}
		retval.prev = sap.prev;
		break;
	}
	case TERM_CONSTANT:
		/*
		 * n.b. NodeSubst bumps substitutions's reference count.
		 */
		retval.next = retval.prev =
			term->constant.binder->flags & BINDING_OPAQUE ?
			NodeConstant(prev, depth, term->constant.binder->index):
			NodeSubst(prev, depth, constant_of(term));
		break;
	case TERM_LET: {
		/*
		 * Similar to the application case, but we also construct
		 * the let's body as an abstraction.  That body isn't
		 * connected to 'prev' since it's only used via this let.
		 */
		retval.next = prev = NodeLet(prev, depth, term->let.ndefs);
		assert(prev->nslots);
		prev->slots[0].variety = SLOT_BODY;
		prev->slots[0].subst = flatten_chain(term->let.body, depth + 1);
		struct slot_and_prev sap = { .prev = prev };
		for (size_t i = 1; i < term->let.ndefs; ++i) {
			sap = flatten_hoist(
				term->let.vals[i], sap.prev, depth);
			if (is_fresh_subst(sap.slot)) {
				sap.slot.subst->nref = 1;
				sap.slot.subst->backref = &prev->slots[i];
			}
			prev->slots[i] = sap.slot;
		}
		retval.prev = sap.prev;
		break;
	}
	case TERM_NUM:
		retval.next = retval.prev = NodeNum(prev, depth, term->num);
		break;
	case TERM_PRIM:
		retval.next = retval.prev = NodePrim(prev, depth, term->prim);
		break;
	case TERM_STRING:
		retval.next = retval.prev = NodeString(prev, depth, term->str);
		break;
	case TERM_SYMBOL:
		retval.next = retval.prev = NodeSymbol(prev, depth, term->sym);
		break;
	case TERM_TEST: {
		/*
		 * When we're done flattening the test and hooking up
		 * pointers, we want the predicate to be evaluated before
		 * the test node itself (i.e. to its right in the same
		 * singly-linked environment, so evaluted first in right-
		 * to-left traversal).  It's thus handled identically to
		 * the function and values within an application; there
		 * may be room to consolidate these cases further.
		 */
		retval.next = prev = NodeTest(prev, depth);
		assert(retval.next->nslots == 3);

		struct slot_and_prev sap =
			flatten_hoist(term->test.pred, prev, depth);
		if (is_fresh_subst(sap.slot)) {
			sap.slot.subst->nref = 1;
			sap.slot.subst->backref = &retval.next->slots[0];
		}
		retval.next->slots[SLOT_TEST_PRED] = sap.slot;
		retval.prev = sap.prev;

		/*
		 * The consequent and alternative are referenced by the
		 * test node but not evaluated until *after* the test.
		 * Based on the test's outcome, reduction will attach
		 * one or the other to the reduction environment in
		 * place of the test node.  Unlike an abstraction body,
		 * these bodies are at the same depth as the test itself
		 * since tests lack a name-binding construct.
		 */
		assert(term->test.ncsqs == 1);
		retval.next->slots[SLOT_TEST_CSQ].subst =
			flatten_chain(term->test.csqs[0], depth);
		assert(term->test.nalts == 1);
		retval.next->slots[SLOT_TEST_ALT].subst =
			flatten_chain(term->test.alts[0], depth);
		break;
	}
	case TERM_VAR:
		/*
		 * As above, bound variable indexes should be strictly
		 * less than the number of abstractions traversed so far.
		 */
		assert(term->var.up < depth);
		retval.next = retval.prev =
			NodeBoundVar(prev, depth, term->var.up,
				     term->var.across);
		break;
	default:
		panicf("Unhandled term variety %d\n", term->variety);
	}
	return retval;
}

struct node *flatten(const struct term *term)
{
	return flatten_chain(term, 0);
}
