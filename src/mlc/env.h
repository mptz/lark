#ifndef LARK_MLC_ENV_H
#define LARK_MLC_ENV_H
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
 * A global environment of free variables and defined substitutions.
 *
 * We're a little free (pun intended) with our use of the technical term
 * 'free variable' vis a vis its Lambda Calculus use.  In the Lambda
 * Calculus, all syntactically valid variable names are semantically valid
 * but some are free i.e. not bound by enclosing lambda expressions.  Such
 * free variables are manipulable as terms but not substituted-for.
 *
 * We instead require free variables to be declared; declaration-by-mention
 * (implicit creation of free variables) is too error-prone in a software
 * engineering setting.  In our usage, a free variable is a declared variable
 * with no associated value.  Our reduction implementation treats such
 * variables as self-resolving i.e. they can be manipulated but resolve to
 * themselves rather than to values.
 *
 * The environment can also be explicitly extended with defined names
 * e.g. "id := [x. x]", in which case free uses of the name "id" will
 * subsequently resolve to the root of "[x. x]".  This substitution is
 * currently implemented via lambda lifting in resolve().
 *
 * The environment is namespaced; names in the global environment, whether
 * declared or defined, must be unique in their namespaces but may exist
 * alongside identical names in other spaces.  Name lookup fails unless
 * the combination of name and active namespaces yields exactly one value.
 */

#include <stdbool.h>

#include <util/symtab.h>	/* symbol_mt */

struct binder;
struct node;
struct term;
struct wordtab;

extern void env_init(void);
extern void env_dump(const char *substr);
extern const struct binder *env_at(size_t index);
extern struct binder *env_define(symbol_mt name, symbol_mt space,
				 struct node *val);
extern struct binder *env_install(symbol_mt name, symbol_mt space,
				  struct term *term);
extern struct binder *env_lookup(symbol_mt name, const struct wordtab *spaces);
extern void env_get_public(struct wordtab *spaces);
extern bool env_is_public(symbol_mt space);
extern void env_public(symbol_mt space);
extern int env_new_space(symbol_mt space);
extern bool env_test(symbol_mt name);

#endif /* LARK_MLC_ENV_H */
