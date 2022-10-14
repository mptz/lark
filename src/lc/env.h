#ifndef LARK_LC_ENV_H
#define LARK_LC_ENV_H
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
 * Environment to store symbol-term mappings.  This environment is not
 * like the "contexts" which are used to store variable bindings in
 * semantic definitions of programming languages.  In order to maintain
 * this lambda calculator as a "pure" implementation of the untyped
 * lambda calculus, contexts and symbols play no role, and substitution
 * is part of the meta-language rather than the language.
 *
 * In other words, the environment is a convenience to save keystrokes.
 * Before we start evaluating a lambda expression, we *fully* substitute
 * all symbols with their values.  Thus evaluation always starts with a
 * fully bound lambda term (no free variables).
 */

#include <util/symtab.h>

struct term;

extern void env_dump(void);
extern void env_init(void);
extern void env_install(struct term *sym);
extern struct term *env_lookup(symbol_mt name);

#endif /* LARK_LC_ENV_H */
