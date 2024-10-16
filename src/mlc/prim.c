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
#include <math.h>
#include <stddef.h>

#include <util/memutil.h>
#include <util/message.h>

#include "node.h"
#include "prim.h"

enum prim_variety {
	PRIM_INVALID,

	/* arithmetic */
	PRIM_ADD, PRIM_SUB, PRIM_MULT, PRIM_DIV,

	/* equality and inequality */
	PRIM_EQ, PRIM_NE, PRIM_LT, PRIM_LTE, PRIM_GT, PRIM_GTE,

	/* Boolean logic */
	PRIM_AND, PRIM_OR, PRIM_XOR, PRIM_NOT,

	/* floating-point operations */
	PRIM_IS_INTEGRAL,

	/* string operations */
	PRIM_CONCAT,

	/* cell operations */
	PRIM_AT, PRIM_CELL, PRIM_FILL, PRIM_FIND, PRIM_FUSE,
	PRIM_IS_CELL, PRIM_NELEMS,

	/* list operations */
	PRIM_CAR, PRIM_CDR, PRIM_IS_NIL, PRIM_IS_PAIR,

	PRIM_UNDEFINED, PRIM_PANIC,

} __attribute__ ((packed));

/*
 * Rather than allocate a fresh node for the result of an operation, we
 * generally try to re-use (recycle) the redex's application node as the
 * result.  This obviously only works if the application node has more
 * slots than the result needs.  Application nodes have no substructures
 * which need to be separately freed, so simply overwriting the node's
 * variety and nslots works.
 */

static struct node *
prim_replace_redex(struct node *redex, struct node *val)
{
	assert(redex->nref == 1);
	assert(redex->backref);
	assert(redex->backref->subst == redex);
	assert(val->nref == 0);
	val->backref = redex->backref;
	val->backref->subst = val;
	redex->nref--, val->nref++;

	node_replace(val, redex);
	node_deref(redex);
	node_free(redex);
	return val;
}

/*
 * XXX we can probably merge these return helpers.
 */
static struct node *
prim_return_num(struct node *redex, double num)
{
	assert(redex->nref == 1);
	assert(redex->backref);
	assert(redex->variety == NODE_APP);
	assert(redex->nslots);
	node_recycle(redex);
	redex->slots[0].variety = SLOT_NUM;
	redex->slots[0].num = num;
	redex->variety = NODE_VAL;
	redex->nslots = 1;
	return redex;
}

static struct node *
prim_return_string(struct node *redex, const char *str)
{
	assert(redex->nref == 1);
	assert(redex->backref);
	assert(redex->variety == NODE_APP);
	assert(redex->nslots);
	node_recycle(redex);
	redex->slots[0].variety = SLOT_STRING;
	redex->slots[0].str = str;
	redex->variety = NODE_VAL;
	redex->nslots = 1;
	return redex;
}

static struct node *
prim_return_var(struct node *redex, struct slot var)
{
	/*
	 * If we're copying an explicit substitution, bump the
	 * reference count to reflect the new sharing.
	 */
	assert(slot_is_ref(var));
	if (var.variety == SLOT_SUBST) var.subst->nref++;

	assert(redex->nref == 1);
	assert(redex->backref);
	assert(redex->variety == NODE_APP);
	assert(redex->nslots);
	node_recycle(redex);
	redex->slots[0] = var;
	redex->variety = NODE_VAR;
	redex->nslots = 1;
	return redex;
}

static bool known(const struct node *redex, size_t i, struct node **arg)
{
	if (i < redex->nslots && redex->slots[i].variety == SLOT_SUBST) {
		*arg = redex->slots[i].subst;
		return true;
	}
	return false;
}

static bool known_cell(const struct node *redex, size_t i, struct node **cell)
{
	struct node *arg;
	if (i < redex->nslots &&
	    redex->slots[i].variety == SLOT_SUBST &&
	    (arg = redex->slots[i].subst)->variety == NODE_CELL) {
		*cell = arg;
		return true;
	}
	return false;
}

