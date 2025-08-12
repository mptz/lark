#ifndef LARK_MLC_TERM_H
#define LARK_MLC_TERM_H
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

/*
 * Name-resolved, locally nameless Lambda Calculus terms.  All terms
 * must be closed, i.e. no free variables.  We support constants and
 * variables:
 *
 * Constants precede terms: constants are references to values in an
 * ambient global environment containing a countable number of entries.
 * Constants are represented by unsigned integral indexes into that
 * global environment.  Free variables can be simulated by 'opaque'
 * environment bindings which prevent constants from being expanded to
 * their values.
 *
 * Variables are bound within terms: they are references to surrounding
 * binding forms in the same term.  These bound variables use De Bruijn
 * indexing combined with argument numbering.
 *
 * In both cases (constants and variables) we need a redundant symbolic
 * name for printing and diagnostics.  For variables, we store this in
 * the term; for constants, it's available from the global environment.
 */

#include <util/symtab.h>

struct binder;

enum term_variety {
	TERM_INVALID,
	TERM_ABS,		/* nonrecursive abstraction */
	TERM_APP,		/* function application */
	TERM_CELL,		/* fixed-size n-ary value */
	TERM_CONSTANT,		/* global constant */
	TERM_FIX,		/* recursive abstraction */
	TERM_LET,		/* scoped variable-binding construct */
	TERM_NUM,		/* number atom */
	TERM_PRIM,		/* primitive operation or value */
	TERM_PRUNED,		/* truncated unsharing */
	TERM_STRING,		/* string atom */
	TERM_SYMBOL,		/* symbol atom */
	TERM_TEST,		/* conditional expression */
	TERM_VAR,		/* local variable */
} __attribute__ ((packed));

struct term {
	enum term_variety variety;
	union {
		struct { size_t nformals; symbol_mt *formals;
			 struct term *body; } abs;
		struct { struct term *fun;
			 size_t nargs; struct term **args; } app;
		struct { size_t nelts; struct term **elts; } cell;
		struct { const struct binder *binder; } constant;
		struct { size_t ndefs; symbol_mt *vars;
			 struct term **vals, *body; } let;
		struct { size_t ncsqs, nalts;
			 struct term *pred, **csqs, **alts; } test;
		struct { int up, across; symbol_mt name; } var;
		double num;
		const struct prim *prim;
		const char *str;
		symbol_mt sym;
	};
};

extern struct term *TermAbs(size_t nformals, symbol_mt *formals,
			    struct term *body);
extern struct term *TermApp(struct term *fun, size_t nargs, struct term **args);
extern struct term *TermCell(size_t nelts, struct term **elts);
extern struct term *TermConstant(const struct binder *binder);
extern struct term *TermFix(size_t nformals, symbol_mt *formals,
			    struct term *body);
extern struct term *TermLet(size_t ndefs, symbol_mt *vars,
			    struct term **vals, struct term *body);
extern struct term *TermPrim(const struct prim *prim);
extern struct term *TermPruned(void);
extern struct term *TermNum(double num);
extern struct term *TermString(const char *str);
extern struct term *TermSymbol(symbol_mt sym);
extern struct term *TermTest(struct term *pred,
			     size_t ncsqs, struct term **csqs,
			     size_t nalts, struct term **alts);
extern struct term *TermVar(int up, int across, symbol_mt name);

extern void term_print(const struct term *term);

#endif /* LARK_MLC_TERM_H */
