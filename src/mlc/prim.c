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

#include "node.h"
#include "prim.h"

const char *prim_name(enum prim_variety variety)
{
	switch (variety) {
	case PRIM_ADD:		return "+";
	case PRIM_SUB:		return "-";
	case PRIM_MULT:		return "*";
	case PRIM_DIV:		return "/";
	case PRIM_EQ:		return "==";
	case PRIM_NE:		return "!=";
	case PRIM_LT:		return "<";
	case PRIM_LTE:		return "<=";
	case PRIM_GT:		return ">";
	case PRIM_GTE:		return ">=";
	case PRIM_CAR:		return "#0";
	case PRIM_CDR:		return "#1";
	case PRIM_ISNIL:	return "#is-nil";
	case PRIM_ISPAIR:	return "#is-pair";
	default: panicf("Unhandled primitive variety %d\n", variety);
	}
}

static struct node *
prim_reduce_binary(struct node *redex, enum prim_variety variety)
{
	/*
	 * The redex is confirmed reducible; we need to allocate a new
	 * node to take its place.  Decrement reference counts of all
	 * three redex components since we're using them up.
	 */
	struct node *num1 = redex->slots[1].subst,
		    *num2 = redex->slots[2].subst;
	double lhs = num1->slots[0].num,
	       rhs = num2->slots[0].num,
	       val;
	switch (variety) {
	case PRIM_ADD:	val = lhs + rhs; break;
	case PRIM_SUB:	val = lhs - rhs; break;
	case PRIM_MULT:	val = lhs * rhs; break;
	case PRIM_DIV:	val = lhs / rhs; break;
	case PRIM_EQ:	val = lhs == rhs; break;
	case PRIM_NE:	val = lhs != rhs; break;
	case PRIM_LT:	val = lhs <  rhs; break;
	case PRIM_LTE:	val = lhs <= rhs; break;
	case PRIM_GT:	val = lhs >  rhs; break;
	case PRIM_GTE:	val = lhs >= rhs; break;
	default: panicf("Unhandled primitive variety %d\n", variety);
	}

	struct node *num = NodeNum(redex->prev, redex->depth, val);

	/*
	 * Replace the redex with the freshly allocated number.
	 * XXX we could overwrite the redex as we do elsewhere.
	 */
	assert(redex->nref == 1);
	assert(redex->backref);
	num->backref = redex->backref;
	num->backref->subst = num;
	redex->nref--, num->nref++;
	node_replace(num, redex);
	assert(!redex->nref);
	node_deref(redex);
	node_free(redex);
	return num->prev;
}

static struct node *
prim_reduce_unary(struct node *redex, enum prim_variety variety)
{
	/*
	 * Example node interreferences for #is-pair (q)
	 *
	 * ... [@A ^R] ... [@R ^O ^Q] <-headl
	 *
	 * 	headr -> [@O #is-pair][@Q ... ]...
	 */
	struct node *op = redex->slots[0].subst,
		    *arg = redex->slots[1].subst;
	op->nref--;			/* deref operator (@O above) */
	assert(op->nref >= 0);
	arg->nref--;			/* deref argument (@Q above) */
	assert(arg->nref >= 0);

	switch (variety) {
	case PRIM_ISNIL:
		redex->slots[0].variety = SLOT_NUM;
		redex->slots[0].num =
			(arg->variety == NODE_CELL && arg->nslots == 0);
		break;
	case PRIM_ISPAIR:
		redex->slots[0].variety = SLOT_NUM;
		redex->slots[0].num =
			(arg->variety == NODE_CELL && arg->nslots == 2);
		break;
	default:
		panicf("Unhandled primitive variety %d\n", variety);
	}

	assert(redex->nref == 1);
	assert(redex->backref);
	assert(redex->variety == NODE_APP);
	assert(redex->nslots == 2);
	redex->variety = NODE_VAL;
	redex->nslots = 1;
	return redex;
}

