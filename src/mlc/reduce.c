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
#include <stdio.h>

#include <util/message.h>

#include "beta.h"
#include "node.h"
#include "memloc.h"
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
		rule_prim, rule_prim_redex, rule_prim_irred,
		rule_move_left, rule_reverse, rule_move_right,
		rule_enter_abs, rule_exit_abs, rule_collect,
		quick_inert_unref, quick_value_unref, quick_beta_move;
};

static struct eval_stats the_eval_stats;

void print_eval_stats(void)
{
	printf(
	"\t\t\tREDUCTION STATISTICS\n"
	"\t\t\t====================\n"
	"Steps:\t%12s %-10lu %12s %-10lu %12s %-10lu\n"
	"Rules:\t%12s %-10lu %12s %-10lu %12s %-10lu\n"
	      "\t%12s %-10lu %12s %-10lu %12s %-10lu\n"
	      "\t%12s %-10lu %12s %-10lu %12s %-10lu\n"
	      "\t%12s %-10lu %12s %-10lu %12s %-10lu\n"
	"Quick:\t%12s %-10lu %12s %-10lu %12s %-10lu\n",
	"reductions",	the_eval_stats.reduce_start,
	/* not showing reduce_done but won't differ unless reducing */
	"eval_rl",	the_eval_stats.eval_rl,
	"eval_lr",	the_eval_stats.eval_lr,
	"beta",		the_eval_stats.rule_beta,
	"rename",	the_eval_stats.rule_rename,
	"test",		the_eval_stats.rule_test,
	"prim",		the_eval_stats.rule_prim,
	"prim_redex",	the_eval_stats.rule_prim_redex,
	"prim_irred",	the_eval_stats.rule_prim_irred,
	"move_left",	the_eval_stats.rule_move_left,
	"reverse",	the_eval_stats.rule_reverse,
	"move_right",	the_eval_stats.rule_move_right,
	"enter_abs",	the_eval_stats.rule_enter_abs,
	"exit_abs",	the_eval_stats.rule_exit_abs,
	"collect",	the_eval_stats.rule_collect,
	"inert_unref",	the_eval_stats.quick_inert_unref,
	"value_unref",	the_eval_stats.quick_value_unref,
	"beta_move",	the_eval_stats.quick_beta_move);
}

/*
 * For left-to-right sanity checks, check two primary invariants:
 *
 * (1) no redexes (beta, primitive, test, ...)
 * (2) no values (abstractions) hidden behind renames.
 *
 * Note that detection of redexes during reduction relies on a lack
 * of renaming chains, since we look to a fixed depth of 1 to preserve
 * O(1) operation.  Therefore a failure of #2 will likely lead to a
 * failure of #1.
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
		if (node_is_abs(node) && node_abs_body(node) &&
		    node->depth + 1 != node_abs_body(node)->depth)
			panicf("Depth mismatch between abs @%s and body @%s\n",
			       memloc(node), memloc(node_abs_body(node)));

		/* missed-redex check */
		const struct slot *slot = &node->slots[0];
		if (node->variety == NODE_APP && slot->variety == SLOT_SUBST) {
			const struct node *lhs = node_chase_lhs(slot->subst);
			if (node_is_abs(lhs))
				panicf("Missed beta-redex @%s\n", memloc(node));
		}

		/* XXX need separate missed-primitive check? */

		/* missed-test check */
		if (node->variety == NODE_TEST &&
		    node->slots[0].variety == SLOT_SUBST &&
		    node->slots[0].subst->nslots == 1 &&
		    node->slots[0].subst->slots[0].variety == SLOT_NUM)
			panicf("Missed test @%s\n", memloc(node));

		/* rename-chain terminating in ABS? (non-ABS is OK) */
		/* XXX does this need to check for primitive/test as well? */
		if (node_abs_depth(node) > 1)
			panicf("Missed rename chain @%s\n", memloc(node));
	}
}

/*
 * For right-to-left sanity checks (which we apply before reducing
 * and on reaching normal form), we check a reduction invariant:
 * we should no longer have nodes with reference count == 0.  We
 * additionally perform depth sanity checks.
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
		if (node_is_abs(node) &&
		    node->depth + 1 != node_abs_body(node)->depth)
			panicf("Depth mismatch between abs @%s and body @%s\n",
			       memloc(node), memloc(node_abs_body(node)));

		/* uncollected garbage? */
		if (node->nref == 0)
			panicf("Found uncollected garbage @%s\n", memloc(node));
	}
}

