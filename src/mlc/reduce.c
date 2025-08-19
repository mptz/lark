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
#include <string.h>

#include <util/message.h>

#include "beta.h"
#include "heap.h"
#include "memloc.h"
#include "mlc.h"
#include "node.h"
#include "prim.h"
#include "reduce.h"

#define EVAL_STATS 1
#define SANITY_CHECK 1
#define TRACE_EVAL 0

struct eval_stats {
	unsigned long
		reduce_start, reduce_done,
		eval_rl, eval_lr,
		rule_beta, rule_rename, rule_test,
		rule_zeta,
		rule_prim,
		rule_move_left, rule_reverse, rule_move_right,
		rule_move_up, rule_collect,
		rule_enter_abs, rule_exit_abs,
		rule_enter_test, rule_exit_test,
		quick_inert_unref, quick_value_unref, quick_beta_move;
};

static struct eval_stats the_eval_stats;

static void gc(struct node *head, struct node *outer)
{
	if (!quiet_setting)
		fputs("==================== COLLECTING "
		      "GARBAGE ====================\n", stderr);
	do {
		assert(!done(head));
		for (head = head->next; !done(head); /* nada */) {
			if (head->nref)
				head = head->next;
			else {
				struct node *tmp = head->next;
				node_remove(head);
				node_deref(head);
				node_free(head);
				head = tmp;
			}
		}
		head = outer;
		if (head) outer = head->outer;
	} while (head);
	node_heap_calibrate();
	fflush(stdout);
	if (!quiet_setting) print_heap_stats();
}

void print_eval_stats(void)
{
	printf(
	"\t\t\tREDUCTION STATISTICS\n"
	"\t\t\t====================\n"
	"Steps:\t%12s %-10lu %12s %-10lu %12s %-10lu\n"		/* 1 */
	"Rules:\t%12s %-10lu %12s %-10lu %12s %-10lu\n"		/* 2 */
	      "\t%12s %-10lu %12s %-10lu\n"			/* 3 */
	      "\t%12s %-10lu %12s %-10lu %12s %-10lu\n"		/* 4 */
	      "\t%12s %-10lu %12s %-10lu\n"			/* 5 */
	      "\t%12s %-10lu %12s %-10lu\n"			/* 6 */
	      "\t%12s %-10lu %12s %-10lu\n"			/* 7 */
	"Quick:\t%12s %-10lu %12s %-10lu %12s %-10lu\n",	/* 8 */

	"reductions",	the_eval_stats.reduce_start,		/* 1 */
	/* not showing reduce_done but won't differ unless reducing */
	"eval_rl",	the_eval_stats.eval_rl,
	"eval_lr",	the_eval_stats.eval_lr,

	"beta",		the_eval_stats.rule_beta,		/* 2 */
	"rename",	the_eval_stats.rule_rename,
	"test",		the_eval_stats.rule_test,

	"zeta",		the_eval_stats.rule_zeta,		/* 3 */
	"prim",		the_eval_stats.rule_prim,

	"move_left",	the_eval_stats.rule_move_left,		/* 4 */
	"reverse",	the_eval_stats.rule_reverse,
	"move_right",	the_eval_stats.rule_move_right,

	"enter_abs",	the_eval_stats.rule_enter_abs,		/* 5 */
	"enter_test",	the_eval_stats.rule_enter_test,

	"exit_abs",	the_eval_stats.rule_exit_abs,		/* 6 */
	"exit_test",	the_eval_stats.rule_exit_test,

	"move_up",	the_eval_stats.rule_move_up,		/* 7 */
	"collect",	the_eval_stats.rule_collect,

	"inert_unref",	the_eval_stats.quick_inert_unref,	/* 8 */
	"value_unref",	the_eval_stats.quick_value_unref,
	"beta_move",	the_eval_stats.quick_beta_move);
}

void reset_eval_stats(void)
{
	memset(&the_eval_stats, 0, sizeof the_eval_stats);
}

