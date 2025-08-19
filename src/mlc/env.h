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
 * A global environment of defined constants.
 *
 * These constants can be either transparent (with visible values which
 * can be referenced by subsequently defined terms) or opaque (serving as
 * free variables, i.e. representing unevaluable expressions).  Binder
 * flags track opacity as well as the reduction status of a given
 * constant.  Depending on a constant's binder flags, we might store it
 * as either an (evaluated or unevaluated) node or as a term to be
 * substituted syntactically via lifting using let-expressions.
 *
 * In the vanilla Lambda Calculus, all syntactically valid variable names
 * are semantically valid but some are free i.e. not bound by enclosing
 * lambda expressions.  Such free variables are manipulable as terms but
 * not substituted-for.  In MLC, we instead use pre-declared opaque
 * constants; declaration-by-mention (implicit creation of free variables)
 * is too error-prone in a software engineering setting.
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
struct wordbuf;
struct wordtab;

extern void env_init(void);
extern void env_dump(const char *substr);
extern const struct binder *env_at(size_t index);
extern struct binder *env_bind(symbol_mt name, symbol_mt space);
extern struct binder *env_lookup(symbol_mt name, const struct wordtab *spaces);
extern bool env_test(symbol_mt name);

extern int env_begin(symbol_mt space, symbol_mt library);
extern void env_publish(symbol_mt space, symbol_mt published);
extern void env_published(symbol_mt space, struct wordbuf *buf);
extern symbol_mt env_whose(symbol_mt space);

#endif /* LARK_MLC_ENV_H */
