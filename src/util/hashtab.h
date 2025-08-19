#ifndef LARK_UTIL_HASHTAB_H
#define LARK_UTIL_HASHTAB_H
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

#include <stddef.h>
#include <stdint.h>

#define HASHTAB_NEST_SIZE 4

struct hashtab_entry {
	const void *key;
	size_t keysize;
	void *data;
};

struct hashtab_nest {
	struct hashtab_entry entries [HASHTAB_NEST_SIZE];
};

struct hashtab {
	uint64_t salt;
	void *oob;
	size_t nnests;
	struct hashtab_nest *nests;
};

struct hashtab_iter {
	const struct hashtab *hashtab;
	size_t nest;
	unsigned entry;
};

struct hashtab_stats {
	size_t capacity, used, nests, nestsused,
	       entry_used [HASHTAB_NEST_SIZE];
};

extern void hashtab_init(struct hashtab *table, size_t hint);
extern void hashtab_fini(struct hashtab *table);
extern void hashtab_free_all_keys(struct hashtab *table);
extern void hashtab_free_all_data(struct hashtab *table);
extern void hashtab_free_all(struct hashtab *table);
extern void *hashtab_get(const struct hashtab *table, const void *key,
			 size_t keysize);
extern void hashtab_put(struct hashtab *table, const void *key,
			size_t keysize, void *data);
extern int hashtab_set_oob(struct hashtab *table, void *oob);
extern void hashtab_stats(const struct hashtab *table,
			  struct hashtab_stats *stats);
extern void hashtab_iter_init(const struct hashtab *table,
			      struct hashtab_iter *iter);
extern struct hashtab_entry *hashtab_iter_next(struct hashtab_iter *iter);

#endif /* LARK_UTIL_HASHTAB_H */
