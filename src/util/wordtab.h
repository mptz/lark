#ifndef LARK_UTIL_WORDTAB_H
#define LARK_UTIL_WORDTAB_H
/*
 * Copyright (c) 2009-2018 Michael P. Touloumtzis.
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

#include <stdbool.h>
#include <stddef.h>

#include "word.h"

#define CUCKOO_NEST_SIZE 4

struct wordtab_entry {
	word key;
	void *data;
};

struct cuckoo_bin {
	struct wordtab_entry entries [CUCKOO_NEST_SIZE];
};

struct wordtab {
	size_t capacity;
	void *oob;
	struct cuckoo_bin *bins;
};

struct wordtab_iter {
	struct wordtab *wordtab;
	size_t bin;
	unsigned entry;
};

struct wordtab_stats {
	size_t capacity, used, bins, binsused,
	       entry_used [CUCKOO_NEST_SIZE];
};

extern void wordtab_init(struct wordtab *table, size_t hint);
extern void wordtab_fini(struct wordtab *table);
extern void wordtab_free_all_data(struct wordtab *table);
extern void *wordtab_get(const struct wordtab *table, word key);
extern void wordtab_put(struct wordtab *table, word key, void *data);
extern bool wordtab_rub(struct wordtab *table, word key);
extern void wordtab_set_oob(struct wordtab *table, void *oob);
extern void wordtab_stats(struct wordtab *table, struct wordtab_stats *stats);
extern void wordtab_iter_init(struct wordtab *table, struct wordtab_iter *iter);
extern struct wordtab_entry *wordtab_iter_next(struct wordtab_iter *iter);

#endif /* LARK_UTIL_WORDTAB_H */