/*
 * For left-to-right sanity checks, check two primary invariants:
 *
 * (1) no redexes (beta, test, ...)
 * (2) no values (abstractions, atomics, etc) hidden behind renames.
 *
 * Note that detection of redexes during reduction relies on a lack
 * of renaming chains, since we look to a fixed depth of 1 to preserve
 * O(1) operation.  Therefore a failure of #2 will likely lead to a
 * failure of #1.  For this reason we're probably safe not checking
 * every possible missed primitive redex--that logic would be very
 * messy to duplicate here as it involves checking for evaluated
 * arguments, not just an evaluated primitive operation.
 *
 * Additionally sanity-check list structure and depths.
 */
static void sanity_check_l(const struct node *node, unsigned depth)
{
	assert(node->variety == NODE_SENTINEL);
	for (node = node->next; !done(node); node = node->next) {

		/* doubly-linked structure invariants */
		assert(node->next->prev == node);
		assert(node->prev->next == node);

		/* double-check depths and relative depths */
		assert(node->depth >= 0);
		assert(node->depth == depth);
		if (node_is_binder(node) && node_binder_body(node) &&
		    node->depth + 1 != node_binder_body(node)->depth)
			panicf("Depth mismatch between @%s and body @%s\n",
			       memloc(node), memloc(node_binder_body(node)));

		/* missed-redex check */
		if (node->variety == NODE_LET)
			panicf("Missed let-redex @%s\n", memloc(node));
		const struct slot *slot = &node->slots[0];
		if (node->variety == NODE_APP && slot->variety == SLOT_SUBST)
			if (node_is_abs(node_chase_lhs(slot->node)))
				panicf("Missed beta-redex @%s\n", memloc(node));
		if (node->variety == NODE_TEST && slot->variety == SLOT_SUBST) {
			const struct node *lhs = node_chase_lhs(slot->node);
			if (lhs->variety == NODE_VAL &&
			    lhs->slots[0].variety == SLOT_NUM)
				panicf("Missed test redex @%s\n", memloc(node));
		}

		/* rename-chain terminating in value? */
		for (size_t i = 0; i < node->nslots; ++i)
			if (node->slots[i].variety == SLOT_SUBST &&
			    node_subst_depth(node->slots[i].node) > 0)
				panicf("Missed rename chain @%s[%zu]\n",
					memloc(node), i);
	}
}

/*
 * For right-to-left sanity checks (which we apply before reducing
 * and on reaching normal form), we check a reduction invariant:
 * we should no longer have nodes with reference count == 0.  We
 * additionally perform list-structure and depth sanity checks.
 *
 * After reducing, we could additionally check that the term is in
 * normal form (no beta-redexes either at the top level or within
 * abstractions), but that's not yet implemented.  It's less urgent
 * as sanity_check_l verifies no beta-redexes at the current level,
 * and we run that on reversing, including within abstractions.
 */
static void sanity_check_r(const struct node *node, unsigned depth)
{
	assert(node->variety == NODE_SENTINEL);
	for (node = node->prev; !done(node); node = node->prev) {

		/* doubly-linked structure invariants */
		assert(node->next->prev == node);
		assert(node->prev->next == node);

		/* double-check depths and relative depths */
		assert(node->depth >= 0);
		assert(node->depth == depth);
		if (node_is_binder(node) &&
		    node->depth + 1 != node_binder_body(node)->depth)
			panicf("Depth mismatch between @%s and body @%s\n",
			       memloc(node), memloc(node_binder_body(node)));

		/* uncollected garbage? */
		if (node->nref == 0)
			panicf("Found uncollected garbage @%s\n", memloc(node));
	}
}

enum eval_dir { RL, LR };

static void trace_eval(enum eval_dir dir, unsigned depth, struct node *head)
{
	struct node *lb = dir == RL ? head : head->prev,
		    *rb = dir == RL ? head->next : head;
	printf("eval_%s[+%u]: ", dir == RL ? "rl" : "lr", depth);
	node_print_until(lb);
	fputs(dir == RL ? " <=L=< " : " >=R=> ", stdout);
	node_print_after(rb);
	putchar('\n');
	fflush(stdout);
}

