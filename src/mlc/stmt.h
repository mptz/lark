#ifndef LARK_MLC_STMT_H
#define LARK_MLC_STMT_H
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

#include <util/symtab.h>

struct form;

enum stmt_variety {
	STMT_INVALID,
	STMT_CONCEAL,
	STMT_DEF,
	STMT_ECHO,
	STMT_INSPECT,
	STMT_PUBLIC,
	STMT_REQUIRE,
	STMT_REVEAL,
	STMT_SECTION,
	STMT_VAL,
} __attribute__ ((packed));

struct stmt {
	enum stmt_variety variety;
	int line0, line1;
	union {
		struct { struct form *var, *val; unsigned flags; } def;
		struct { struct form	   *val; unsigned flags; } val;
		struct form *form;
		symbol_mt sym;
	};
};

extern struct stmt *StmtConceal(symbol_mt id);
extern struct stmt *StmtDef(struct form *var, struct form *val, unsigned flags);
extern struct stmt *StmtEcho(struct form *str);
extern struct stmt *StmtInspect(symbol_mt id);
extern struct stmt *StmtPublic(void);
extern struct stmt *StmtRequire(symbol_mt id);
extern struct stmt *StmtReveal(symbol_mt id);
extern struct stmt *StmtSection(symbol_mt id);
extern struct stmt *StmtVal(struct form *val, unsigned flags);

extern void stmt_free(struct stmt *stmt);
extern void stmt_eval(const struct stmt *stmt);

#endif /* LARK_MLC_STMT_H */
