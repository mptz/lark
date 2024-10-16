#ifndef LARK_MLC_PRIM_H
#define LARK_MLC_PRIM_H
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

enum prim_syntax {
	PRIM_SYNTAX_INVALID,
	PRIM_SYNTAX_ATOM,
	PRIM_SYNTAX_FUNCTION,
	PRIM_SYNTAX_OP1,
	PRIM_SYNTAX_OP2,
};

struct node;

struct prim {
	unsigned variety;
	enum prim_syntax syntax;
	const char *name;
	struct node *(*reduce)(unsigned variety, struct node *);
};

extern struct prim

	/* arithmetic */
	prim_add, prim_sub, prim_mult, prim_div,

	/* equality and inequality */
	prim_eq, prim_ne, prim_lt, prim_lte, prim_gt, prim_gte,

	/* Boolean logic */
	prim_and, prim_or, prim_xor, prim_not,

	/* floating-point operations */
	prim_is_integral,

	/* string operations */
	prim_concat,

	/* cell operations */
	prim_at, prim_cell, prim_fill, prim_find, prim_fuse,
	prim_is_cell, prim_nelems,

	/* list operations */
	prim_car, prim_cdr, prim_is_nil, prim_is_pair,

	prim_undefined, prim_panic;

#endif /* LARK_MLC_PRIM_H */
