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
	FORM_CELL,
	FORM_DEF,
	FORM_FIX,
	FORM_LET,
	FORM_OP1,
	FORM_OP2,
	FORM_PRIM,
	FORM_NUM,
	FORM_SECTION,
	FORM_STRING,
	FORM_TEST,
	FORM_VAR,
} __attribute__ ((packed));

enum form_syntax {
	FORM_SYNTAX_AUTO,
	FORM_SYNTAX_PREFIX,
	FORM_SYNTAX_POSTFIX,
} __attribute__ ((packed));

struct prim;
struct YYLTYPE;

struct form {
	enum form_variety variety;
	enum form_syntax syntax;
	struct form *prev;
	int line0, line1;
	union {
		struct { struct form *self, *params, *bodies; } abs;
		struct { struct form *fun, *args; } app;
		struct { struct form *elts; } cell;
		struct { struct form *var, *val; } def;
		struct { struct form *defs, *body; } let;
		struct { const struct prim *prim; struct form *arg; } op1;
		struct { const struct prim *prim; struct form *lhs, *rhs; } op2;
		struct { struct form *pred, *csq, *alt; } test;
		struct { symbol_mt name; } var;
		double num;
		const struct prim *prim;
		const char *huid;
		const char *str;
	};
};

extern struct form *FormAbs(struct form *params, struct form *bodies);
extern struct form *FormApp(struct form *fun, struct form *args,
			    enum form_syntax syntax);
extern struct form *FormCell(struct form *elts);
extern struct form *FormDef(struct form *var, struct form *val);
extern struct form *FormFix(struct form *self, struct form *params,
			    struct form *bodies);
extern struct form *FormLet(struct form *defs, struct form *body);
extern struct form *FormNum(double num);
extern struct form *FormOp1(const struct prim *prim, struct form *arg);
extern struct form *FormOp2(const struct prim *prim,
			    struct form *lhs, struct form *rhs);
extern struct form *FormPrim(const struct prim *prim,
			     const struct YYLTYPE *loc);
extern struct form *FormSection(const char *huid,
				const struct YYLTYPE *loc);
extern struct form *FormString(const char *str);
extern struct form *FormStringConcat(struct form *str0, struct form *str1);
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
