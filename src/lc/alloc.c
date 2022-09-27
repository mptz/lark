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
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include <util/memutil.h>
#include <util/message.h>

#include "alloc.h"
#include "heap.h"

void
allocator_init(struct allocator *alloc, size_t slots)
{
	alloc->base = xmalloc(sizeof alloc->base[0] * slots);
	alloc->capacity = slots;
	alloc->used = 0;
	heap_allocator_register(alloc);
}

void
allocator_fini(struct allocator *alloc)
{
	heap_allocator_deregister(alloc);
	free(alloc->base);
	alloc->capacity = 0;
}

struct term *
allocator_push(struct allocator *alloc, struct term *term)
{
	if (alloc->used >= alloc->capacity)
		panicf("Allocator at capacity: %s\n", alloc->name);
	alloc->base[alloc->used++] = term;
	return term;
}

struct term *
allocator_pop(struct allocator *alloc)
{
	if (!alloc->used)
		panicf("Allocator popped while empty: %s\n", alloc->name);
	return alloc->base[--alloc->used];
}

void
allocator_reset(struct allocator *alloc)
{
	alloc->used = 0;
}

struct term *
allocator_top(struct allocator *alloc)
{
	if (!alloc->used)
		panicf("Allocator top accessed while empty: %s\n", alloc->name);
	return alloc->base[alloc->used - 1];
}