static bool known_num(const struct node *redex, size_t i, double *num)
{
	struct node *arg;
	if (i < redex->nslots &&
	    redex->slots[i].variety == SLOT_SUBST &&
	    (arg = redex->slots[i].subst)->nslots == 1 &&
	    arg->slots[0].variety == SLOT_NUM) {
		assert(arg->variety == NODE_VAL);
		*num = arg->slots[0].num;
		return true;
	}
	return false;
}

static bool known_string(const struct node *redex, size_t i, const char **str)
{
	struct node *arg;
	if (i < redex->nslots &&
	    redex->slots[i].variety == SLOT_SUBST &&
	    (arg = redex->slots[i].subst)->nslots == 1 &&
	    arg->slots[0].variety == SLOT_STRING) {
		assert(arg->variety == NODE_VAL);
		*str = arg->slots[0].str;
		return true;
	}
	return false;
}

static struct node *prim_reduce_arith1(unsigned variety, struct node *redex)
{
	double arg, val;
	if (!(	redex->nslots == 2 &&
		known_num(redex, 1, &arg)))
		return redex->prev;
	switch ((enum prim_variety) variety) {
	case PRIM_IS_INTEGRAL: {
		double intpart;
		val = !modf(arg, &intpart);
		break;
	}
	case PRIM_NOT:	val = !arg; break;
	default: panicf("Unhandled primitive variety %u\n", variety);
	}
	return prim_return_num(redex, val);
}

static struct node *prim_reduce_arith2(unsigned variety, struct node *redex)
{
	double lhs, rhs, val;
	if (!(	redex->nslots == 3 &&
		known_num(redex, 1, &lhs) &&
		known_num(redex, 2, &rhs)))
		return redex->prev;
	switch ((enum prim_variety) variety) {
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
	case PRIM_AND:	val = lhs && rhs; break;
	case PRIM_OR:	val = lhs || rhs; break;
	case PRIM_XOR:	val = !lhs ^ !rhs; break;	/* logical XOR */
	default: panicf("Unhandled primitive variety %u\n", variety);
	}
	return prim_return_num(redex, val);
}

static struct node *prim_reduce_at(unsigned variety, struct node *redex)
{
	double arg, ipart;
	struct node *cell;
	struct slot var;
	assert(variety == PRIM_AT);
	if (!(	redex->nslots == 3 &&
		known_num(redex, 1, &arg) &&
		known_cell(redex, 2, &cell)))
		return redex->prev;
	if (modf(arg, &ipart))
		panicf("Index not an integer: %g\n", arg);
	if (arg < 0 || arg >= cell->nslots)
		panicf("Index out of range: %zu\n", (size_t) arg);
	var = cell->slots[(size_t) arg];
	return prim_return_var(redex, var);
}

static struct node *prim_reduce_cell(unsigned variety, struct node *redex)
{
	double nelems;
	if (!(	redex->nslots == 3 &&
		known_num(redex, 1, &nelems)))
		return redex->prev;
	if (nelems < 0) panic("Negative cell size!\n");
	struct slot k = redex->slots[2];
	if (k.variety == SLOT_SUBST) k.subst->nref += nelems;

	/* could hypothetically recycle in some cases */
	struct node *cell = NodeCell(redex->prev, redex->depth, nelems);
	for (size_t i = 0; i < nelems; ++i) cell->slots[i] = k;
	return prim_replace_redex(redex, cell);
}

static struct node *prim_reduce_cell_num(unsigned variety, struct node *redex)
{
	struct node *cell;
	double val;
	assert(variety == PRIM_NELEMS);
	if (!(	redex->nslots == 2 &&
		known_cell(redex, 1, &cell)))
		return redex->prev;
	val = cell->nslots;
	return prim_return_num(redex, val);
}

static struct node *prim_reduce_fill(unsigned variety, struct node *redex)
{
	double nelems;
	if (!(	redex->nslots == 3 &&
		known_num(redex, 1, &nelems)))
		return redex->prev;
	struct slot fn = redex->slots[2];
	if (fn.variety == SLOT_SUBST) fn.subst->nref += nelems;

