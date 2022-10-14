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
#include <stddef.h>
#include <stdio.h>

#include <util/message.h>

#include "beta.h"
#include "node.h"
#include "memloc.h"
#include "reduce.h"

#define EVAL_STATS 1
#define SANITY_CHECK 1
#define TRACE_EVAL 0

struct eval_stats {
	unsigned long
		reduce_start, reduce_done,
		eval_rl, eval_lr,
		rule_beta_value, rule_beta_inert, rule_rename,
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
	"Quick:\t%12s %-10lu %12s %-10lu %12s %-10lu\n",
	"reductions",	the_eval_stats.reduce_start,
	/* not showing reduce_done but won't differ unless reducing */
	"eval_rl",	the_eval_stats.eval_rl,
	"eval_lr",	the_eval_stats.eval_lr,
	"beta_value",	the_eval_stats.rule_beta_value,
	"beta_inert",	the_eval_stats.rule_beta_inert,
	"rename",	the_eval_stats.rule_rename,
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
 * (1) no beta redexes (applications whose functions are abstractions);
 * (2) no values (abstractions) hidden behind renames.
 *
 * Note that our detection of beta redexes relies on a lack of renaming
 * chains, since we look to a fixed depth of 1 to preserve O(1) operation.
 * Therefore a failure of #2 will likely lead to a failure of #1.
 *
 * Additionally sanity-check depths.
 */
static void sanity_check_l(const struct node *node, unsigned depth)
{
	for (/* nada */; node; node = node->prev) {

		/* double-check depths and relative depths */
		assert(node->depth >= 0);
		assert(node->depth == depth);
		if (node->bits == NODE_BITS_ABS && node_abs_body(node) &&
		    node->depth + 1 != node_abs_body(node)->depth)
			panicf("Depth mismatch between abs @%s and body @%s\n",
			       memloc(node), memloc(node_abs_body(node)));

		/* missed-redex check */
		if ((node->bits & NODE_MASK_APP) &&
		    (node->bits & NODE_LHS_SUBST)) {
			const struct node *lhs = node_chase(node->lhs.subst);
			if (lhs->bits == NODE_BITS_ABS)
				panicf("Found beta-redex @%s\n", memloc(node));
		}

		/* rename-chain terminating in ABS? (non-ABS is OK) */
		if (node_abs_depth(node) > 1)
			panicf("Found rename chain @%s\n", memloc(node));
	}
}

/*
 * For right-to-left sanity checks (which we apply before reducing
 * and on reaching normal form), we check a reduction invariant:
 * we should not have nodes with reference count == 0 with the
 * exception of '*' (the last ES reached).  We additionally perform
 * depth sanity checks.
 *
 * After reducing, we could additionally check that the term is in
 * normal form (no beta-redexes either at the top level or within
 * abstractions), but that's not yet implemented.  It's less urgent
 * as sanity_check_l verifies no beta-redexes at the current level,
 * and we run that on reaching '*' including within abstractions.
 */
static void sanity_check_r(const struct node *node, unsigned depth)
{
	for (/* nada */; node; node = node->prev) {

		/* double-check depths and relative depths */
		assert(node->depth >= 0);
		assert(node->depth == depth);
		if (node->bits == NODE_BITS_ABS &&
		    node->depth + 1 != node_abs_body(node)->depth)
			panicf("Depth mismatch between abs @%s and body @%s\n",
			       memloc(node), memloc(node_abs_body(node)));

		/* uncollected garbage? */
		if (node->nref == 0 && node->prev != NULL)
			panicf("Uncollected garbage @%s\n", memloc(node));
	}
}

enum eval_dir { RL, LR };

static void trace_eval(enum eval_dir dir, unsigned depth,
		       struct node *headl, struct node *headr)
{
	printf("eval_%s[+%u]: ", dir == RL ? "rl" : "lr", depth);
	node_print_rl(headl);
	fputs(dir == RL ? " <== " : " ==> ", stdout);
	node_print_lr(headr, headl == NULL);
	putchar('\n');
	fflush(stdout);
}

/*
 * Reduction proceeds left-to-right then right-to-left.  Each pass has
 * both a primary and a secondary function:
 *
 * Left-to-right: 1) beta reduction; 2) disintermediating renames.
 * Right-to-left: 1) reducing under abstractions; 2) garbage collection.
 *
 * Descent into an abstraction is a recursive traversal, i.e. we echo
 * left-to-right then right-to-level traversals on the abstraction body.
 */
struct node *reduce(struct node *headl)
{
	struct node *headr = NULL, *outer = NULL,
		    *prev, *x, *y;
	unsigned depth = 0;

