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

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

#include "fgh.h"
#include "hashtab.h"
#include "memutil.h"
#include "message.h"
#include "minmax.h"

#define CACHE_LINE_SIZE	64		/* not always, but often */
#define HASHTAB_DEFAULT_NESTS 32	/* capacity == #nests * nest size */

static void
hashtab_alloc(struct hashtab *table)
{
	assert(__builtin_popcountl(table->nnests) == 1); 	/* power of 2 */
	if (getrandom(&table->salt, sizeof table->salt, 0) < 0)
		ppanic("getrandom");	/* fresh salt each resize */
	size_t size = table->nnests * sizeof table->nests[0];
	if (posix_memalign((void**) &table->nests, CACHE_LINE_SIZE, size))
		panic("Virtual memory exhausted (aligned allocation)\n");
	memset(table->nests, 0, size);
	if (table->oob == 0)
		return;
	for (size_t i = 0; i < table->nnests; ++i)
		for (unsigned j = 0; j < HASHTAB_NEST_SIZE; ++j)
			table->nests[i].entries[j].data = table->oob;
}

void
hashtab_init(struct hashtab *table, size_t hint)
{
	table->nnests = HASHTAB_DEFAULT_NESTS;
	/* convert capacity hint to reasonable # nests */
	hint = hint * 2 / HASHTAB_NEST_SIZE;
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
		table->nnests = MAX(table->nnests, hint);
	}
	table->oob = NULL;
	hashtab_alloc(table);
}

void
hashtab_fini(struct hashtab *table)
{
	free(table->nests);
}

void
hashtab_free_all_keys(struct hashtab *table)
{
	struct hashtab_iter iter;
	hashtab_iter_init(table, &iter);
	struct hashtab_entry *entry;
	while ((entry = hashtab_iter_next(&iter)))
		xfree(entry->key);	/* xfree b/c key is const */
}

void
hashtab_free_all_data(struct hashtab *table)
{
	struct hashtab_iter iter;
	hashtab_iter_init(table, &iter);
	struct hashtab_entry *entry;
	while ((entry = hashtab_iter_next(&iter)))
		free(entry->data);
}

void
hashtab_free_all(struct hashtab *table)
{
	struct hashtab_iter iter;
	hashtab_iter_init(table, &iter);
	struct hashtab_entry *entry;
	while ((entry = hashtab_iter_next(&iter)))
		free((void*) entry->key), free(entry->data);
}

static inline void *
hashtab_find(const struct hashtab *table, const void *key,
	     size_t keysize, uint32_t hash)
{
	const size_t mask = table->nnests - 1;
	const struct hashtab_nest *nest = table->nests + (hash & mask);
	for (unsigned i = 0; i < HASHTAB_NEST_SIZE; ++i)
		if (nest->entries[i].keysize == keysize &&
		    !memcmp(nest->entries[i].key, key, keysize) &&
		    nest->entries[i].data != table->oob)
			return nest->entries[i].data;
	return table->oob;
}

void *
hashtab_get(const struct hashtab *table, const void *key,
	    size_t keysize)
{
	const uint64_t hash = fghs64(key, keysize, table->salt);
	void *result = hashtab_find(table, key, keysize, hash);
	return (result != table->oob) ? result :
			hashtab_find(table, key, keysize, hash >> 32);
}

static void
hashtab_grow(struct hashtab *table)
{
	/*
	 * Allocate a new table.
	 */
	struct hashtab_nest *tmp = table->nests;
	table->nnests *= 2;
	hashtab_alloc(table);

	/*
	 * Insert each entry from the old table.  Note that this rehashes
	 * everything; we could do this more straightforwardly.
	 */
	for (size_t i = 0; i < table->nnests / 2; ++i) {
		for (unsigned j = 0; j < HASHTAB_NEST_SIZE; ++j) {
			struct hashtab_entry *entry = tmp[i].entries + j;
			if (entry->data != table->oob)
				hashtab_put(table, entry->key,
					    entry->keysize, entry->data);
		}
	}
	free(tmp);
}