	/*
 	 * Technically we don't need to allocate unless the size of the
	 * output cell is more than 3 (bigger than the redex),
	 * otherwise we could recycle the redex.  Not pursuing that
	 * microoptimization at the moment, for simplicity.
	 */
	struct node *cell = NodeCell(redex->prev, redex->depth, nelems),
		    *prev = cell;
	for (size_t i = 0; i < nelems; ++i) {

		/* create and link arguments */
		struct node *app = NodeApp(prev, redex->depth, 1);
		struct node *arg = NodeNum(app, redex->depth, i);
		prev->next = app, app->next = arg;
		prev = arg;

		/* connect cell to application */
		cell->slots[i].variety = SLOT_SUBST;
		cell->slots[i].subst = app;
		app->backref = &cell->slots[i];
		app->nref++;

		/* set application function; backref not needed here */
		/* function's reference count already adjusted above */
		app->slots[0] = fn;

		/* connect application to argument */
		app->slots[1].variety = SLOT_SUBST;
		app->slots[1].subst = arg;
		arg->backref = &app->slots[1];
		arg->nref++;
	}

	/*
	 * Attach the entire chain to redex's referent.
	 */
	assert(redex->nref == 1);
	assert(redex->backref);
	assert(redex->backref->subst == redex);
	cell->backref = redex->backref;
	cell->backref->subst = cell;
	redex->nref--, cell->nref++;

	/*
	 * Replace the redex in the reduction chain and free it.
	 */
	cell->prev = redex->prev;
	prev->next = redex->next;
	cell->prev->next = cell;
	prev->next->prev = prev;
	node_deref(redex);
	node_free(redex);
	return prev;
}

static struct node *prim_reduce_find(unsigned variety, struct node *redex)
{
	double arg, val;
	struct node *cell;
	assert(variety == PRIM_FIND);
	if (!(	redex->nslots == 3 &&
		known_num(redex, 1, &arg) &&
		known_cell(redex, 2, &cell)))
		return redex->prev;
	size_t i;
	for (i = 0; i < cell->nslots; ++i)
		if (cell->slots[i].variety == SLOT_SUBST &&
		    cell->slots[i].subst->variety == NODE_VAL &&
		    cell->slots[i].subst->nslots == 1 &&
		    cell->slots[i].subst->slots[0].variety == SLOT_NUM &&
		    cell->slots[i].subst->slots[0].num == arg)
			break;
	val = i == cell->nslots ? -1.0 : i;
	return prim_return_num(redex, val);
}

static struct node *prim_reduce_fuse(unsigned variety, struct node *redex)
{
	struct node *cell0, *cell1;
	if (!(	redex->nslots == 3 &&
		known_cell(redex, 1, &cell0) &&
		known_cell(redex, 2, &cell1)))
		return redex->prev;

	/*
	 * If one of the cells is empty, we can return the other without
	 * allocating.
	 */
	struct slot var;
	if (cell0->nslots == 0) {
		var = redex->slots[2];
		return prim_return_var(redex, var);
	}
	if (cell1->nslots == 0) {
		var = redex->slots[1];
		return prim_return_var(redex, var);
	}

	/*
 	 * Technically we don't need to allocate unless the sum of the
	 * sizes of the two cells is more than 3 (bigger than the redex),
	 * otherwise we could recycle the redex.  Not pursuing that
	 * microoptimization at the moment, for simplicity.
	 */
	size_t nslots = cell0->nslots + cell1->nslots;
	struct node *cell = NodeCell(redex->prev, redex->depth, nslots);
	size_t i = 0;
	for (size_t j = 0; j < cell0->nslots; ++i, ++j) {
		cell->slots[i] = cell0->slots[j];
		if (cell->slots[i].variety == SLOT_SUBST)
			cell->slots[i].subst->nref++;
	}
	for (size_t j = 0; j < cell1->nslots; ++i, ++j) {
		cell->slots[i] = cell1->slots[j];
		if (cell->slots[i].variety == SLOT_SUBST)
			cell->slots[i].subst->nref++;
	}
	assert(i == nslots);