/*
 * Reduction proceeds right-to-left then left-to-right.  Each pass has
 * both a primary and a secondary function:
 *
 * Right-to-left: 1) simplify redexes; 2) disintermediating renames.
 * Left-to-right: 1) reducing under abstractions; 2) garbage collection.
 *
 * Descent into an abstraction is a recursive traversal, i.e. we echo
 * right-to-left then left-to-right traversals on the abstraction body.
 * Bodies of unevaluated tests are handled identically to abstractions.
 *
 * If we're performing an 'abstract reduction' (respecting abstractions)
 * we don't enter them so left-to-right traversal does garbage collection
 * only.
 */
struct node *reduce(struct node *head, enum reduction reduction)
{
	struct node *outer = NULL,	/* containing abstraction links */
		    *x, *y;		/* temporaries */
	unsigned depth = 0, ticks = 0;

	the_eval_stats.reduce_start++;
	/* fall through to eval_body... */

eval_body:
	if (head->variety != NODE_SENTINEL)
		panic("Can't reduce a non-sentinel node\n");
	if (SANITY_CHECK) sanity_check_r(head, depth);
	head = head->prev;
	/* fall through to eval_rl... */

eval_rl:
	if (EVAL_STATS) the_eval_stats.eval_rl++;
	if (TRACE_EVAL) trace_eval(RL, depth, head);
	if (done(head))
		goto rule_reverse;
	if ((++ticks & 0xFF) == 0 && the_heap_pressure > the_heap_threshold)
		gc(head, outer);

	/*
	 * Verify some invariants: before we evaluate nodes in R-to-L,
	 * they are not shared and have backrefs (parent references);
	 * our local depth is also correctly calibrated.
	 */
	assert(head->nref == 1);
	assert(head->backref);
	assert(head->depth == depth);

	/*
	 * Let expressions are always reducible.
	 */
	if (head->variety == NODE_LET)
		goto rule_zeta;

	/*
	 * For us to have anything to do, the 0th slot in the head node
	 * must be an explicit substitution.  A substitution in the 0th
	 * slot doesn't guarantee a redex, but the absence of one does
	 * guarantee a non-redex.
	 */
	if (!head->nslots || head->slots[0].variety != SLOT_SUBST)
		goto rule_move_left;		

	/*
	 * For many scenarios we simply move to the left without acting.
	 * This is true not only for obviously self-evaluating nodes but
	 * also for e.g. cells, whose contents have been flattened out
	 * and already evaluated, but whose structure is self-evaluating.
	 */
	switch (head->variety) {
	case NODE_APP:
		/*
		 * We're still not sure 'head' is a redex.  Check for a
		 * primitive or abstraction in function position.
		 */
		if (node_is_prim(head->slots[0].node))
			goto rule_prim;
		if (node_is_abs(head->slots[0].node))
			goto rule_beta;
		break;
	case NODE_CELL:
		break;
	case NODE_TEST:
		goto rule_test;
	case NODE_VAR:
		/*
		 * An explicit substitution VAR node encountered during
		 * R-to-L traversal is a name alias.  In such a situation
		 * we forward references to this node to its referent,
		 * avoiding rename chains that might cause missed redexes.
		 */
		goto rule_rename;
	default:
		panicf("Unhandled node variety %d\n", head->variety);
	}
	/* fall through to rule_move_left... */

rule_move_left:
	/*
	 * If the current node is not a redex and we can't do an
	 * administrative renaming, the default rule simply moves
	 * the reduction head to the left.
	 */
	if (EVAL_STATS) the_eval_stats.rule_move_left++;
	head = head->prev;
	goto eval_rl;

rule_zeta:
	/*
	 * For the generic do_subst below (shared with beta reduction):
	 *	'head' is the application node, where:
	 *		head's 0th slot is the abstraction (x below)
	 *		head's remaining slots are the arguments
	 *	'x' is the abstraction node, where:
	 *		x's 0th slot is the abstraction body
	 *		x's remaining slots are the formal parameters
	 * In a let expression these coincide... the 0th slot is the
	 * body of the let and the remaining elements are the arguments.
	 * Note that this leaves no room for named formal parameters,
	 * so any names won't be reconstructable by unflattening... but
	 * let expressions are always redexes so that is unimportant.
	 */
	if (EVAL_STATS) the_eval_stats.rule_zeta++;
	assert(head->variety == NODE_LET);
	assert(head->slots[0].variety == SLOT_BODY);
	x = head;
	goto do_subst;

rule_beta:
	if (EVAL_STATS) the_eval_stats.rule_beta++;
	assert(head->slots[0].variety == SLOT_SUBST);
	x = head->slots[0].node;
	assert(head->depth >= x->depth);
	assert(node_is_abs(x));
	assert(x->nref > 0);
	x->nref--;	/* since redex-root application is going away */
	/* fall through to do_subst... */

do_subst:
	/*
	 * Arity mismatches simply trigger a panic... once this abstract
	 * machine is handling previously type-checked terms in a typed
	 * lambda calculus, this won't arise so we don't have to build
	 * machinery to deal with it.
	 */ 
	if (head->nslots != x->nslots)
		panic("Arity mismatch in beta-reduction!\n");

