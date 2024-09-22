#ifndef LARK_MLC_ENV_H
#define LARK_MLC_ENV_H
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
 * A global environment of free variables and defined substitutions.
 *
 * The environment tracks free variables (i.e. maps symbols to terms of
 * variety TERM_FREE_VAR) to guarantee that each free instance of a name
 * in a term resolves to the same term.  The first use of a free name in
 * any term implicitly extends the environment with a fresh variable;
 * subsequent free uses of that name resolve to the same variable.
 *
 * The environment can also be explicitly extended with defined names
 * e.g. "id := \x. x", in which case free uses of the name "id" will
 * subsequently resolve to the root of "\x. x" (this substitution is
 * currently implemented via lambda lifting in resolve()).
 *
 * A name added to the environment by definition must be distinct from
 * all prior names, whether those names were added implicitly by use of
 * free variables or by definition.  Failure by redefinition yields a
 * returned entry with .var == NULL.
 */

#include <stdbool.h>

#include <util/symtab.h>

/* not the ideal location for this, but no obvious better one */
extern symbol_mt the_placeholder_symbol;

struct term;
struct env_entry {
	symbol_mt name;
	unsigned index;
	struct term *var, *val;
};
extern int env_entry_cmp(const void *a, const void *b);	/* for qsort */

extern void env_init(void);
extern void env_dump(const char *substr);
extern struct env_entry env_declare(symbol_mt name);
extern struct env_entry env_define(symbol_mt name, struct term *val);
extern bool env_test(symbol_mt name);

#endif /* LARK_MLC_ENV_H */
