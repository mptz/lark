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
 * Additionally sanity-check depths.
 */
static void sanity_check_l(const struct node *node, unsigned depth)
{
	for (/* nada */; node; node = node->prev) {

		/* double-check depths and relative depths */
		assert(node->depth >= 0);
		assert(node->depth == depth);
		if (node_is_abs(node) && node_abs_body(node) &&
		    node->depth + 1 != node_abs_body(node)->depth)
			panicf("Depth mismatch between abs @%s and body @%s\n",
			       memloc(node), memloc(node_abs_body(node)));

		/* missed-redex check */
		const struct slot *slot = &node->slots[0];
		if (node_is_app(node) && slot->variety == SLOT_SUBST) {
			const struct node *lhs = node_chase_lhs(slot->subst);
			if (node_is_abs(lhs))
				panicf("Missed beta-redex @%s\n", memloc(node));
		}

		/* XXX need separate missed-primitive check? */

		/* missed-test check */
		if (node->variety == NODE_TEST &&
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
		if (node_is_abs(node) &&
		    node->depth + 1 != node_abs_body(node)->depth)
			panicf("Depth mismatch between abs @%s and body @%s\n",
			       memloc(node), memloc(node_abs_body(node)));

		/* uncollected garbage? */
		if (node->nref == 0 && node->prev != NULL)
			panicf("Found uncollected garbage @%s\n", memloc(node));
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
 * Reduction proceeds right-to-left then left-to-right.  Each pass has
 * both a primary and a secondary function:
 *
 * Right-to-left: 1) beta reduction; 2) disintermediating renames.
 * Left-to-right: 1) reducing under abstractions; 2) garbage collection.
 *
 * Descent into an abstraction is a recursive traversal, i.e. we echo
 * right-to-left then left-to-right traversals on the abstraction body.
 */
struct node *reduce(struct node *headl)
{
	struct node *headr = NULL, *outer = NULL,	/* reduction state */
		    *x, *y;				/* temporaries */
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
	 * Verify some invariants: before we evaluate nodes in R-to-L,
	 * they are not shared and have backrefs (parent references)
	 * if applicable; our local depth is also correctly calibrated.
	 */
	assert(headl->nref == 1 || !headl->prev);
	assert(!headl->prev || headl->backref);
	assert(headl->depth == depth);

	/*
	 * For many scenarios we simply move to the left without acting.
	 * This section is a sequence of tests to determine if we can
	 * take any reduction action at all.
	 */
	if (headl->variety == NODE_TEST)
		goto rule_test;

	/*
	 * For us to have anything to do, the 0th slot in the head node
	 * must be an explicit substitution.  When the head node is unary,
	 * a substitution might be a name alias.  When the head term has
	 * multiple slots (i.e. an application), it can only be a beta-redex
	 * if the 0th slot is a substitution (a known term) rather than a
	 * free or bound variable (an unknown term).
	 */
	assert(headl->nslots);
	if (headl->slots[0].variety != SLOT_SUBST)
		goto rule_move_left;		