	the_eval_stats.reduce_start++;
	if (SANITY_CHECK) sanity_check_r(headl, depth);
	/* fall through to eval_rl... */

eval_rl:
	if (EVAL_STATS) the_eval_stats.eval_rl++;
	if (TRACE_EVAL) trace_eval(RL, depth, headl, headr);
	/*
	 *                 headl    headr
	 *  <==R-to-L===     |        |
	 *                   v        v
	 *       +-----+  #=====#  +-----+  +-----+
	 * ... <-|-prev|<-|-prev|  |prev-|->|prev-|-> ...
	 *       +-----+  #=====#  +-----+  +-----+
	 *               *current*
	 */
	if (!headl)				/* done with R-to-L? */
		goto rule_reverse;		

	/*
	 * For most scenarios we simply move to the left without acting.
	 */
	if (!(headl->bits & NODE_LHS_SUBST))	/* neither subst nor app? */
		goto rule_move_left;		

	if (!(headl->bits & NODE_MASK_APP)) {	/* not an app? */
		/*
		 * A SUBST node encountered during R-to-L traversal is a
		 * name alias unless it's the leftmost (star) node.  In
		 * such a situation we forward references to this SUBST
		 * to its referent, avoiding renaming chains that might
		 * cause us to miss redexes.
		 */
		if (headl->prev)
			goto rule_rename;
		goto rule_move_left;
	}

	/*
	 * At this point we know 'headl' is an application, but we're not
	 * sure it's a beta-redex.  The crumbling transformation flattens
	 * expressions, so we can't have an abstraction or application
	 * nested inside this application.  We have only variables to
	 * deal with, whether they be original variables of the lambda
	 * term or node variables (substitutions) introduced by crumbling.
	 */

	/*
	 * In order to have a beta-redex we must have a substitution
	 * referencing an abstraction in function position.  We know
	 * we have a substitution in function position, so we check
	 * for an abstraction.
	 */
	if (((x = headl->lhs.subst)->bits != NODE_BITS_ABS))
		goto rule_move_left;

	/*
	 * Now we know 'headl' is a beta-redex.  We have two different
	 * reduction rules depending on whether the argument is a value
	 * (absent atomic types, an abstraction) or is "inert" (not a
	 * value).
	 */
	if ((headl->bits & NODE_RHS_SUBST) &&
	    ((y = headl->rhs.subst)->bits == NODE_BITS_ABS))
			goto rule_beta_value;
	goto rule_beta_inert;

rule_move_left:
	/*
	 * If the current node is not a redex and we can't do an
	 * administrative renaming, the default rule simply moves
	 * the pointer to the left.
	 */
	if (EVAL_STATS) the_eval_stats.rule_move_left++;
	prev = headl->prev;
	headl->prev = headr;
	headr = headl;
	headl = prev;
	goto eval_rl;

rule_beta_value:
	if (EVAL_STATS) the_eval_stats.rule_beta_value++;
	assert(headl->bits & NODE_LHS_SUBST);
	assert(headl->bits & NODE_RHS_SUBST);
	assert(headl->lhs.subst == x);
	assert(headl->rhs.subst == y);
	assert(x->bits == NODE_BITS_ABS);
	assert(y->bits == NODE_BITS_ABS);
	assert(x->nref > 0);
	assert(y->nref > 0);
	x->nref--, y->nref--;
	assert(headl->depth >= x->depth);

