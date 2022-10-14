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
#include <stdio.h>

#include <util/memutil.h>
#include <util/wordtab.h>

#include "env.h"
#include "form.h"
#include "term.h"

#define ENV_SIZE_HINT 100

static struct wordtab the_global_env;
static unsigned the_last_index;

int env_entry_cmp(const void *a, const void *b)
{
	const struct env_entry *ea = *(const struct env_entry *const *) a,
			       *eb = *(const struct env_entry *const *) b;
	return	(ea->index > eb->index) ? +1 :
		(ea->index < eb->index) ? -1 : 0;
}

void env_init(void)
{
	wordtab_init(&the_global_env, ENV_SIZE_HINT);
}

/*
 * Environment dumping, a debugging function, currently iterates directly
 * over the environment hash table; entries are printed out of index order.
 */
void env_dump(void)
{
	struct wordtab_iter iter;
	wordtab_iter_init(&the_global_env, &iter);
	struct wordtab_entry *entry;
	while ((entry = wordtab_iter_next(&iter))) {
		const struct env_entry *ee = entry->data;
		assert(entry->key == ee->name);
		assert(ee->var->variety == TERM_FREE_VAR);
		assert(ee->name == ee->var->fv.name);
		printf("#%u\t%s", ee->index, symtab_lookup(ee->name));
		if (ee->val) {
			fputs(" := ", stdout);
			term_print(ee->val);
		}
		putchar('\n');
	}
}

static inline struct env_entry *env_get(symbol_mt name)
{
	return wordtab_get(&the_global_env, name);
}

static struct env_entry *env_put(symbol_mt name, struct term *val)
{
	struct env_entry *pe = xmalloc(sizeof *pe);
	pe->name = name;
	pe->index = ++the_last_index;
	pe->var = TermFreeVar(name);
	pe->val = val;
	wordtab_put(&the_global_env, name, pe);
	return pe;
}

struct env_entry env_declare(symbol_mt name)
{
	struct env_entry *pe = env_get(name);
	return pe ? *pe : *env_put(name, NULL);
}

struct env_entry env_define(symbol_mt name, struct term *val)
{
	/* fail if name already present in environment */
	if (env_get(name))
		return (struct env_entry) { .name = name, .index = 0,
					    .var = NULL, .val = NULL };
	return *env_put(name, val);
}

bool env_test(symbol_mt name)
{
	return !!env_get(name);
}
