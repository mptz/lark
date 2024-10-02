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

#include "crumble.h"
#include "node.h"
#include "term.h"

/*
 * In the SCAM machine definition, the variable '*' (a five-pointed
 * star in the original) is a special variable marking the start of a
 * linear environment of explicit substitution nodes.  In this code
 * base it corresponds to node->prev == NULL.
 */
#define STAR NULL

static struct node_chain
crumble_term(struct term *term, struct node *prev, unsigned depth);

/*
 * When crumbling, we assemble a node chain, linking each node to its
 * predecessor.  After the chain is complete, we fix up successors.
 */
struct node *crumble_chain(struct term *term, unsigned depth)
{
	struct node_chain chain = crumble_term(term, STAR, depth);
	assert(chain.next->depth == depth);
	assert(chain.prev->depth == depth);
	for (struct node *curr = chain.prev, *next = STAR;
	     curr != STAR; next = curr, curr = curr->prev)
		curr->next = next;
	return NodeSentinel(chain.next, chain.prev, depth);
}

/*
 * crumble_flatten is called multiple times when we're crumbling an
 * term with nested subterms (e.g. an application, pair, or test).
 * The intent is suggested by the name: we replace *children* with
 * *references* (which you could admittedly view as another kind
 * of nesting, but which are also linearized into the environment
 * alongside the parent term so we can visit every term at a given
 * abstraction depth with left/right traversals).  Once that is done
 * we only need to "go deeper" to enter into abstractions.
 *
 * We eliminate nesting by returning a variable (bound, free, or
 * subst); in the substitution case we crumble the nested subterm and
 * attach it to the linear environment we're constructing (link it to
 * 'prev').  We return the slot to write into the parent node along
 * with the current head of the environment, 'prev'.
 */
static struct slot_and_prev {
	struct slot slot;
	struct node *prev;
} crumble_flatten(struct term *term, struct node *prev, unsigned depth)
{
	switch (term->variety) {
	case TERM_BOUND_VAR:
		/*
		 * Since we only crumble top-level terms, bound variable
		 * indexes are bounded by the current abstraction depth.
		 * Outside any abstractions (depth == 0) we shouldn't
		 * encounter a bound variable at all.
		 */
		assert(term->bv.up < depth);
		return (struct slot_and_prev) {
			.slot.variety = SLOT_BOUND,
			.slot.bv.up = term->bv.up,
			.slot.bv.across = term->bv.across,
			.prev = prev,
		};
	case TERM_FREE_VAR:
		return (struct slot_and_prev) {
			.slot.variety = SLOT_FREE,
			.slot.term = term,
			.prev = prev,
		};
	default: {
		/*
		 * Since crumbled applications only contain indirections
		 * via variables and substitutions, any non-variable will
		 * be a separately-crumbled substitution.
		 */
		struct node_chain chain = crumble_term(term, prev, depth);
		return (struct slot_and_prev) {
			.slot.variety = SLOT_SUBST,
			.slot.subst = chain.next,
			.prev = chain.prev,
		};
	}
	}
}

static struct node_chain
crumble_term(struct term *term, struct node *prev, unsigned depth)
{
	struct node_chain retval;

	switch (term->variety) {
	case TERM_ABS:
	case TERM_FIX:
		assert(term->abs.nformals > 0);
		assert(term->abs.nbodies == 1);
		retval.next = retval.prev =
			NodeAbs(prev, depth,
				crumble_chain(term->abs.bodies[0], depth + 1),
				term->abs.nformals, term->abs.formals);
		/*
		 * We distinguish fixpoint abstractions from regular
		 * (nonrecursive) abstractions by the first parameter
		 * having variety SLOT_SELF rather than SLOT_PARAM.
		 */
		if (term->variety == TERM_FIX)
			retval.next->slots[1].variety = SLOT_SELF;
		break;
	case TERM_APP: {
		/*
		 * Crumbling an application involves replacing its non-
		 * variable components with explicit substitutions.
		 *
		 * When we do so, we set 'backref' in the referent to the
		 * referring slot.  Since non-star nodes generated by
		 * crumbling always have a reference count of 1, there's
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
			sap = crumble_flatten(
				i == 0 ? term->app.fun : term->app.args[i-1],
				sap.prev, depth);
			assert(slot_is_ref(sap.slot));
			if (sap.slot.variety == SLOT_SUBST) {
				assert(sap.slot.subst != prev);
				assert(sap.slot.subst->nref == 0);
				sap.slot.subst->nref = 1;
				sap.slot.subst->backref = &prev->slots[i];
			}
			prev->slots[i] = sap.slot;
		}
		retval.prev = sap.prev;
		break;
	}
	case TERM_BOUND_VAR:
		/*
		 * As above, bound variable indexes should be strictly
		 * less than the number of abstractions traversed so far.
		 */
		assert(term->bv.up < depth);
		retval.next = retval.prev =
			NodeBoundVar(prev, depth, term->bv.up, term->bv.across);
		break;
	case TERM_FREE_VAR:
		retval.next = retval.prev = NodeFreeVar(prev, depth, term);
		break;
	case TERM_NIL:
		retval.next = retval.prev = NodeCell(prev, depth, 0);
		break;
	case TERM_NUM:
		retval.next = retval.prev = NodeNum(prev, depth, term->num);
		break;
	case TERM_PAIR: {
		/*
		 * This is similar to the TERM_APP scenario--it may be
		 * possible to coalesce some of this, especially if we
		 * allow n-ary cells in forms & terms.
		 */
		retval.next = prev = NodeCell(prev, depth, 2);
		struct slot_and_prev sap;

		sap = crumble_flatten(term->pair.car, prev, depth);
		assert(slot_is_ref(sap.slot));
		if (sap.slot.variety == SLOT_SUBST) {
			assert(sap.slot.subst != prev);
			assert(sap.slot.subst->nref == 0);
			sap.slot.subst->nref = 1;
			sap.slot.subst->backref = &retval.next->slots[0];
		}
		retval.next->slots[0] = sap.slot;

		sap = crumble_flatten(term->pair.cdr, sap.prev, depth);
		assert(slot_is_ref(sap.slot));
		if (sap.slot.variety == SLOT_SUBST) {
			assert(sap.slot.subst != prev);
			assert(sap.slot.subst->nref == 0);
			sap.slot.subst->nref = 1;
			sap.slot.subst->backref = &retval.next->slots[1];
		}
		retval.next->slots[1] = sap.slot;

		retval.prev = sap.prev;
		break;
	}
	case TERM_PRIM:
		retval.next = retval.prev = NodePrim(prev, depth, term->prim);
		break;
	case TERM_TEST: {
		/*
		 * When we're done crumbling the test and hooking up
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
			crumble_flatten(term->test.pred, prev, depth);
		assert(slot_is_ref(sap.slot));
		if (sap.slot.variety == SLOT_SUBST) {
			assert(sap.slot.subst != prev);
			assert(sap.slot.subst->nref == 0);
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
			crumble_chain(term->test.csqs[0], depth);
		assert(term->test.nalts == 1);
		retval.next->slots[SLOT_TEST_ALT].subst =
			crumble_chain(term->test.alts[0], depth);
		break;
	}
	default:
		panicf("Unhandled term variety %d\n", term->variety);
	}
	return retval;
}

struct node *crumble(struct term *term)
{
	return crumble_chain(term, 0);
}
