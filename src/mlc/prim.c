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

const char *prim_symbol(enum prim_variety variety)
{
	switch (variety) {
	case PRIM_ADD:	return "+";
	case PRIM_SUB:	return "-";
	case PRIM_MULT:	return "*";
	case PRIM_DIV:	return "/";
	case PRIM_EQ:	return "==";
	case PRIM_NE:	return "!=";
	case PRIM_LT:	return "<";
	case PRIM_LTE:	return "<=";
	case PRIM_GT:	return ">";
	case PRIM_GTE:	return ">=";
	default: panicf("Unhandled primitive variety %d\n", variety);
	}
}

struct node *prim_reduce(struct node *redex, int depth)
{
	assert(redex->nslots > 0);
	assert(redex->slots[0].variety == SLOT_SUBST);
	struct node *prim = redex->slots[0].subst;
	assert(prim->nslots > 0);
	assert(prim->slots[0].variety == SLOT_PRIM);

	/* XXX we don't need 'depth' as a parameter, right? */
	assert(redex->depth == depth);

	/*
	 * Unless there are two arguments, both substitutions referencing
	 * numbers, 'redex' is actually irreducible so simply return it.
	 */
	struct node *num1, *num2;
	if (!(	redex->nslots == 3 &&
		redex->slots[1].variety == SLOT_SUBST &&
		(num1 = redex->slots[1].subst)->nslots == 1 &&
		num1->slots[0].variety == SLOT_NUM &&
		redex->slots[2].variety == SLOT_SUBST &&
		(num2 = redex->slots[2].subst)->nslots == 1 &&
		num2->slots[0].variety == SLOT_NUM	))
		return redex;
	
	/*
	 * The redex was in fact reducible; we need to allocate a new
	 * node to take its place.  Decrement reference counts of all
	 * three redex components since we're using them up.
	 */
	double lhs = num1->slots[0].num, rhs = num2->slots[0].num, val;
	switch (prim->slots[0].prim) {
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
	default: panicf("Unhandled primitive variety %d\n",
			prim->slots[0].prim);
	}

	prim->nref--, num1->nref--, num2->nref--;
	struct node *num = NodeNum(NULL, depth, val);
	if (redex->prev) {
		assert(redex->nref == 1);
		redex->nref--, num->nref++;
		assert(redex->backref);
		num->backref = redex->backref;
		num->backref->subst = num;
	} else {
		assert(redex->nref == 0);
		assert(!redex->backref);
	}
	return num;
}