enum eval_dir { RL, LR };

static void trace_eval(enum eval_dir dir, unsigned depth, struct node *head)
{
	printf("eval_%s[+%u]: ", dir == RL ? "rl" : "lr", depth);
	if (dir == RL) {
		node_print_until(head);
		fputs(" <=< ", stdout);
		node_print_after(head->next);
	} else {
		node_print_until(head->prev);
		fputs(" >=> ", stdout);
		node_print_after(head);
	}
	putchar('\n');
	fflush(stdout);
}

/*
 * Reduction proceeds right-to-left then left-to-right.  Each pass has
 * both a primary and a secondary function:
 *
 * Right-to-left: 1) beta reduction; 2) disintermediating renames.
 * Left-to-right: 1) reducing under abstractions; 2) garbage collection.
 *
 * Descent into an abstraction is a recursive traversal, i.e. we echo
 * right-to-left then left-to-right traversals on the abstraction body.
 */
struct node *reduce(struct node *head)
{
	struct node *outer = NULL,	/* containing abstraction links */
		    *x, *y;		/* temporaries */
	unsigned depth = 0;

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
	/*
	 *                 headl    headr
	 *  <==R-to-L===     |        |
	 *                   v        v
	 *       +-----+  #=====#  +-----+  +-----+
	 * ... <-|-prev|<-|-prev|  |prev-|->|prev-|-> ...
	 *       +-----+  #=====#  +-----+  +-----+
	 *               *current*
	 */
	if (done(head))
		goto rule_reverse;

	/*
	 * Verify some invariants: before we evaluate nodes in R-to-L,
	 * they are not shared and have backrefs (parent references);
	 * our local depth is also correctly calibrated.
	 */
	assert(head->nref == 1);
	assert(head->backref);
	assert(head->depth == depth);

	/*
	 * For us to have anything to do, the 0th slot in the head node
	 * must be an explicit substitution.  When the head node is unary,
	 * a substitution might be a name alias.  When the head term has
	 * multiple slots (i.e. an application), it can only be a beta-redex
	 * if the 0th slot is a substitution (a known term) rather than a
	 * free or bound variable (an unknown term).  Or the term could be
	 * a test... but without a substitution in the 0th slot we know
	 * it's irreducible without variety-directed dispatch.
	 */
	if (!head->nslots || head->slots[0].variety != SLOT_SUBST)
		goto rule_move_left;		

	/*
	 * For many scenarios we simply move to the left without acting.
	 * This section is a sequence of tests to determine if we can
	 * take any reduction action at all.
	 */
	switch (head->variety) {
	case NODE_APP:
		/*
		 * We're still not sure 'head' is a redex.  Check for a
		 * primitive or abstraction in function position.
		 */
		if (node_is_prim(head->slots[0].subst))
			goto rule_prim;
		if (node_is_abs(head->slots[0].subst))
			goto rule_beta;
		break;
	case NODE_CELL:
		break;
	case NODE_TEST:
		goto rule_test;
	case NODE_VAR:
		/*
		 * A SUBST node encountered during R-to-L traversal is a
		 * name alias unless it's the leftmost node.  In such a
		 * situation we forward references to this SUBST to its
		 * its referent, avoiding renaming chains that might lead
		 * us to miss redexes.
		 *
		 * XXX revisit this logic... what would have to change
		 * to be able to rename even the leftmost?  Do we ever
		 * want to do so?
		 */
		if (head->prev->variety != NODE_SENTINEL)
			goto rule_rename;
		break;
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

rule_beta:
	if (EVAL_STATS) the_eval_stats.rule_beta++;
	assert(head->slots[0].variety == SLOT_SUBST);
	x = head->slots[0].subst;
	assert(head->depth >= x->depth);
	assert(node_is_abs(x));
	assert(x->nref > 0);
	x->nref--;	/* since redex-root application is going away */

#ifdef RECURSION_IMPLEMENTED
	/*
	 * Recursive case... still working this out.
	 */
	assert(x->nslots >= 2);		/* body + at least one parameter */
	if (x->slots[1].variety == SLOT_SELF) {
		


	} else {
		/* move below arity check for nonrecursive here? */
	}
#endif

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
	 *    variable slots (SLOT_BOUND) with explicit substitution
	 *    slots (SLOT_SUBST), which are pointers to reduction-graph
	 *    nodes.  Since application nodes can contain both bound and
	 *    free variables, we must wrap those in substitution nodes
	 *    prior to beta-reduction.
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
	y = NULL;
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
			slot.subst->nref--;
			assert(slot.subst->nref >= 0);
			/*
			 * Self-application check.
			 */
			if (slot.subst == x)
				y = x;
			continue;
		}