	/*
	 * Attach the new cell to redex's referent.
	 */
	assert(redex->nref == 1);
	assert(redex->backref);
	assert(redex->backref->subst == redex);
	cell->backref = redex->backref;
	cell->backref->subst = cell;
	redex->nref--, cell->nref++;

	/*
	 * Replace the redex in the reduction chain and free it.
	 */
	node_replace(cell, redex);
	node_deref(redex);
	node_free(redex);
	return cell;
}

static struct node *prim_reduce_pair(unsigned variety, struct node *redex)
{
	struct node *cell;
	if (!(	redex->nslots == 2 &&
		known_cell(redex, 1, &cell) &&
		cell->nslots == 2))
		return redex->prev;
	struct slot var;
	switch ((enum prim_variety) variety) {
	case PRIM_CAR:	var = cell->slots[0]; break;
	case PRIM_CDR:	var = cell->slots[1]; break;
	default: panicf("Unhandled primitive variety %u\n", variety);
	}
	return prim_return_var(redex, var);
}

static struct node *prim_reduce_str2(unsigned variety, struct node *redex)
{
	const char *lhs, *rhs;
	char *val;
	if (!(	redex->nslots == 3 &&
		known_string(redex, 1, &lhs) &&
		known_string(redex, 2, &rhs)))
		return redex->prev;
	switch ((enum prim_variety) variety) {
	case PRIM_CONCAT: {
		val = xmalloc(strlen(lhs) + strlen(rhs) + 1);
		strcpy(stpcpy(val, lhs), rhs);
		break;
	}
	default: panicf("Unhandled primitive variety %u\n", variety);
	}
	return prim_return_string(redex, val);
}

static struct node *prim_reduce_test(unsigned variety, struct node *redex)
{
	struct node *arg;
	if (!(	redex->nslots == 2 &&
		known(redex, 1, &arg)))
		return redex->prev;
	double val;
	switch ((enum prim_variety) variety) {
	case PRIM_IS_CELL:
		val = arg->variety == NODE_CELL;
		break;
	case PRIM_IS_NIL:
		val = arg->variety == NODE_CELL && arg->nslots == 0;
		break;
	case PRIM_IS_PAIR:
		val = arg->variety == NODE_CELL && arg->nslots == 2;
		break;
	default:
		panicf("Unhandled primitive variety %u\n", variety);
	}
	return prim_return_num(redex, val);
}

struct prim prim_add = {
	.variety = PRIM_ADD,
	.syntax = PRIM_SYNTAX_OP2,
	.name = "+",
	.reduce = prim_reduce_arith2,
};

struct prim prim_sub = {
	.variety = PRIM_SUB,
	.syntax = PRIM_SYNTAX_OP2,
	.name = "-",
	.reduce = prim_reduce_arith2,
};

struct prim prim_mult = {
	.variety = PRIM_MULT,
	.syntax = PRIM_SYNTAX_OP2,
	.name = "*",
	.reduce = prim_reduce_arith2,
};

struct prim prim_div = {
	.variety = PRIM_DIV,
	.syntax = PRIM_SYNTAX_OP2,
	.name = "/",
	.reduce = prim_reduce_arith2,
};

struct prim prim_eq = {
	.variety = PRIM_EQ,
	.syntax = PRIM_SYNTAX_OP2,
	.name = "==",
	.reduce = prim_reduce_arith2,
};

struct prim prim_ne = {
	.variety = PRIM_NE,
	.syntax = PRIM_SYNTAX_OP2,
	.name = "<>",
	.reduce = prim_reduce_arith2,
};

struct prim prim_lt = {
	.variety = PRIM_LT,
	.syntax = PRIM_SYNTAX_OP2,
	.name = "<",
	.reduce = prim_reduce_arith2,
};

struct prim prim_lte = {
	.variety = PRIM_LTE,
	.syntax = PRIM_SYNTAX_OP2,
	.name = "<=",
	.reduce = prim_reduce_arith2,
};

struct prim prim_gt = {
	.variety = PRIM_GT,
	.syntax = PRIM_SYNTAX_OP2,
	.name = ">",
	.reduce = prim_reduce_arith2,
};

