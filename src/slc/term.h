#ifndef LARK_SLC_TERM_H
#define LARK_SLC_TERM_H
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

/*
 * Name-resolved, locally nameless Lambda Calculus terms.  Free variables
 * are represented by pointers to canonical free variables in the global
 * environment, while bound variables use De Bruijn indexing--though in
 * the latter case we still store a symbolic variable name for printing.
 */

#include <util/symtab.h>

enum term_variety {
	TERM_INVALID,
	TERM_ABS,
	TERM_APP,
	TERM_BOUND_VAR,		/* i.e. local variable */
	TERM_FREE_VAR,		/* i.e. global variable */
} __attribute__ ((packed));

struct term {
	enum term_variety variety;
	union {
		struct { symbol_mt formal; struct term *body; } abs;
		struct { struct term *fun, *arg; } app;
		struct { int index; symbol_mt name; } bv;
		struct { symbol_mt name; } fv;
	};
};

extern struct term *TermAbs(symbol_mt formal, struct term *body);
extern struct term *TermApp(struct term *fun, struct term *arg);
extern struct term *TermBoundVar(int index, symbol_mt name);
extern struct term *TermFreeVar(symbol_mt name);

extern void term_print(const struct term *term);

#endif /* LARK_SLC_TERM_H */