	/*
	 * First traverse and preprocess the application's arguments:
	 *
	 * -- Create new nodes as needed.  Beta-reduction replaces bound
	 *    variable (SLOT_BOUND) and constant (SLOT_CONSTANT) slots
	 *    with explicit substitution (SLOT_SUBST) slots, which are
	 *    pointers to reduction-graph nodes.  Wrapping variable &
	 *    constant slots requires node allocation.
	 *
	 *    NOTE: in contrast to the SCAM abstract machine (both the
	 *    abstract specification and the reference implementation),
	 *    we don't allocate a new node when an argument slot already
	 *    contains an explicit substitution (SLOT_SUBST)--we just
	 *    reuse the existing substitution.  As described above, new
	 *    explicit substitutions are allocated only for bound & free
	 *    variables, which we never directly substitute in
	 *    beta-reduction.
	 *
	 *    This change may need to be reverted if I discover it causes
	 *    issues.  One way to build confidence is to add additional
	 *    invariants.  The evaluation order of the SCAM abstract
	 *    machine allows many helpful invariants; the key appears
	 *    to be that we substitute first, evaluate second, and never
	 *    re-substitute in already-evaluated terms.
	 *
	 * -- Look for self-application, which prevents destructive
	 *    beta-evaluation even when x (the function) lacks refs.
	 *
	 * We start traversing at 1 since the function is in slot 0.
	 *
	 * We use the temporary 'y' from here to beta-reduction to
	 * track self-reference.
	 */
	y = x->variety == NODE_FIX ? x : NULL;
	for (size_t i = 1; i < head->nslots; ++i) {
		struct slot slot = head->slots[i];
		assert(slot_is_ref(slot));
		if (slot.variety == SLOT_SUBST) {
			/*
			 * In the case of self-application this might be
			 * decrementing the same node's reference a second
			 * time; this is OK since it should have acquired
			 * two references from the self-application.
			 */
			slot.node->nref--;
			assert(slot.node->nref >= 0);
			/*
			 * Self-application check.
			 */
			if (slot.node == x)
				y = x;
			continue;
		}

		/*
		 * This is the only place in reduction where we allocate
		 * new nodes directly (as opposed to within beta-reduction).
		 * We meet our obligations:
		 *	- Set depth here (we leave prev NULL, however,
		 *	  since we only intend to link these to head
		 *	  if they pick up references from substitution).
		 *	- Don't set backref, which is OK as backref is
		 *	  only used during the renaming step of R-to-L
		 *	  traversal; though we are in R-to-L traversal
		 *	  now, we insert newly allocated nodes to the
		 *	  right of current position.
		 *	- Though reference count is 0 initially, it will
		 *	  be incremented by each substitution in
		 *	  beta_reduce() -- if any.
		 *
		 * We also mark the node as 'fresh' so that below, if
		 * acquires references in beta-reduction, we add to it
		 * to the environment/evaluation context via 'head'.
		 */
		assert(slot.variety == SLOT_BOUND ||
		       slot.variety == SLOT_CONSTANT);
		struct node *node = (slot.variety == SLOT_BOUND) ?
			NodeBoundVar(NULL, depth, slot.bv.up, slot.bv.across) :
			NodeConstant(NULL, depth, slot.index);
		node->isfresh = true;
		head->slots[i].node = node;
		head->slots[i].variety = SLOT_SUBST;
	}
				