static struct node *
prim_reduce_unary_list(struct node *redex, enum prim_variety variety)
{
	/*
	 * Example node interreferences for '#0' (car):
	 *
	 * ... [@A ^R] ... [@R ^O ^C] <-headl
	 *
	 * 	headr -> [@O #0][@C ^H ^T][@H ^X][@T ^Y] ...
	 *
	 * Some observations:
	 *
	 *   a) Our goal is to replace the node @R with one that contains
	 *      the single substitution ^H.  However, we can't simply use
	 *	the existing node @H as the result, since it's linked in
	 *	the environment.
	 *
	 *	The obvious approach is to instead allocate a fresh node
	 *	with one slot identical to slot #0 of @C.  That slot may
	 *	contain an explicit substitution or a free/bound variable.
	 *
	 *	The perhaps less-obvious approach, a performance hack, is
	 *	to repurpose node @R as the result by shrinking it to one
	 *	slot and replacing its 0th slot with the 0th slot of @C.
	 *	This is the approach we take.
	 *
	 *	Why is this safe?  A few points:
	 *
	 *	First, @R is already at 'headl' which means it has the
	 *	proper reference count and backreference.  Backreferences
	 *	only matter to the left of the current reduction site,
	 *	i.e. 'headl' and nodes linked from it.
	 *
	 *	Second, node_free() for both application nodes (like @R)
	 *	variable nodes does no iteration and ignores nslots,
	 *	making it safe to modify @R's variety and nslots.
	 *
	 *   b) Assuming we repurpose @R, we still have a few nodes to
	 *	dereference and possibly make available for collection:
	 *		1) @O, the operator;
	 *		2) @C, the cell we're extracting from;
	 *		3) @H, if slot #0 in @C is a substitution.
	 *		4) @T, if slot #1 in @C is a substitution.
	 *	Of course referencing @H from @R *creates* a reference
	 *	to @H, which cancels out the dereference in #3.  Taking
	 *	the creates this reference to @T instead.
	 */
	struct node *op = redex->slots[0].subst,
		    *arg = redex->slots[1].subst;
	op->nref--;			/* deref operator (@O above) */
	assert(op->nref >= 0);
	arg->nref--;			/* deref cell (@C above) */
	assert(arg->nref >= 0);

	struct slot car = arg->slots[0], cdr = arg->slots[1];
	assert(slot_is_ref(car) && slot_is_ref(cdr));
	switch (variety) {
	case PRIM_CAR:
		redex->slots[0] = car;
		if (car.variety == SLOT_SUBST) car.subst->nref++;
		if (cdr.variety == SLOT_SUBST) {
#ifdef XXX_REFERENCE_COUNTING_FIXED
			cdr.subst->nref--;	/* deref cdr (@T above) */
			assert(cdr.subst->nref >= 0);
#endif	/* XXX_REFERENCE_COUNTING_FIXED */
		}
		break;
	case PRIM_CDR:
		redex->slots[0] = cdr;
		if (cdr.variety == SLOT_SUBST) cdr.subst->nref++;
		if (car.variety == SLOT_SUBST) {
#ifdef XXX_REFERENCE_COUNTING_FIXED
			car.subst->nref--;	/* deref car (@H above) */
			assert(car.subst->nref >= 0);
#endif	/* XXX_REFERENCE_COUNTING_FIXED */
		}
		break;
	default:
		panicf("Unhandled primitive variety %d\n", variety);
	}

	assert(redex->nref == 1);
	assert(redex->backref);
	assert(redex->variety == NODE_APP);
	assert(redex->nslots == 2);
	redex->variety = NODE_VAR;
	redex->nslots = 1;
	return redex;
}

bool prim_reducible(struct node *redex)
{
	if (!redex->nslots || redex->slots[0].variety != SLOT_SUBST)
		return false;
	struct node *prim = redex->slots[0].subst;
	if (prim->nslots != 1 || prim->slots[0].variety != SLOT_PRIM)
		return false;
	enum prim_variety variety = prim->slots[0].prim;

	switch (variety) {
	case PRIM_ADD:
	case PRIM_SUB:
	case PRIM_MULT:
	case PRIM_DIV:
	case PRIM_EQ:
	case PRIM_NE:
	case PRIM_LT:
	case PRIM_LTE:
	case PRIM_GT:
	case PRIM_GTE: {
		struct node *num1, *num2;
		return	redex->nslots == 3 &&
			redex->slots[1].variety == SLOT_SUBST &&
			(num1 = redex->slots[1].subst)->nslots == 1 &&
			num1->slots[0].variety == SLOT_NUM &&
			redex->slots[2].variety == SLOT_SUBST &&
			(num2 = redex->slots[2].subst)->nslots == 1 &&
			num2->slots[0].variety == SLOT_NUM;
	}
	case PRIM_CAR:
	case PRIM_CDR: {
		struct node *arg;
		return	redex->nslots == 2 &&
			redex->slots[1].variety == SLOT_SUBST &&
			(arg = redex->slots[1].subst)->variety == NODE_CELL &&
			arg->nslots == 2;
	}
	case PRIM_ISNIL:
	case PRIM_ISPAIR:
		return	redex->nslots == 2 &&
			redex->slots[1].variety == SLOT_SUBST;
	default:
		panicf("Unhandled primitive variety %d\n", variety);
	}
}

struct node *prim_reduce(struct node *redex)
{
	assert(redex->nslots > 0);
	assert(redex->slots[0].variety == SLOT_SUBST);
	struct node *prim = redex->slots[0].subst;
	assert(prim->nslots > 0);
	assert(prim->slots[0].variety == SLOT_PRIM);
	enum prim_variety variety = prim->slots[0].prim;

	switch (variety) {
	case PRIM_ADD:
	case PRIM_SUB:
	case PRIM_MULT:
	case PRIM_DIV:
	case PRIM_EQ:
	case PRIM_NE:
	case PRIM_LT:
	case PRIM_LTE:
	case PRIM_GT:
	case PRIM_GTE:
		return prim_reduce_binary(redex, variety);
	case PRIM_CAR:
	case PRIM_CDR:
		return prim_reduce_unary_list(redex, variety);
	case PRIM_ISNIL:
	case PRIM_ISPAIR:
		return prim_reduce_unary(redex, variety);
	default:
		panicf("Unhandled primitive variety %d\n", variety);
	}
}