struct prim prim_gte = {
	.variety = PRIM_GTE,
	.syntax = PRIM_SYNTAX_OP2,
	.name = ">=",
	.reduce = prim_reduce_arith2,
};

struct prim prim_and = {
	.variety = PRIM_AND,
	.syntax = PRIM_SYNTAX_FUNCTION,
	.name = "$and",
	.reduce = prim_reduce_arith2,
};

struct prim prim_or = {
	.variety = PRIM_OR,
	.syntax = PRIM_SYNTAX_FUNCTION,
	.name = "$or",
	.reduce = prim_reduce_arith2,
};

struct prim prim_xor = {
	.variety = PRIM_XOR,
	.syntax = PRIM_SYNTAX_FUNCTION,
	.name = "$xor",
	.reduce = prim_reduce_arith2,
};

struct prim prim_not = {
	.variety = PRIM_NOT,
	.syntax = PRIM_SYNTAX_FUNCTION,
	.name = "$not",
	.reduce = prim_reduce_arith1,
};

struct prim prim_is_integral = {
	.variety = PRIM_IS_INTEGRAL,
	.syntax = PRIM_SYNTAX_FUNCTION,
	.name = "$is-integral",
	.reduce = prim_reduce_arith1,
};

struct prim prim_concat = {
	.variety = PRIM_CONCAT,
	.syntax = PRIM_SYNTAX_OP2,
	.name = "++",
	.reduce = prim_reduce_str2,
};

struct prim prim_at = {
	.variety = PRIM_AT,
	.syntax = PRIM_SYNTAX_FUNCTION,
	.name = "$at",
	.reduce = prim_reduce_at,
};

struct prim prim_cell = {
	.variety = PRIM_CELL,
	.syntax = PRIM_SYNTAX_FUNCTION,
	.name = "$cell",
	.reduce = prim_reduce_cell,
};

struct prim prim_fill = {
	.variety = PRIM_FILL,
	.syntax = PRIM_SYNTAX_FUNCTION,
	.name = "$fill",
	.reduce = prim_reduce_fill,
};

struct prim prim_find = {
	.variety = PRIM_FIND,
	.syntax = PRIM_SYNTAX_FUNCTION,
	.name = "$find",
	.reduce = prim_reduce_find,
};

struct prim prim_fuse = {
	.variety = PRIM_FUSE,
	.syntax = PRIM_SYNTAX_FUNCTION,
	.name = "$fuse",
	.reduce = prim_reduce_fuse,
};

struct prim prim_is_cell = {
	.variety = PRIM_IS_CELL,
	.syntax = PRIM_SYNTAX_FUNCTION,
	.name = "$is-cell",
	.reduce = prim_reduce_test,
};

struct prim prim_nelems = {
	.variety = PRIM_NELEMS,
	.syntax = PRIM_SYNTAX_OP1,
	.name = "#",
	.reduce = prim_reduce_cell_num,
};

struct prim prim_car = {
	.variety = PRIM_CAR,
	.syntax = PRIM_SYNTAX_OP1,
	.name = "#0",
	.reduce = prim_reduce_pair,
};

struct prim prim_cdr = {
	.variety = PRIM_CDR,
	.syntax = PRIM_SYNTAX_OP1,
	.name = "#1",
	.reduce = prim_reduce_pair,
};

struct prim prim_is_nil = {
	.variety = PRIM_IS_NIL,
	.syntax = PRIM_SYNTAX_FUNCTION,
	.name = "$is-nil",
	.reduce = prim_reduce_test,
};

struct prim prim_is_pair = {
	.variety = PRIM_IS_PAIR,
	.syntax = PRIM_SYNTAX_FUNCTION,
	.name = "$is-pair",
	.reduce = prim_reduce_test,
};

struct prim prim_undefined = {
	.variety = PRIM_UNDEFINED,
	.syntax = PRIM_SYNTAX_ATOM,
	.name = "$undefined",
};

struct prim prim_panic = {
	.variety = PRIM_PANIC,
	.syntax = PRIM_SYNTAX_FUNCTION,
	.name = "$panic",
};
