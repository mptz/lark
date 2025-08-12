/*
 * Copyright (c) 2009-2024 Michael P. Touloumtzis.
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
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

#include "fghw.h"
#include "memutil.h"
#include "message.h"
#include "minmax.h"
#include "wordtab.h"

#define CACHE_LINE_SIZE 64		/* not always, but often */
#define CUCKOO_DEFAULT_SIZE 256

static void
wordtab_alloc(struct wordtab *table)
{
	assert(__builtin_popcountl(table->capacity) == 1);	/* power of 2 */
	if (getrandom(&table->salt, sizeof table->salt, 0) < 0)
		ppanic("getrandom");	/* fresh salt each resize */
	size_t size = sizeof table->bins[0] * table->capacity;
	if (posix_memalign((void**) &table->bins, CACHE_LINE_SIZE, size))
		panic("Virtual memory exhausted (aligned allocation)\n");
	memset(table->bins, 0, size);
	if (table->oob == 0)
		return;
	for (size_t i = 0; i < table->capacity; ++i)
		for (unsigned j = 0; j < WORDTAB_NEST_SIZE; ++j)
			table->bins[i].entries[j].data = table->oob;
}

void
wordtab_init(struct wordtab *table, size_t hint)
{
	table->capacity = CUCKOO_DEFAULT_SIZE;
	if (hint) {
		/* Round to next higher power of 2. */
		hint--;
		hint |= hint >> 1;
		hint |= hint >> 2;
		hint |= hint >> 4;
		hint |= hint >> 8;
		hint |= hint >> 16;
#if UINTPTR_MAX > UINT32_MAX
		hint |= hint >> 32;
#endif
		hint++;
		table->capacity = MAX(table->capacity, hint);
	}
	table->oob = NULL;
	wordtab_alloc(table);
}

void
wordtab_fini(struct wordtab *table)
{
	free(table->bins);
}

void
wordtab_free_all_data(struct wordtab *table)
{
	struct wordtab_iter iter;
	wordtab_iter_init(table, &iter);
	struct wordtab_entry *entry;
	while ((entry = wordtab_iter_next(&iter)))
		free(entry->data);
}

static inline void *
wordtab_find(const struct wordtab *table, word key, uint32_t hash)
{
	const size_t mask = table->capacity - 1;
	const struct cuckoo_bin *bin = table->bins + (hash & mask);
	for (unsigned i = 0; i < WORDTAB_NEST_SIZE; ++i)
		if (bin->entries[i].key == key &&
		    bin->entries[i].data != table->oob)
			return bin->entries[i].data;
	return table->oob;
}

void *
wordtab_get(const struct wordtab *table, word key)
{
	const uint64_t hash = fghws64(key, table->salt);
	void *result =	wordtab_find(table, key, hash);
	return (result != table->oob) ? result :
			wordtab_find(table, key, hash >> 32);
}

void
wordtab_grow(struct wordtab *table)
{
	/*
	 * Allocate a new table.
	 */
	struct cuckoo_bin *tmp = table->bins;
	table->capacity *= 2;
	wordtab_alloc(table);

	/*
	 * Insert each entry from the old table.  Note that this rehashes
	 * everything; we could do this more straightforwardly.
	 */
	for (size_t i = 0; i < table->capacity / 2; ++i) {
		for (unsigned j = 0; j < WORDTAB_NEST_SIZE; ++j) {
			struct wordtab_entry *entry = tmp[i].entries + j;
			if (entry->data != table->oob)
				wordtab_put(table, entry->key, entry->data);
		}
	}
	free(tmp);
}

bool
wordtab_is_empty(struct wordtab *table)
{
	struct wordtab_iter iter;
	wordtab_iter_init(table, &iter);
	return !wordtab_iter_next(&iter);
}

static inline bool
wordtab_update(struct wordtab *table, word key, void *data, uint32_t hash)
{
	const size_t mask = table->capacity - 1;
	struct cuckoo_bin *bin = table->bins + (hash & mask);
	for (unsigned i = 0; i < WORDTAB_NEST_SIZE; ++i)
		if (bin->entries[i].key == key &&
		    bin->entries[i].data != table->oob) {
			bin->entries[i].data = data;
			return true;
		}
	return false;
}

