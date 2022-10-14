#ifndef LARK_LC_ALLOC_H
#define LARK_LC_ALLOC_H
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

#include <stdbool.h>
#include <stddef.h>

#include <util/circlist.h>

#define ALLOCATOR_DEFAULT_SLOTS 65536	/* should be enough for anyone */

struct term;

struct allocator {
	struct circlist entry;
	struct term **base;
	size_t capacity, used;
	const char *name;
};

extern void allocator_init(struct allocator *alloc, size_t slots);
extern void allocator_fini(struct allocator *alloc);
static inline bool allocator_empty(struct allocator *alloc)
	{ return alloc->used == 0; }
extern struct term *allocator_push(struct allocator *alloc, struct term *term);
extern struct term *allocator_pop(struct allocator *alloc);
extern void allocator_reset(struct allocator *alloc);
extern struct term *allocator_top(struct allocator *alloc);

#endif /* LARK_LC_ALLOC_H */
