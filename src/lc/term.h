#ifndef LARK_LC_TERM_H
#define LARK_LC_TERM_H
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

#include <stdint.h>

#include <util/symtab.h>

/*
 * Terms of the untyped lambda calculus.  This term structure supports
 * both named variables and de Bruijn indexing, although we expect that
 * reduction will be performed using de Bruijn terms only, without the
 * use of alpha conversion.
 */

/*
 * Types for terms.  In addition to the core lambda calculus types, we
 * also support some implementation artifacts like symbols and GC free
 * list/protection.
 */
enum term_type {
	ERR,	/* Erroneous--not a valid term */
	ABS,	/* Abstraction */
	APP,	/* Application */
	GBG,	/* Garbage */
	SYM,	/* Symbol */
	VAR,	/* Variable */
} __attribute__ ((packed));

/*
 * The actual term type.  Most of the data is in a union indexed by
 * the term_type enumeration.
 */
struct term {
	enum term_type type;
	uint8_t mark;
	union {
		struct { symbol_mt formal; struct term *body; } abs;
		struct { struct term *fun, *arg; } app;
		struct { struct term *nextfree; } gbg;
		struct { symbol_mt name; struct term *body; } sym;
		struct { int index; symbol_mt name; } var;
	};
};

extern struct term the_error_term;

/*
 * Constructors for terms.
 */
extern struct term *Abs(symbol_mt formal, struct term *body);
extern struct term *App(struct term *fun, struct term *arg);
extern struct term *Sym(symbol_mt name);
static inline struct term *SymS(const char *name)
	{ return Sym(symtab_intern(name)); }
extern struct term *Var(symbol_mt name);
static inline struct term *VarS(const char *name)
	{ return Var(symtab_intern(name)); }
extern struct term *VarI(int index);

/*
 * Traverse the given term, setting de Bruijn indexes destructively.
 */
extern void term_index(struct term *term);

/*
 * External representation of terms.
 */
extern void term_print(const struct term *term);
extern void term_print_indexed(const struct term *term);

#endif /* LARK_LC_TERM_H */
