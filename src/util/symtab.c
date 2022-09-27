/*
 * Copyright (c) 2009-2021 Michael P. Touloumtzis.
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
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "hashtab.h"
#include "memutil.h"
#include "message.h"
#include "symtab.h"
#include "wordbuf.h"

#define SYMTAB_HASHTAB_HINT 200		/* XXX calibrate */

static struct hashtab the_symtab_hash;
static struct wordbuf the_symtab_buf;
static int symtab_initialized = 0;
symbol_mt the_empty_symbol;

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

#define NG 255

static const unsigned char decode_table[] = {
	NG, NG, NG, NG, NG, NG, NG, NG,		/* 0-7 */
	NG, NG, NG, NG, NG, NG, NG, NG,		/* 8-15 */
	NG, NG, NG, NG, NG, NG, NG, NG,		/* 16-23 */
	NG, NG, NG, NG, NG, NG, NG, NG,		/* 24-31 */

	NG, NG, NG, NG, 62, NG, NG, NG,		/* 32-39 */
	NG, NG, NG, NG, NG, NG, NG, NG,		/* 40-47 */
	52, 53, 54, 55, 56, 57, 58, 59,		/* 48-55 */
	60, 61, NG, NG, NG, NG, NG, NG,		/* 56-63 */

	NG,  0,  1,  2,  3,  4,  5,  6,		/* 64-71 */
	 7,  8,  9, 10, 11, 12, 13, 14,		/* 72-79 */
	15, 16, 17, 18, 19, 20, 21, 22,		/* 80-87 */
	23, 24, 25, NG, NG, NG, NG, 63,		/* 88-95 */

	NG, 26, 27, 28, 29, 30, 31, 32,		/* 96-103 */
	33, 34, 35, 36, 37, 38, 39, 40,		/* 104-111 */
	41, 42, 43, 44, 45, 46, 47, 48,		/* 112-119 */
	49, 50, 51, NG, NG, NG, NG, NG		/* 120-127 */
};

static const unsigned char encode_table[] = {
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
	'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
	'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
	'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
	'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
	'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
	'w', 'x', 'y', 'z', '0', '1', '2', '3',
	'4', '5', '6', '7', '8', '9', '$', '_'
};

symbol_mt
symtab_gensym(void)
{
	static char buf [32] = { 0, }, *phi, *plo;

	assert(symtab_initialized);
	assert(sizeof encode_table == 64);
	if (!phi)
		phi = plo = stpcpy(buf, "gs_A") - 1;	/* both point to 'A' */

	symbol_mt sym = 0;
	while (!sym) {
		if (!hashtab_get(&the_symtab_hash, buf, strlen(buf) + 1))
			sym = symtab_intern(buf);

		/* increment gensym for next time */
		char *p = plo;
		while (1) {
			int d = decode_table[(unsigned char) *p] + 1;
			assert(d > 0 && d <= 64);
			if (d < 64) {
				*p = encode_table[d];
				break;
			}
			*p-- = encode_table[0];
			if (p < phi)
				break;
		}

		/* do we need to increase length? */
		if (p < phi) {
			if (++plo + 1 >= buf + sizeof buf)
				panic("Gensyms exhausted!\n");
			for (p = phi; p <= plo; /* nada */)
				*p++ = encode_table[0];
		}
	}
	return sym;
}

symbol_mt
symtab_intern(const char *name)
{
	if (!symtab_initialized) {
		hashtab_init(&the_symtab_hash, SYMTAB_HASHTAB_HINT);
		wordbuf_init(&the_symtab_buf);
		/*
		 * We can't return symbol_mt == 0; it puns with NULL used
		 * by the hash table to indicate no entry found; reserve
		 * that slot now.
		 */
		wordbuf_pushback(&the_symtab_buf, (word)
			xstrdup("--- INTERNAL ERROR --- INVALID SYMBOL ---"));
		symtab_initialized = 1;	/* must be before recursive call */
		the_empty_symbol = symtab_intern("");
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
	assert(symtab_initialized);
	if (s == 0 || s >= wordbuf_used(&the_symtab_buf))
		panic("Invalid symbol table index!\n");
	return (const char *) wordbuf_at(&the_symtab_buf, s);
}
