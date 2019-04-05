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

#include <util/bytebuf.h>
#include <util/hashtab.h>
#include <util/memutil.h>
#include <util/message.h>
#include <util/word.h>

#include "heap.h"
#include "pool.h"

/*
 * Currently we track only one section (with read-only data), globally.
 * At some point we'll at least want to add ro/rw separation, possibly
 * arbitrary numbers of each, and zeroed sections as well (similar to
 * bss, but probably both ro & rw flavors).
 */
static struct bytebuf ro_pool;

/*
 * It may not be necessary to use separate hash tables to pool separate
 * data types if all types have sufficiently compatible representations.
 */
static struct hashtab intpooltab;
static struct hashtab natpooltab;
static struct hashtab strpooltab;

void pool_init(void)
{
	bytebuf_init(&ro_pool);
	hashtab_init(&intpooltab, 0);
	hashtab_init(&natpooltab, 0);
	hashtab_init(&strpooltab, 0);
}

static size_t pool_add(struct hashtab *hashtab, const void *p, size_t sz)
{
	/*
	 * First try to find the literal in the pool.  0 is a safe
	 * out-of-band value because this address is the address of
	 * the actual data, and it's preceded by a heap header.  If
	 * that header weren't there, we'd have to add a shim to our
	 * buffer or switch the table to a different OOB value.
	 */
	size_t addr = (size_t) hashtab_get(hashtab, p, sz);
	if (addr) {
		assert(WORD_ALIGNED(addr));
		assert(addr < bytebuf_used(&ro_pool));
		return addr;
	}

	/*
	 * This gets a little tricky because we're not just copying the
	 * value into the literal pool buffer, we're copying a whole
	 * heap object including header and footer.  We also can't tell
	 * the allocated size of an object by inspecting its contents,
	 * since our bignum routines sometimes overallocate for safety.
	 * Luckily the heap can tell us the true size of a datum.
	 */
	size_t pos = bytebuf_used(&ro_pool);
	addr = pos + heap_header_size();
	assert(WORD_ALIGNED(addr));
	size_t needed = heap_datum_size(p);
	bytebuf_grow(&ro_pool, needed);
	assert(bytebuf_used(&ro_pool) >= pos + needed);
	assert(WORD_ALIGNED(bytebuf_used(&ro_pool)));
	heap_perm(p, ro_pool.data + pos, needed);

	/*
	 * Add this value to the appropriate hash table.  We duplicate
	 * the object (used as hash table key) on the way in: if we use
	 * the value we're given as an argument, it'll be clobbered by
	 * the next GC run.  If we try pointing directly into the read-
	 * only pool, it'll be clobbered if the RO pool bytebuf resizes
	 * upwards.  Ask me how I know.
	 */
	hashtab_put(hashtab, xmemdup(p, sz), sz, (void*) addr);

	return addr;
}

size_t pool_int(int_mt z) { return pool_add(&intpooltab, z, intrepsize(z)); }
size_t pool_nat(nat_mt n) { return pool_add(&natpooltab, n, natrepsize(n)); }
size_t pool_str(str_mt s) { return pool_add(&strpooltab, s, strrepsize(s)); }
void *pool_base(void)	{ return ro_pool.data; }
size_t pool_size(void)	{ return bytebuf_used(&ro_pool); }
