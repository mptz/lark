#ifndef LARK_MLC_FORM_H
#define LARK_MLC_FORM_H
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
	FORM_FIX,
	FORM_OP1,
	FORM_OP2,
	FORM_PAIR,
	FORM_PRIM,
	FORM_NIL,
	FORM_NUM,
	FORM_TEST,
	FORM_VAR,
} __attribute__ ((packed));

enum form_syntax {
	FORM_SYNTAX_AUTO,
	FORM_SYNTAX_PREFIX,
	FORM_SYNTAX_POSTFIX,
} __attribute__ ((packed));

struct form {
	enum form_variety variety;
	enum form_syntax syntax;
	struct form *prev;
	union {
		struct { struct form *self, *params, *bodies; } abs;
		struct { struct form *fun, *args; } app;
		struct { unsigned op; struct form *arg; } op1;
		struct { unsigned op; struct form *lhs, *rhs; } op2;
		struct { struct form *car, *cdr; } pair;
		struct { struct form *pred, *csq, *alt; } test;
		struct { symbol_mt name; } var;
		double num;
		unsigned prim;
	};
};

extern struct form *FormAbs(struct form *params, struct form *bodies);
extern struct form *FormApp(struct form *fun, struct form *args,
			    enum form_syntax syntax);
extern struct form *FormFix(struct form *self, struct form *params,
			    struct form *bodies);
extern struct form *FormNum(double num);
extern struct form *FormOp1(int op, struct form *arg);
extern struct form *FormOp2(int op, struct form *lhs, struct form *rhs);
extern struct form *FormNil(void);
extern struct form *FormPair(struct form *car, struct form *cdr);
extern struct form *FormPrim(unsigned prim);
extern struct form *FormTest(struct form *pred, struct form *csq,
			     struct form *alt);
extern struct form *FormVar(symbol_mt name);
static inline struct form *FormVarNext(symbol_mt name, struct form *prev)
	{ struct form *r = FormVar(name); r->prev = prev; return r; }
static inline struct form *FormVarS(const char *name)
	{ return FormVar(symtab_intern(name)); }

extern void form_free(struct form *form);
static inline size_t form_length(const struct form *form)
	{ size_t n; for (n = 0; form; ++n, form = form->prev); return n; }
extern void form_print(const struct form *form);
extern struct form *form_splice(struct form *a, struct form *b);

#endif /* LARK_MLC_FORM_H */