	/*
	 * If x has no remaining references, it can never be applied
	 * beyond this point; in that scenario we don't need to copy
	 * (alpha-convert) its body because the body will just be
	 * garbage collected later without being used again.  Instead
	 * we remove its body (leaving x itself as a placeholder in
	 * the environment) and reduce that body directly.  We still
	 * have to traverse the entire body to perform variable
	 * substitution and depth adjustments.
	 *
	 * The exception to this optimization is when x == y, i.e.
	 * self-application of x.  In that case the reference count
	 * has fallen to 0 for the moment, but might come back up as
	 * we substitute x for the free variable in the body of x
	 * (creating new references).  So we can't destroy x's body
	 * yet.
	 */
	if (x->nref == 0 && x != y) {
		if (EVAL_STATS) the_eval_stats.quick_beta_move++;
		headl = beta_nocopy(headl, node_take_body(x), y, depth,
				    headl->depth - x->depth);
	} else
		headl = beta_reduce(headl, node_abs_body(x), y, depth,
				    headl->depth - x->depth);

	/*
 	 * If y is unreferenced after beta-reduction, we can free its
	 * abstraction body right away rather than waiting for L-to-R
	 * garbage collection.  We can't free y itself since it's linked
	 * by headr at this level or at a lower abstraction depth; that
	 * will have to wait for L-to-R gc.  Freeing the body is usually
	 * a bigger win, however--possibly a *much* bigger win.
	 *
	 * We see this scenario with e.g.
	 *	(\a b. b) y	     <= Variable 'a' doesn't appear in
	 *				body, so 'y' has no references
	 *				from the reduced body.
	 */
	if (y->nref == 0) {
		if (EVAL_STATS) the_eval_stats.quick_value_unref++;
		node_free_body(y);
	}
	goto eval_rl;

rule_beta_inert:
	if (EVAL_STATS) the_eval_stats.rule_beta_inert++;
	assert(headl->bits & NODE_LHS_SUBST);
	assert(headl->bits & NODE_MASK_APP);
	assert(headl->lhs.subst == x);
	assert(x->bits == NODE_BITS_ABS);
	assert(x->nref > 0);
	x->nref--;

	/*
	 * This is the only place in reduction where we allocate a new
	 * node directly (as opposed to within beta-reduction).  We meet
	 * our obligations:
	 *	- Set depth and prev here.
	 *	- Don't set backref, which is OK as backref is only used
	 *	  during the renaming step of R-to-L traversal; we are
	 *	  in R-to-L traversal now and are inserting the newly
	 *	  allocated node to the right of current position.
	 *	- Though reference count is 0 initially, it will be
	 *	  incremented by each substitution of y in beta().
	 */
	y = headl->bits & NODE_RHS_BOUND ?
		NodeBoundVar(headr, depth, headl->rhs.index) :
	    headl->bits & NODE_RHS_FREE ?
		NodeFreeVar(headr, depth, headl->rhs.term) :
		NodeSubst(headr, depth, headl->rhs.subst);
	assert(headl->depth >= x->depth);
	headl = beta_reduce(headl, node_abs_body(x), y, depth,
			    headl->depth - x->depth);