		/*
		 * This is the only place in reduction where we allocate
		 * new nodes directly (as opposed to within beta-reduction).
		 * We meet our obligations:
		 *	- Set depth here (we leave prev NULL, however,
		 *	  since we only intend to link these to headr
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
		 * to the environment/evaluation context via 'headr'.
		 */
		assert(slot.variety == SLOT_BOUND ||
		       slot.variety == SLOT_FREE);
		struct node *node = (slot.variety == SLOT_BOUND) ?
			NodeBoundVar(NULL, depth, slot.bv.up, slot.bv.across) :
			NodeFreeVar(NULL, depth, slot.term);
		node->isfresh = true;
		head->slots[i].subst = node;
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
	 * The exception to this optimization is when x is also an
	 * argument, i.e. self-application of x.  In that case the
	 * reference count has fallen to 0 for the moment, but might
	 * come back up as we substitute x for a bound variable in
	 * the body of x (creating new references).  So we can't
	 * destroy x's body yet.
	 */
	assert(head->depth == depth);
	assert(head->depth >= x->depth);
	if (x->nref == 0 && !y /* no self-reference */) {
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
		struct node *arg = y->slots[i].subst;
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
			 */
			if (!node_is_abs(arg))
				continue;
			/*
		 	 * If an abstraction is unreferenced after beta-
			 * reduction, we can free its body right away
			 * rather than waiting for L-to-R garbage collection.
			 * We can't free the node itself since it's linked
			 * by headr at this level or at a lower abstraction
			 * depth; that will have to wait for L-to-R gc.
			 * Freeing the body is usually a bigger win,
			 * however--possibly a *much* bigger win.
			 *
			 * We see this scenario with e.g.
			 *	(\a b. b) y	     <= Variable 'a' doesn't
			 *				appear in body, so
			 *				'y' has no references
			 *				from the reduced body.
			 *
			 * XXX If the node is equal to 'headr', we can free
			 * it completely (including its body, if an
			 * abstraction) and adjust 'headr' accordingly.
			 * XXX this case is unimplemented!
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
	 * Depending on the primitive, the 'redex' may not have really
	 * been a redex after all... for example, attempting to sum a
	 * pair of bound variables cannot be simplified.  Testing for
	 * a redex is primitive-specific; when the test fails we move
	 * left without reducing.
	 */
	if (!prim_reducible(head)) {
		if (EVAL_STATS) the_eval_stats.rule_prim_irred++;
		goto rule_move_left;
	}

	/*
	 * Primitive reduction handles connecting the result to the
	 * evaluation chain as well as freeing the original redex if
	 * necessary.
	 */
	if (EVAL_STATS) the_eval_stats.rule_prim_redex++;
	head = prim_reduce(head);
	goto eval_rl;

rule_rename:
	/*
	 * Note that backreferences point not to nodes, but to slots
	 * within nodes, as depicted in this diagram.
	 *
	 * Before:
	 *                (head)
	 *                   |
	 *        +---------+|   +---------+
         *        |         ||   |         |
	 *        |         VV   |         V
	 *  [@X subst] ... [@Y subst] ... [@Z <anything>]
	 *      ^              ^ backref       backref
	 *      |              |    |             | 
	 *      +-------------------+             |
	 *                     |                  |
	 *                     +------------------+
	 *
	 *           (head)
	 * After:       |
	 *        +------------------------+
         *        |     |                  |
	 *        |     V                  V
	 *  [@X subst] ... [@Y subst] ... [@Z <anything>]
	 *      ^           (freed)            backref
	 *      |                                 | 
	 *      +---------------------------------+
	 */
	if (EVAL_STATS) the_eval_stats.rule_rename++;
	assert(head->nslots == 1);
	assert(head->slots[0].variety == SLOT_SUBST);
	assert(head->backref);
	assert(head->backref->subst == head);
	x = head->slots[0].subst;	/* x is the target */
	x->backref = head->backref;	/* snap backwards ref */
	x->backref->subst = x;		/* snap forwards ref */
	y = head;			/* store head in temp */
	head = head->prev;		/* move left */
	node_remove(y);			/* dispose of rename... */
	y->nref--;
	assert(!y->nref);
	node_free(y);
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
	x = head->slots[SLOT_TEST_PRED].subst;
	if (x->nslots != 1 || x->slots[0].variety != SLOT_NUM)
		goto rule_move_left;

	x->nref--;			/* predicate is consumed */
	if (x->slots[0].num) {
		/* consequent branch */
		x = head->slots[SLOT_TEST_CSQ].subst->next;
		y = head->slots[SLOT_TEST_CSQ].subst->prev;
		node_pinch(head->slots[SLOT_TEST_CSQ].subst);
	} else {
		/* alternative branch */
		x = head->slots[SLOT_TEST_ALT].subst->next;
		y = head->slots[SLOT_TEST_ALT].subst->prev;
		node_pinch(head->slots[SLOT_TEST_ALT].subst);
	}

	/*
	 * Connect the chosen subexpression (between 'x' and 'y') to the
	 * evaluation environment in place of 'head', the redex.
	 */
	assert(head->backref);
	x->backref = head->backref;
	x->backref->subst = x;
	assert(head->nref == 1);
	assert(x->nref == 1);
	head->nref--;
	x->prev = head->prev;
	x->prev->next = x;
	y->next = head->next;
	y->next->prev = y;

	/*
	 * Now we can update 'headl' to 'y', the right end of the chosen
	 * branch, and free the test node itself.
	 */
	x = head;
	head = y;
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
	/*
	 *                 headl    headr
	 *  ===L-to-R==>     |        |
	 *                   v        v
	 *       +-----+  +-----+  #=====#  +-----+
	 * ... <-|-prev|<-|-prev|  |prev-|->|prev-|-> ...
	 *       +-----+  +-----+  #=====#  +-----+
	 *                        *current*
	 */
	if (done(head)) {
		if (!outer) goto done;
		goto rule_exit_abs;
	}
	if (!head->nref)
		goto rule_collect;
	if (node_is_abs(head))
		goto rule_enter_abs;
	/* fall through to rule_move_right... */

/* rule_move_right: */
	/*
	 * Move right without taking any other action.
	 */
	if (EVAL_STATS) the_eval_stats.rule_move_right++;
	head = head->next;
	goto eval_lr;

rule_enter_abs:
	/*
	 * Enter into an abstraction.  We only do this for abstractions
	 * which are referenced by other terms; otherwise we gc them.
	 * This avoids useless reduction work.
	 */
	if (EVAL_STATS) the_eval_stats.rule_enter_abs++;
	assert(node_is_abs(head));
	assert(head->nref);
	assert(head->nslots >= 2);
	assert(head->slots[1].variety == SLOT_PARAM ||
	       head->slots[1].variety == SLOT_SELF);

	head->outer = outer, outer = head;	/* push outer */
	head = node_abs_body(head);		/* load body sentinel */
	++depth;
	goto eval_body;

rule_exit_abs:
	/*
	 * Pop contexts to exit an abstraction body.  At this point
	 * headl points to the (now-reduced) body of the abstraction
	 * we entered, and headr is NULL since we're at the right end.
	 *
	 * Note that instead of restoring outer's headl and outer to
	 * headl and headr (as they were when we saved them) we move
	 * right since we're done handling this node.  This step thus
	 * combines the pop and an equivalent of rule_move_right.
	 */
	if (EVAL_STATS) the_eval_stats.rule_exit_abs++;
	assert(done(head));
	assert(outer != NULL);
	assert(node_is_abs(outer));
	head = outer, outer = head->outer;	/* pop outer */
	head = head->next;			/* move right */
	assert(depth > 0);
	--depth;
	goto eval_lr;

rule_collect:
	if (EVAL_STATS) the_eval_stats.rule_collect++;
	assert(!head->nref);
	x = head->next;
	node_remove(head);
	node_deref(head);
	node_free(head);
	head = x;
	goto eval_lr;

done:
	assert(done(head));
	assert(!outer);
	assert(depth == 0);
	if (SANITY_CHECK) sanity_check_r(head, depth);
	the_eval_stats.reduce_done++;
	return head;
}
