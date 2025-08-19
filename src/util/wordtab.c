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
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

#include "fghw.h"
#include "memutil.h"
#include "message.h"
#include "minmax.h"
#include "wordtab.h"

#define CACHE_LINE_SIZE 64		/* not always, but often */
#define WORDTAB_DEFAULT_NNESTS 256

static void
wordtab_alloc(struct wordtab *table)
{
	assert(__builtin_popcountl(table->nnests) == 1);	/* power of 2 */
	if (getrandom(&table->salt, sizeof table->salt, 0) < 0)
		ppanic("getrandom");	/* fresh salt each resize */
	size_t size = sizeof table->nests[0] * table->nnests;
	if (posix_memalign((void**) &table->nests, CACHE_LINE_SIZE, size))
		panic("Virtual memory exhausted (aligned allocation)\n");
	memset(table->nests, 0, size);
	if (table->oob == 0)
		return;
	for (size_t i = 0; i < table->nnests; ++i)
		for (unsigned j = 0; j < WORDTAB_NEST_SIZE; ++j)
			table->nests[i].entries[j].data = table->oob;
}

void
wordtab_init(struct wordtab *table, size_t hint)
{
	table->nnests = WORDTAB_DEFAULT_NNESTS;
	/* convert capacity hint to reasonable # nests */
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
	wordtab_alloc(table);
}

void
wordtab_fini(struct wordtab *table)
{
	free(table->nests);
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
	const size_t mask = table->nnests - 1;
	const struct wordtab_nest *nest = table->nests + (hash & mask);
	for (unsigned i = 0; i < WORDTAB_NEST_SIZE; ++i)
		if (nest->entries[i].key == key &&
		    nest->entries[i].data != table->oob)
			return nest->entries[i].data;
	return table->oob;
}

void *
wordtab_get(const struct wordtab *table, word key)
{
	const uint64_t hash = fghws64(key, table->salt);
	void *result = wordtab_find(table, key, hash);
	return (result != table->oob) ? result :
			wordtab_find(table, key, hash >> 32);
}

static void
wordtab_grow(struct wordtab *table)
{
	/*
	 * Allocate a new table.
	 */
	struct wordtab_nest *tmp = table->nests;
	table->nnests *= 2;
	wordtab_alloc(table);

	/*
	 * Insert each entry from the old table.  Note that this rehashes
	 * everything; we could do this more straightforwardly.
	 */
	for (size_t i = 0; i < table->nnests / 2; ++i) {
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
	const size_t mask = table->nnests - 1;
	struct wordtab_nest *nest = table->nests + (hash & mask);
	for (unsigned i = 0; i < WORDTAB_NEST_SIZE; ++i)
		if (nest->entries[i].key == key &&
		    nest->entries[i].data != table->oob) {
			nest->entries[i].data = data;
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
	const size_t mask = table->nnests - 1;
	size_t eject = 0, pos = hash & mask;
	struct wordtab_entry entry = { .key = key, .data = data };
	for (size_t tries = ceil(log(table->nnests)); tries; --tries) {
		/*
		 * If there's room where we've chosen, insert uneventfully.
		 */
		struct wordtab_nest *nest = table->nests + pos;
		for (unsigned i = 0; i < WORDTAB_NEST_SIZE; ++i)
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
		struct wordtab_entry tmp = nest->entries[eject];
		nest->entries[eject] = entry;
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
	for (size_t i = 0; i < table->nnests; ++i)
		for (unsigned j = 0; j < WORDTAB_NEST_SIZE; ++j)
			if (table->nests[i].entries[j].data != table->oob)
				table->nests[i].entries[j].data = table->oob;
}

int
wordtab_set_oob(struct wordtab *table, void *oob)
{
	if (oob == table->oob)
		return 0;

	/* if new oob value is in use as data, return an error */
	for (size_t i = 0; i < table->nnests; ++i)
		for (unsigned j = 0; j < WORDTAB_NEST_SIZE; ++j)
			if (table->nests[i].entries[j].data == oob)
				return -1;

	void *old = table->oob;
	for (size_t i = 0; i < table->nnests; ++i)
		for (unsigned j = 0; j < WORDTAB_NEST_SIZE; ++j)
			if (table->nests[i].entries[j].data == old)
				table->nests[i].entries[j].data = oob;
	table->oob = oob;
	return 0;
}

void
wordtab_stats(const struct wordtab *table, struct wordtab_stats *stats)
{
	memset(stats, 0, sizeof *stats);
	stats->capacity = table->nnests * WORDTAB_NEST_SIZE;
	stats->nests = table->nnests;
	for (size_t i = 0; i < table->nnests; ++i) {
		unsigned nestused = 0;
		for (unsigned j = 0; j < WORDTAB_NEST_SIZE; ++j)
			if (table->nests[i].entries[j].data != table->oob) {
				nestused = 1;
				++stats->used;
				++stats->entry_used[j];
			}
		stats->nestsused += nestused;
	}
}

void
wordtab_iter_init(const struct wordtab *wordtab, struct wordtab_iter *iter)
{
	iter->wordtab = wordtab;
	iter->nest = 0;
	iter->entry = 0;
}

struct wordtab_entry *
wordtab_iter_next(struct wordtab_iter *iter)
{
	const struct wordtab *tab = iter->wordtab;
	while (iter->nest < tab->nnests) {
		while (iter->entry < WORDTAB_NEST_SIZE) {
			struct wordtab_entry *entry =
				tab->nests[iter->nest].entries + iter->entry++;
			if (entry->data != tab->oob)
				return entry;
		}
		iter->nest++;
		iter->entry = 0;
	}
	return NULL;
}