static inline bool
hashtab_update(struct hashtab *table, const void *key, size_t keysize,
	       void *data, uint32_t hash)
{
	const size_t mask = table->nnests - 1;
	struct hashtab_nest *nest = table->nests + (hash & mask);
	for (unsigned i = 0; i < HASHTAB_NEST_SIZE; ++i)
		if (nest->entries[i].keysize == keysize &&
		    !memcmp(nest->entries[i].key, key, keysize) &&
		    nest->entries[i].data != table->oob) {
			nest->entries[i].data = data;
			return true;
		}
	return false;
}

void
hashtab_put(struct hashtab *table, const void *key,
	    size_t keysize, void *data)
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
	uint64_t hash = fghs64(key, keysize, table->salt);
	if (hashtab_update(table, key, keysize, data, hash) ||
	    hashtab_update(table, key, keysize, data, hash >> 32))
		return;

	/*
	 * We are going to have to add rather than update.
	 */
	const size_t mask = table->nnests - 1;
	size_t eject = 0, pos = hash & mask;
	struct hashtab_entry entry = { .key = key, .keysize = keysize,
				       .data = data };
	for (size_t tries = ceil(log(table->nnests)); tries; --tries) {
		/*
		 * If there's room where we've chosen, insert uneventfully.
		 */
		struct hashtab_nest *nest = table->nests + pos;
		for (unsigned i = 0; i < HASHTAB_NEST_SIZE; ++i)
			if (nest->entries[i].data == table->oob) {
				nest->entries[i] = entry;
				return;
			}

		/*
		 * Insert anyway, booting the current occupant.  We rotate
		 * which occupant we kick out of the nest; this eliminates
		 * cycling for most hash collisions, and may reduce the
		 * chance of cycling in general (unverified).
		 */
		struct hashtab_entry tmp = nest->entries[eject];
		nest->entries[eject] = entry;
		entry = tmp;
		eject = (eject + 1) % HASHTAB_NEST_SIZE;

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
		hash = fghs64(entry.key, entry.keysize, table->salt);
		uint32_t h1 = hash & mask, h2 = (hash >> 32) & mask;
		if	(pos == h1) pos = h2;
		else if (pos == h2) pos = h1;
		else	panic("Table key mutated!\n");
	}

	/*
	 * Table is too crowded.  Grow it and try again.
	 */
	hashtab_grow(table);
	hashtab_put(table, entry.key, entry.keysize, entry.data);
}

int
hashtab_set_oob(struct hashtab *table, void *oob)
{
	if (oob == table->oob)
		return 0;

	/* if new oob value is in use as data, return an error */
	for (size_t i = 0; i < table->nnests; ++i)
		for (unsigned j = 0; j < HASHTAB_NEST_SIZE; ++j)
			if (table->nests[i].entries[j].data == oob)
				return -1;

	void *old = table->oob;
	for (size_t i = 0; i < table->nnests; ++i)
		for (unsigned j = 0; j < HASHTAB_NEST_SIZE; ++j)
			if (table->nests[i].entries[j].data == old)
				table->nests[i].entries[j].data = oob;
	table->oob = oob;
	return 0;
}

void
hashtab_stats(const struct hashtab *table, struct hashtab_stats *stats)
{
	memset(stats, 0, sizeof *stats);
	stats->capacity = table->nnests * HASHTAB_NEST_SIZE;
	stats->nests = table->nnests;
	for (size_t i = 0; i < table->nnests; ++i) {
		unsigned nestused = 0;
		for (unsigned j = 0; j < HASHTAB_NEST_SIZE; ++j)
			if (table->nests[i].entries[j].data != table->oob) {
				nestused = 1;
				++stats->used;
				++stats->entry_used[j];
			}
		stats->nestsused += nestused;
	}
}

void
hashtab_iter_init(const struct hashtab *hashtab, struct hashtab_iter *iter)
{
	iter->hashtab = hashtab;
	iter->nest = 0;
	iter->entry = 0;
}

struct hashtab_entry *
hashtab_iter_next(struct hashtab_iter *iter)
{
	const struct hashtab *tab = iter->hashtab;
	while (iter->nest < tab->nnests) {
		while (iter->entry < HASHTAB_NEST_SIZE) {
			struct hashtab_entry *entry =
				tab->nests[iter->nest].entries + iter->entry++;
			if (entry->data != tab->oob)
				return entry;
		}
		iter->nest++;
		iter->entry = 0;
	}
	return NULL;
}