	/*
	 * What it has all been leading up to... beta-reduction.
 	 *
	 * If x has no remaining references, it can never be applied
	 * beyond this point; in that scenario we don't need to copy
	 * (alpha-convert) its body because the body will just be
	 * garbage collected later without being used again.  Instead
	 * we remove its body (leaving x itself as a placeholder in
	 * the environment) and reduce that body directly.  We still
	 * have to traverse the entire body to perform variable
	 * substitution and depth adjustments.
	 *
	 * In the presence of self-application, via fixpoints or x
	 * being one of its ordinary arguments, this optimization is
	 * unsafe; the reference count has fallen to 0 for the moment
	 * but might increase as we substitute x for a bound variable
	 * in its own body.  In that case, copy instead.
	 *
	 * If head == x, we're substituting in a let expression, in
	 * which no self-reference is possible so we can always move
	 * rather than copy.
	 */
	assert(head->depth == depth);
	assert(head->depth >= x->depth);
	if (head == x || (x->nref == 0 && !y /* no self-reference */)) {
		if (EVAL_STATS) the_eval_stats.quick_beta_move++;
		y = head;	/* save a redex reference before reducing */
		head = beta_nocopy(head, node_take_body(x), depth,
				   head->depth - x->depth);
	} else {
		y = head;	/* save a redex reference before reducing */
		head = beta_reduce(head, node_abs_body(x), depth,
				   head->depth - x->depth);
	}

	/*
	 * Now 'y', not 'head', points to the redex.
	 */

	/*
	 * Link arguments to the right of 'head' (the previously-evaluated
	 * environment).  If an argument is unreferenced after
	 * beta-reduction, however, we can immediately free it (in whole
	 * or in part, depending on circumstances) rather than wait for
	 * L-to-R garbage collection.
	 */
	for (size_t i = 1; i < y->nslots; ++i) {
		assert(y->slots[i].variety == SLOT_SUBST);
		struct node *arg = y->slots[i].node;
		if (arg->isfresh) {
			/*
			 * arg is a node which we allocated above; we know
			 * it's not linked by head at this level or at a
			 * lower abstraction depth, so we can safely free
			 * it completely if it's unreferenced.
			 */
			arg->isfresh = false;
			if (arg->nref) {
				node_insert_after(arg, head);
			} else {
				if (EVAL_STATS)
					the_eval_stats.quick_inert_unref++;
				node_free(arg);
			}
		} else if (!arg->nref) {
			/*
			 * arg is a node which previously existed; we can't
			 * necessarily free the node itself, but we may be
			 * able to free sub-components of it.  Currently
			 * only implemented for abstractions.
			 *
			 * Why can't we simply remove and free it?  It may
			 * be referenced by 'head' at this or a lower
			 * abstraction depth.  We could free it if it's
			 * at this abstraction depth, but for now we play
			 * it safe.
			 */
			if (!node_is_abs(arg))
				continue;
			/*
		 	 * If an abstraction is unreferenced after beta-
			 * reduction, we can free its body right away
			 * rather than waiting for L-to-R garbage collection.
			 * We can't free the node itself since it's linked
			 * by head at this level or at a lower abstraction
			 * depth; that will have to wait for L-to-R gc.
			 * Freeing the body is usually a bigger win,
			 * however--possibly a *much* bigger win.
			 *
			 * We see this scenario with e.g.
			 *	(\a b. b) y	     <= Variable 'a' doesn't
			 *				appear in body, so
			 *				'y' has no references
			 *				from the reduced body.
			 */
			if (EVAL_STATS) the_eval_stats.quick_value_unref++;
			node_wipe_body(arg);
		}
	}

	/*
	 * Having disposed of all the argument substitution nodes, we
	 * can finally free the redex node itself.
	 */
	assert(y->nref == 0);
	node_free(y);
	goto eval_rl;

rule_prim:
	if (EVAL_STATS) the_eval_stats.rule_prim++;

