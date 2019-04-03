/*
 * Copyright (c) 2009-2019 Michael P. Touloumtzis.
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

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "hashtab.h"
#include "memutil.h"
#include "message.h"
#include "symtab.h"
#include "wordbuf.h"

static struct hashtab the_symtab_hash;
static struct wordbuf the_symtab_buf;
static int symtab_initialized = 0;

void
symtab_dump(void)
{
	fputs("Symbols:\n", stdout);
	size_t b = wordbuf_used(&the_symtab_buf);
	for (size_t i = 1 /* skip placeholder */; i < b; ++i)
		printf("%zu = %s\n", i,
			(const char *) wordbuf_at(&the_symtab_buf, i));

	fputs("Hash table contents:\n", stdout);
	struct hashtab_iter iter;
	struct hashtab_entry *entry;
	hashtab_iter_init(&the_symtab_hash, &iter);
	while ((entry = hashtab_iter_next(&iter)))
		printf("%s -> %"PRIuPTR"\n", (const char *) entry->key,
			(uintptr_t) entry->data);

	struct hashtab_stats stats;
	hashtab_stats(&the_symtab_hash, &stats);
	printf("Hash capacity %zu, used %zu, #nests %zu, nests used %zu\n",
		stats.capacity, stats.used, stats.nests, stats.nestsused);
	for (size_t i = 0; i < HASHTAB_NEST_SIZE; ++i)
		printf("Nest usage for slot %zu: %zu\n", i,
			stats.entry_used[i]);
}

symbol_mt
symtab_intern(const char *name)
{
	if (!symtab_initialized) {
		hashtab_init(&the_symtab_hash, 200 /* XXX arbitrary hint */);
		wordbuf_init(&the_symtab_buf);
		/*
		 * We can't return symbol_mt == 0; it puns with NULL used
		 * by the hash table to indicate no entry found; reserve
		 * that slot now.
		 */
		wordbuf_pushback(&the_symtab_buf, (word)
			xstrdup("--- INTERNAL ERROR --- INVALID SYMBOL ---"));
		symtab_initialized = 1;
	}

	size_t size = strlen(name) + 1;
	symbol_mt sym = (uintptr_t) hashtab_get(&the_symtab_hash, name, size);
	if (sym) return sym;

	sym = wordbuf_used(&the_symtab_buf);
	const char *p = xstrdup(name);
	hashtab_put(&the_symtab_hash, p, size, (void*) (uintptr_t) sym);
	wordbuf_pushback(&the_symtab_buf, (word) p);
	return sym;
}

const char *
symtab_lookup(symbol_mt s)
{
	if (s == 0 || s >= wordbuf_used(&the_symtab_buf))
		panic("Invalid symbol table index!\n");
	return (const char *) wordbuf_at(&the_symtab_buf, s);
}
