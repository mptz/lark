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

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <util/memutil.h>
#include <util/message.h>
#include <util/wordbuf.h>

#include "env.h"
#include "form.h"
#include "readback.h"
#include "term.h"

/*
 * This is sensitive to the number of symbols generated in the past...
 * this makes testing fragile.  Should come up with a more stable approach.
 *	https://github.com/mptz/lark/issues/38
 */
#define MAX_SYMBOL_SIZE 256

static symbol_mt fresh_name(symbol_mt name, const struct wordbuf *names)
{
	size_t i, bound;
	while (1) {
		if (env_test(name))
			goto freshen;
		for (i = 0, bound = wordbuf_used(names); i < bound; ++i)
			if (wordbuf_at(names, i) == name)
				goto freshen;
		return name;
	freshen:
		/* dummy statement preceding declaration */;
		const char *last = symtab_lookup(name);
		if (*last == '\0')
			panic("Empty symbol in fresh_name\n");
		char *next = xmalloc(strlen(last) + 2),
		     *p = stpcpy(next, last) - 1;
		assert(p >= next);	/* verified above name is not empty */
		if (*p >= 'A' && *p < 'Z')
			*p += 1;
		else
			*++p = 'A', *++p = '\0';
		name = symtab_intern(next);
		free(next);
	}
}

static struct form *readback_term(const struct term *term,
				  struct wordbuf *names)
{
	switch (term->variety) {
	case TERM_ABS: {
		symbol_mt formal = fresh_name(term->abs.formal, names);
		wordbuf_push(names, formal);
		struct form *retval = FormAbs(formal,
			readback_term(term->abs.body, names));
		wordbuf_pop(names);
		return retval;
	}
	case TERM_APP:
		return FormApp(readback_term(term->app.fun, names),
			       readback_term(term->app.arg, names));
	case TERM_BOUND_VAR: {
		int depth = wordbuf_used(names);
		assert(term->bv.index < depth);
		return FormVar(wordbuf_at(names, depth - term->bv.index - 1));
	}
	case TERM_FREE_VAR:
		return FormVar(term->fv.name);
	default:
		panicf("Unhandled term variety %d\n", term->variety);
	}
}

struct form *readback(const struct term *term)
{
	struct wordbuf names;
	wordbuf_init(&names);
	struct form *retval = readback_term(term, &names);
	wordbuf_fini(&names);
	return retval;
}