	/*
 	 * If y is unreferenced after beta-reduction, we can immediately
	 * free it rather than waiting for L-to-R garbage collection.
	 * We haven't linked y to headr yet, so we simply free the node
	 * and leave headr unmodified.
	 */
	if (y->nref == 0) {
		if (EVAL_STATS) the_eval_stats.quick_inert_unref++;
		node_free_shallow(y);
	} else
		headr = y;
	goto eval_rl;

rule_rename:
	/*
	 * Before:
	 *                (headl)
	 *                   |
	 *        +---------+|   +---------+
                  |         ||   |         |
	 *        |         VV   |         V
	 *  [@X subst] ... [@Y subst] ... [@Z <anything>]
	 *   ^              ^ backref          backref
	 *   |              |    |                | 
	 *   +-------------------+                |
	 *                  |                     |
	 *                  +---------------------+
	 *
	 * After:
	 *        +------------------------+
                  |                        |
	 *        |                        V
	 *  [@X subst] ... [@Y subst] ... [@Z <anything>]
	 *   ^              (freed)            backref
	 *   |                                    | 
	 *   +------------------------------------+
	 */
	if (EVAL_STATS) the_eval_stats.rule_rename++;
	assert(headl->bits == NODE_LHS_SUBST);
	assert(headl->prev);
	assert(headl->backref);
	assert(headl->backref->subst == headl);
	x = headl->lhs.subst;
	x->backref = headl->backref;
	x->backref->subst = x;
	y = headl;
	y->nref--;
	headl = headl->prev;
	assert(y->nref == 0);
	node_free(y);
	goto eval_rl;

rule_reverse:
	if (EVAL_STATS) the_eval_stats.rule_reverse++;
	if (SANITY_CHECK) sanity_check_l(headr, depth);
	/* fall through to eval_lr... */

eval_lr:
	if (EVAL_STATS) the_eval_stats.eval_lr++;
	if (TRACE_EVAL) trace_eval(LR, depth, headl, headr);
	/*
	 *                 headl    headr
	 *  ===L-to-R==>     |        |
	 *                   v        v
	 *       +-----+  +-----+  #=====#  +-----+
	 * ... <-|-prev|<-|-prev|  |prev-|->|prev-|-> ...
	 *       +-----+  +-----+  #=====#  +-----+
	 *                        *current*
	 */
	if (!headr) {
		if (!outer)
			goto done;
		goto rule_exit_abs;
	}
	if (headl && headr->nref == 0)
		goto rule_collect;
	if (headr->bits == NODE_BITS_ABS)
		goto rule_enter_abs;
	goto rule_move_right;

rule_move_right:
	/*
	 * Move right without taking any other action.
	 */
	if (EVAL_STATS) the_eval_stats.rule_move_right++;
	prev = headr->prev;
	headr->prev = headl;
	headl = headr;
	headr = prev;
	goto eval_lr;

rule_enter_abs:
	/*
	 * Enter into an abstraction.  We only do this for abstractions
	 * which are referenced by other terms; otherwise we gc them.
	 * This avoids useless reduction work.
	 */
	if (EVAL_STATS) the_eval_stats.rule_enter_abs++;
	assert(!headl || headr->nref);
	assert(headr->bits == NODE_BITS_ABS);
	x = node_abs_body(headr);
	headr->rhs.subst = headl;
	headr->outer = outer;
	outer = headr;
	headl = x;
	headr = NULL;
	++depth;
	if (SANITY_CHECK) sanity_check_r(headl, depth);
	goto eval_rl;

rule_exit_abs:
	/*
	 * Pop contexts to exit an abstraction body.  At this point
	 * headl points to the (now-reduced) body of the abstraction
	 * we entered, and headr is NULL since we're at the right end.
	 *
	 * Note that instead of restoring outer's headl and outer to
	 * headl and headr (as they were when we saved them) we move
	 * right since we're done handling this ES.  This step thus
	 * combines the pop and an equivalent of rule_move_right.
	 */
	if (EVAL_STATS) the_eval_stats.rule_exit_abs++;
	assert(headl != NULL);
	assert(headr == NULL);
	assert(outer != NULL);
	assert(outer->bits == NODE_BITS_ABS);
	x = outer->rhs.subst;	/* old headl */
	outer->rhs.subst = headl;
	headl = outer;
	headr = outer->prev;
	headl->prev = x;
	outer = outer->outer;
	assert(depth > 0);
	--depth;
	goto eval_lr;

rule_collect:
	if (EVAL_STATS) the_eval_stats.rule_collect++;
	assert(headr->nref == 0);
	assert(headl != NULL);
	prev = headr->prev;
	node_free(headr);
	headr = prev;
	goto eval_lr;

done:
	assert(headl);
	assert(!headr);
	assert(!outer);
	assert(depth == 0);
	if (SANITY_CHECK) sanity_check_r(headl, depth);
	the_eval_stats.reduce_done++;
	return headl;
}