	/*
	 * Primitive reduction handles connecting the result to the
	 * evaluation chain as well as freeing the original redex if
	 * necessary, making our job here easy.
	 */
	assert(head->nslots);
	assert(head->slots[0].variety == SLOT_SUBST);
	x = head->slots[0].node;
	assert(node_is_prim(x));
	assert(x->nslots == 1);
	assert(x->slots[0].prim->reduce);
	head = x->slots[0].prim->reduce(x->slots[0].prim->variety, head);
	goto eval_rl;

rule_rename:
	/*
	 * Note that backreferences point not to nodes, but to slots
	 * within nodes, as depicted in this diagram illustrating the
	 * pointer-snapping (disintermediating) nature of renames.
	 * Note that Z's backreference doesn't come into the picture
	 * since it's to the right of the reduction head, and Z's net
	 * reference count is unaffected.
	 *
	 * /Before/       head 
	 *                  | 
	 *        +---------+            +---------+
	 *        |         |            |         |
	 *        |         V            |         V
	 *  [@X subst] ... [@Y backref subst] ... [@Z <anything>]
	 *        ^               |
	 *        |               |
	 *        +---------------+
	 *
	 * /After/    head 
	 *              |
	 *        +---------+
	 *        |     |   |
	 *        |     V   V
	 *  [@X subst] ... [@Z <anything>]
	 *
	 *		[@Y subst] (disconnected & freed)
	 */
	if (EVAL_STATS) the_eval_stats.rule_rename++;
	assert(head->nslots == 1);
	assert(head->slots[0].variety == SLOT_SUBST);
	assert(head->backref);
	assert(head->backref->node == head);
	head->backref->node = head->slots[0].node;	/* snap ref */
	head->nref--;			/* since parent ref redirected */
	x = head, head = head->prev;	/* store head & move left */
	node_remove(x), node_free(x);	/* dispose of rename node */
	goto eval_rl;

rule_test:
	if (EVAL_STATS) the_eval_stats.rule_test++;
	assert(head->variety == NODE_TEST);
	assert(head->nslots == 3);
	assert(head->slots[SLOT_TEST_PRED].variety == SLOT_SUBST &&
	       head->slots[SLOT_TEST_CSQ].variety == SLOT_BODY &&
	       head->slots[SLOT_TEST_ALT].variety == SLOT_BODY);

	/*
	 * To reduce a test, the predicate must be a number.  If the
	 * number is nonzero, replace the test with the consequent;
	 * otherwise replace the test with the alternative.
	 */
	x = head->slots[SLOT_TEST_PRED].node;
	if (x->nslots != 1 || x->slots[0].variety != SLOT_NUM)
		goto rule_move_left;

	x->nref--;			/* predicate is consumed */
	if (x->slots[0].num) {
		/* consequent branch */
		x = head->slots[SLOT_TEST_CSQ].node->next;
		y = head->slots[SLOT_TEST_CSQ].node->prev;
		node_pinch(head->slots[SLOT_TEST_CSQ].node);
	} else {
		/* alternative branch */
		x = head->slots[SLOT_TEST_ALT].node->next;
		y = head->slots[SLOT_TEST_ALT].node->prev;
		node_pinch(head->slots[SLOT_TEST_ALT].node);
	}

	/*
	 * Connect the chosen subexpression (between 'x' and 'y') to the
	 * evaluation environment in place of 'head', the redex.
	 * Increment the reference count of 'x', the chosen branch's
	 * left end; the node_free below will decrement it and we want
	 * it to stay above 0.
	 */
	assert(head->backref);
	x->backref = head->backref;
	x->backref->node = x;
	assert(head->nref == 1);
	assert(x->nref == 1);
	head->nref--;
	x->nref++;
	x->prev = head->prev;
	x->prev->next = x;
	y->next = head->next;
	y->next->prev = y;

	/*
	 * Now we can update 'head' to 'y', the right end of the chosen
	 * branch, and free the test node itself.
	 */
	x = head, head = y;
	node_free(x);
	goto eval_rl;

rule_reverse:
	if (EVAL_STATS) the_eval_stats.rule_reverse++;
	if (SANITY_CHECK) sanity_check_l(head, depth);
	assert(head->variety == NODE_SENTINEL);
	head = head->next;
	/* fall through to eval_lr... */

eval_lr:
	if (EVAL_STATS) the_eval_stats.eval_lr++;
	if (TRACE_EVAL) trace_eval(LR, depth, head);
	if (done(head)) goto rule_move_up;
	if (!head->nref) goto rule_collect;
	if (reduction != REDUCTION_DEEP) goto rule_move_right;