void
wordtab_put(struct wordtab *table, word key, void *data)
{
	/*
	 * Make sure we're not trying to insert the out-of-band value.
	 */
	if (data == table->oob) panic("Tried to insert out-of-band value\n");

	/*
	 * First look for the value; if found, update data and return.
	 * We could have an alternate version of this function which skips
	 * this test when the keys are known unique, as when rehashing.
	 */
	uint64_t hash = fghws64(key, table->salt);
	if (wordtab_update(table, key, data, hash) ||
	    wordtab_update(table, key, data, hash >> 32))
		return;

	/*
	 * We are going to have to add rather than update.
	 */
	size_t eject = 0, mask = table->capacity - 1, pos = hash & mask;
	struct wordtab_entry entry = { .key = key, .data = data };
	for (size_t tries = ceil(log(table->capacity)); tries; --tries) {
		/*
		 * If there's room where we've chosen, insert uneventfully.
		 */
		struct cuckoo_bin *bin = table->bins + pos;
		for (unsigned i = 0; i < WORDTAB_NEST_SIZE; ++i)
			if (bin->entries[i].data == table->oob) {
				bin->entries[i] = entry;
				return;
			}

		/*
		 * Insert anyway, booting the current occupant.  We rotate
		 * which occupant we kick out of the nest; this eliminates
		 * cycling for most hash collisions, and may reduce the
		 * chance of cycling in general (unverified).
		 */
		struct wordtab_entry tmp = bin->entries[eject];
		bin->entries[eject] = entry;
		entry = tmp;
		eject = (eject + 1) % WORDTAB_NEST_SIZE;

		/*
		 * Calculate both hashes for the ejectee; we'll move it
		 * to its other location.  Note that a collision is
		 * possible but should not break anything.  If the entry
		 * doesn't hash to where it's currently stored, it must
		 * have been changed outside this code--while this won't
		 * technically break anything (this code would just try
		 * to insert it according to its current hash value),
		 * it's mostly likely a sign of a bug, so we panic.
		 */
		hash = fghws64(entry.key, table->salt);
		uint32_t h1 = hash & mask, h2 = (hash >> 32) & mask;
		if	(pos == h1) pos = h2;
		else if	(pos == h2) pos = h1;
		else	panic("Table key mutated!\n");
	}

	/*
	 * Table is too crowded.  Grow it and try again.
	 */
	wordtab_grow(table);
	wordtab_put(table, entry.key, entry.data);
}

bool
wordtab_rub(struct wordtab *table, word key)
{
	/*
	 * Update the table, writing the out-of-band value into the
	 * entry for the given key (if it exists), which effectively
	 * removes the key-value pair from the table.  Note this does
	 * not free the value.
	 */
	uint64_t hash = fghws64(key, table->salt);
	return wordtab_update(table, key, table->oob, hash) ||
	       wordtab_update(table, key, table->oob, hash >> 32);
}

void
wordtab_rub_all(struct wordtab *table)
{
	/*
	 * In theory this could resize down, but we currently don't.
	 */
	for (size_t i = 0; i < table->capacity; ++i)
		for (unsigned j = 0; j < WORDTAB_NEST_SIZE; ++j)
			if (table->bins[i].entries[j].data != table->oob)
				table->bins[i].entries[j].data = table->oob;
}

void
wordtab_set_oob(struct wordtab *table, void *oob)
{
	void *old = table->oob;
	for (size_t i = 0; i < table->capacity; ++i)
		for (unsigned j = 0; j < WORDTAB_NEST_SIZE; ++j)
			if (table->bins[i].entries[j].data == old)
				table->bins[i].entries[j].data = oob;
	table->oob = oob;
}

void
wordtab_stats(struct wordtab *table, struct wordtab_stats *stats)
{
	/*
	 * XXX confusing... what we call 'capacity' within the
	 * table is actually the number of bins.  Should rename.
	 */
	memset(stats, 0, sizeof *stats);
	stats->capacity = table->capacity * WORDTAB_NEST_SIZE;
	stats->bins = table->capacity;
	for (size_t i = 0; i < table->capacity; ++i) {
		unsigned binused = 0;
		for (unsigned j = 0; j < WORDTAB_NEST_SIZE; ++j)
			if (table->bins[i].entries[j].data != table->oob) {
				binused = 1;
				++stats->used;
				++stats->entry_used[j];
			}
		stats->binsused += binused;
	}
}

void
wordtab_iter_init(const struct wordtab *wordtab, struct wordtab_iter *iter)
{
	iter->wordtab = wordtab;
	iter->bin = 0;
	iter->entry = 0;
}

struct wordtab_entry *
wordtab_iter_next(struct wordtab_iter *iter)
{
	const struct wordtab *tab = iter->wordtab;
	while (iter->bin < tab->capacity) {
		while (iter->entry < WORDTAB_NEST_SIZE) {
			struct wordtab_entry *entry =
				tab->bins[iter->bin].entries + iter->entry++;
			if (entry->data != tab->oob)
				return entry;
		}
		iter->bin++;
		iter->entry = 0;
	}
	return NULL;
}
