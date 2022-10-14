#ifndef LARK_SLC_FORM_H
#define LARK_SLC_FORM_H
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
 * Representations (external forms) of Lambda Calculus terms.  We use
 * 'term' to refer to forms which have been analyzed and are ready for
 * computing; forms are instead the textual representation constructed
 * by the parser.
 */

#include <util/symtab.h>

enum form_variety {
	FORM_INVALID,
	FORM_ABS,
	FORM_APP,
	FORM_VAR,
} __attribute__ ((packed));

struct form {
	enum form_variety variety;
	union {
		struct { symbol_mt formal; struct form *body; } abs;
		struct { struct form *fun, *arg; } app;
		struct { symbol_mt name; } var;
	};
};

extern struct form *FormAbs(symbol_mt formal, struct form *body);
extern struct form *FormApp(struct form *fun, struct form *arg);
extern struct form *FormVar(symbol_mt name);
static inline struct form *FormVarS(const char *name)
	{ return FormVar(symtab_intern(name)); }

extern void form_free(struct form *form);
extern void form_print(const struct form *form);

#endif /* LARK_SLC_FORM_H */