	if (headl->nslots <= 1) {	/* not an app? */
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
	 * sure it's a redex.  The crumbling transformation flattens
	 * expressions, so we can't have an abstraction or application
	 * nested inside this application.  We have only variables to
	 * deal with, whether they be original variables of the lambda
	 * term or node variables (explicit substitutions) introduced by
	 * crumbling or evaluation.
	 */

	/*
	 * In order to have a primitive-redex we must have a substitution
	 * referencing a primitive in function position.  We know we have
	 * a substitution in function position, so check for the primitive.
	 */
	if (node_is_prim(headl->slots[0].subst))
		goto rule_prim;

	/*
	 * Similarly, in order to have a beta-redex we must have a
	 * substitution referencing an abstraction in function position.
	 */
	if (node_is_abs(headl->slots[0].subst))
		goto rule_beta;

	/* fall through to rule_move_left... */

rule_move_left:
	/*
	 * If the current node is not a redex and we can't do an
	 * administrative renaming, the default rule simply moves
	 * the pointer to the left.
	 */
	if (EVAL_STATS) the_eval_stats.rule_move_left++;
	x = headl->prev;
	headl->prev = headr;
	headr = headl;
	headl = x;
	goto eval_rl;

rule_beta:
	if (EVAL_STATS) the_eval_stats.rule_beta++;
	assert(headl->slots[0].variety == SLOT_SUBST);
	x = headl->slots[0].subst;
	assert(headl->depth >= x->depth);
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
	if (headl->nslots != x->nslots)
		panic("Arity mismatch in beta-reduction!\n");

	/*
	 * First traverse and preprocess the application's arguments:
	 * -- Look for self-application, which prevents destructive
	 *    beta-evaluation even when x (the function) lacks refs.
	 * -- Create new nodes as needed (for inert terms).
	 * We start traversing at 1 since the function is in slot 0.
	 *
	 * We use the temporary 'y' from here to beta-reduction to
	 * track self-reference.
	 */
	y = NULL;
	for (size_t i = 1; i < headl->nslots; ++i) {
		struct slot slot = headl->slots[i];
		assert(slot_is_ref(slot));

		/*
		 * If the argument is a value (atom or abstraction), we
		 * substitute its existing node in beta-reduction.  We
		 * should consider merging this with the don't-alloc case
		 * below, if it holds up under scrutiny, since the handling
		 * is the same.
		 */
		if (slot.variety == SLOT_SUBST && slot.subst->isvalue) {
			/*
			 * In the case of self-application this might be
			 * decrementing the same node's reference a second
			 * time; this is OK since it should have acquired
			 * two references from the self-application.
			 */
			slot.subst->nref--;
			assert(slot.subst->nref >= 0);
			/*
			 * Since abstractions are values, self-application
			 * can only happen here, not in the inert case below.
			 */
			if (slot.subst == x)
				y = x;
			continue;
		}

		/*
		 * If the argument is not a value, we may need to create
		 * a new node for it.  NOTE: in contrast to the SCAM
		 * abstract machine (both the abstract specification and
		 * the reference implementation), we don't allocate a new
		 * node when an argument slot already contains an explicit
		 * substitution (SLOT_SUBST)--we just reuse the existing
		 * substitution.  New explicit substitutions are only
		 * allocated for bound & free variables, which we never
		 * directly substitute in beta-reduction.
		 *
		 * This change may need to be reverted if I discover it
		 * causes issues.  One way to build confidence is to add
		 * additional invariants.  The evaluation order of the
		 * SCAM abstract machine allows many helpful invariants,
		 * and we know these non-value terms (being inert) can't
		 * create a beta-redex via substitution.  But even that
		 * difference may not be significant since we handle them
		 * just as we handle abstractions above--the key appears
		 * to be that we substitute first, evaluate second, and
		 * never re-substitute in already-evaluated terms.
		 *
		 * Adding an XXX as a reminder to merge this case with
		 * the above eventually.
		 */
		if (slot.variety == SLOT_SUBST) {
			assert(!slot.subst->isvalue);
			slot.subst->nref--;
			assert(slot.subst->nref >= 0);
			assert(slot.subst != x);    /* self-ref impossible */
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
		headl->slots[i].subst = node;
		headl->slots[i].variety = SLOT_SUBST;
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
	if (x->nref == 0 && !y /* no self-reference */) {
		if (EVAL_STATS) the_eval_stats.quick_beta_move++;
		y = headl;	/* save a redex reference before reducing */
		headl = beta_nocopy(headl, node_take_body(x), depth,
				    headl->depth - x->depth);
	} else {
		y = headl;	/* save a redex reference before reducing */
		headl = beta_reduce(headl, node_abs_body(x), depth,
				    headl->depth - x->depth);
	}

	/*
	 * Now 'y', not 'headl', points to the redex.
	 */

	/*
	 * Link arguments to 'headr' (previously-evaluated environment).
 	 * If an argument is unreferenced after beta-reduction, however,
	 * we can immediately free it (in whole or in part, depending on
	 * circumstances) rather than wait for L-to-R garbage collection.
	 */
	for (size_t i = 1; i < y->nslots; ++i) {
		assert(y->slots[i].variety == SLOT_SUBST);
		struct node *arg = y->slots[i].subst;
		if (arg->isvalue) {
			/*
			 * Values are already present in the environment;
			 * we didn't allocate new nodes for them and don't
			 * need to link them.
			 */
			if (arg->nref || !node_is_abs(arg))
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
			node_free_body(arg);
		} else if (arg->isfresh) {
			arg->isfresh = false;
			if (arg->nref) {
				arg->prev = headr;
				headr = arg;
				continue;
			}
			/*
			 * As above, an unreferenced node can be freed.
			 */
			if (EVAL_STATS) the_eval_stats.quick_inert_unref++;
			node_free(arg);
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
	 * In contrast to beta-reduction, primitive reduction never
	 * yields a redex, so we don't have to leave it in 'headl' for
	 * re-evaluation.  Either the current head is irreducible, in
	 * which case there's no point in taking another crack at it,
	 * or it reduces to e.g. a number or other atom (not a redex).
	 */
	y = headl;
	headl = prim_reduce(headl, depth);

	/*
	 * Depending on the primitive, the 'redex' may not have really
	 * been a redex after all... for example, attempting to sum a
	 * bunch of bound variables cannot be simplified.  When
	 * prim_reduce returns the original redex, we replicate the
	 * logic of rule_move_left (carry on without reducing).  When
	 * prim_reduce returns a *new* redex, the only difference is
	 * we must free the old one.
	 */
	x = y->prev;
	if (headl != y) {
		if (EVAL_STATS) the_eval_stats.rule_prim_redex++;
		assert(y->nref == 0);
		node_free(y);
	} else
		if (EVAL_STATS) the_eval_stats.rule_prim_irred++;
	headl->prev = headr;
	headr = headl;
	headl = x;
	goto eval_rl;

rule_rename:
	/*
	 * Note that backreferences point not to nodes, but to slots
	 * within nodes, as depicted in this diagram.
	 *
	 * Before:
	 *                (headl)
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
	 *           (headl)
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
	assert(headl->slots[0].variety == SLOT_SUBST);
	assert(headl->prev);
	assert(headl->backref);
	assert(headl->backref->subst == headl);
	x = headl->slots[0].subst;
	x->backref = headl->backref;
	x->backref->subst = x;
	y = headl;
	y->nref--;
	headl = headl->prev;
	assert(y->nref == 0);
	node_free(y);
	goto eval_rl;

rule_test:
	if (EVAL_STATS) the_eval_stats.rule_test++;
	assert(headl->variety == NODE_TEST);
	assert(headl->nslots == 5);
	assert(headl->slots[0].variety == SLOT_SUBST &&
	       headl->slots[1].variety == SLOT_SUBST &&
	       headl->slots[2].variety == SLOT_SUBST &&
	       headl->slots[3].variety == SLOT_SUBST &&
	       headl->slots[4].variety == SLOT_SUBST);

	/*
	 * To reduce a test, the predicate must be a number.  If the
	 * number is nonzero, replace the test with the consequent;
	 * otherwise replace the test with the alternative.
	 */
	x = headl->slots[0].subst;	/* x is predicate */
	if (x->nslots != 1 || x->slots[0].variety != SLOT_NUM)
		goto rule_move_left;
	x->nref--;			/* predicate is consumed */
	assert(x->nref == 0);		/* no other references possible yet */
	if (x->slots[0].num) {
		/* consequent branch */
		x = headl->slots[1].subst;	/* consequent left end */
		y = headl->slots[2].subst;	/* consequent right end */
		node_free_env(headl->slots[4].subst);	/* free alternative */
	} else {
		/* alternative branch */
		x = headl->slots[3].subst;	/* alternative left end */
		y = headl->slots[4].subst;	/* alternative right end */
		node_free_env(headl->slots[2].subst);	/* free consequent */
	}
	/*
	 * We can't free the predicate here since it's linked from headr.
	 * It may be *guaranteed* to be headr in which case it should be
	 * possible to free.  But for now we leave it to be collected later.
	 */
	/* node_free(headl->slots[0].subst); */	/* free predicate */

	/*
	 * Connect the left end of the chosen branch to the rest of the
	 * environment (headl->prev) as well as update the parent pointer
	 * to headl (if it exists) to point to the branch left end.
	 */
	assert(x->nref == 0);
	x->prev = headl->prev;
	x->backref = headl->backref;
	if (x->backref) {
		x->backref->subst = x;
		x->nref++;
	}

	/*
	 * Now we can update 'headl' to 'y', the right end of the chosen
	 * branch, and free the test node itself; predicate and non-chosen
	 * branch have already been freed above.
	 */
	x = headl;
	if (x->prev) {
		x->nref--;
		assert(x->nref == 0);
	}
	headl = y;
	node_free_shallow(x);			/* free test node */
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
	if (node_is_abs(headr))
		goto rule_enter_abs;
	goto rule_move_right;

rule_move_right:
	/*
	 * Move right without taking any other action.
	 */
	if (EVAL_STATS) the_eval_stats.rule_move_right++;
	x = headr->prev;
	headr->prev = headl;
	headl = headr;
	headr = x;
	goto eval_lr;

rule_enter_abs:
	/*
	 * Enter into an abstraction.  We only do this for abstractions
	 * which are referenced by other terms; otherwise we gc them.
	 * This avoids useless reduction work.
	 */
	if (EVAL_STATS) the_eval_stats.rule_enter_abs++;
	assert(!headl || headr->nref);
	assert(node_is_abs(headr));
	assert(headr->nslots >= 2);
	assert(headr->slots[1].variety == SLOT_PARAM ||
	       headr->slots[1].variety == SLOT_SELF);

	x = node_abs_body(headr);
	headr->slots[SLOT_ABS_BODY].subst = headl;
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
	 * right since we're done handling this node.  This step thus
	 * combines the pop and an equivalent of rule_move_right.
	 */
	if (EVAL_STATS) the_eval_stats.rule_exit_abs++;
	assert(headl != NULL);
	assert(headr == NULL);
	assert(outer != NULL);
	assert(node_is_abs(outer));
	x = outer->slots[SLOT_ABS_BODY].subst;	/* old headl */
	outer->slots[SLOT_ABS_BODY].subst = headl;
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
	x = headr->prev;
	node_free(headr);
	headr = x;
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