	/*
	 * Left-to-right evaluation only performs reduction nodes which
	 * have bodies (unevaluated subexpressions).
	 */
	switch (head->variety) {
	case NODE_ABS: case NODE_FIX:	goto rule_enter_abs;
	case NODE_LET:			panic("Unevaluated NODE_LET\n");
	case NODE_TEST:			goto rule_enter_test;
	default:					/* fall through */;
	}
	/* fall through to rule_move_right... */

rule_move_right:
	/*
	 * Move right without taking any other action.
	 */
	if (EVAL_STATS) the_eval_stats.rule_move_right++;
	head = head->next;
	goto eval_lr;

rule_move_up:
	if (EVAL_STATS) the_eval_stats.rule_move_up++;
	if (!outer) goto done;
	switch (outer->variety) {
	case NODE_ABS:
	case NODE_FIX:
		goto rule_exit_abs;
	case NODE_TEST:
		if (outer->slots[SLOT_TEST_CSQ].node == head) {
			if (TRACE_EVAL)
				printf("move_up[+%u]: csq ==> alt @%s\n",
				       depth, memloc(outer));
			head = outer->slots[SLOT_TEST_ALT].node;
			goto eval_body;
		}
		goto rule_exit_test;
	default: panicf("Unhandled node variety %d\n", outer->variety);
	}

rule_collect:
	if (EVAL_STATS) the_eval_stats.rule_collect++;
	assert(!head->nref);
	x = head->next;
	node_remove(head);
	node_deref(head);
	node_free(head);
	head = x;
	goto eval_lr;

rule_enter_abs:
	/*
	 * Enter into an abstraction.  We only do this for abstractions
	 * which are referenced by other terms; otherwise we gc them.
	 * This avoids useless reduction work.  Although let expressions
	 * are structured differently from abstractions, they could be
	 * treated uniformly here--but we don't encounter them on left-
	 * to right reductions since they are always reducible.
	 */
	if (EVAL_STATS) the_eval_stats.rule_enter_abs++;
	if (TRACE_EVAL)
		printf("enter_abs[+%u]: vvv @%s\n", depth, memloc(head));
	assert(node_is_abs(head));
	assert(head->nref);

	head->outer = outer, outer = head;	/* push outer */
	head = node_abs_body(head);		/* load body sentinel */
	++depth;
	goto eval_body;

rule_exit_abs:
	/*
	 * Pop contexts to exit an abstraction body.  Note that instead
	 * of restoring head to the value saved in outer, we move right
	 * (to outer->next) since we're done reducing this node.
	 */
	if (EVAL_STATS) the_eval_stats.rule_exit_abs++;
	assert(done(head));
	assert(outer != NULL);
	assert(node_is_binder(outer));
	head = outer, outer = head->outer;	/* pop outer */
	head = head->next;			/* move right */
	assert(depth > 0);
	--depth;
	if (TRACE_EVAL)
		printf("exit_abs[+%u]: ^^^ @%s\n", depth, memloc(head->prev));
	goto eval_lr;

rule_enter_test:
	if (EVAL_STATS) the_eval_stats.rule_enter_test++;
	if (TRACE_EVAL)
		printf("enter_test[+%u]: vvv @%s\n", depth, memloc(head));
	assert(head->variety == NODE_TEST);
	assert(head->nref);

	head->outer = outer, outer = head;		/* push outer */
	head = head->slots[SLOT_TEST_CSQ].node;		/* load body sentinel */
	goto eval_body;

rule_exit_test:
	if (EVAL_STATS) the_eval_stats.rule_exit_test++;
	assert(done(head));
	assert(outer != NULL);
	assert(outer->variety == NODE_TEST);
	head = outer, outer = head->outer;	/* pop outer */
	head = head->next;			/* move right */
	if (TRACE_EVAL)
		printf("exit_test[+%u]: ^^^ @%s\n", depth, memloc(head->prev));
	goto eval_lr;

done:
	assert(done(head));
	assert(!outer);
	assert(depth == 0);
	if (SANITY_CHECK) sanity_check_r(head, depth);
	the_eval_stats.reduce_done++;
	return head;
}
