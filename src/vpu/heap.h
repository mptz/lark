#ifndef LARK_VPU_HEAP_H
#define LARK_VPU_HEAP_H
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

#include <inttypes.h>
#include <stddef.h>

#include <util/circlist.h>

struct vpu;

/*
 * Heap header and footer objects.  To allocate heap-compatible data
 * outside the heap, you'll need to sandwich your datum between a pair of
 * these.  Both must be word-aligned so behavior will be undefined if the
 * size of the "sandwich filling" is not a multiple of the word size.
 */
struct heap_header {
	uintptr_t hmagic;	/* Magic cookie for integrity checking */
	uintptr_t nwords;	/* Words used including header & footer */
	uintptr_t meta;		/* Metadata incl. embedded pointer info */
	void *data[];		/* User data */
};

struct heap_footer {
	uintptr_t fmagic;	/* Magic cookie for integrity checking */
};

/*
 * Structures to register heap roots.  These structures are caller-
 * allocated and must exist for the lifetime of the registration.
 */
struct heap_root_allocator {
	struct circlist entry;
	void *base; size_t *used;
	const char *name;
};

extern void heap_init(void);
extern void *the_heap_token;		/* featureless heap object */
extern void *heap_alloc_managed_words(size_t nwords);
extern void *heap_alloc_unmanaged_bytes(size_t size);
extern void *heap_alloc_unmanaged_words(size_t nwords);
extern size_t heap_datum_size(const void *datum);
extern void heap_dump(void);
extern void heap_dump_datum(const void *datum);
extern void heap_force_gc(void);
extern size_t heap_header_size(void);
extern size_t heap_perm(const void *datum, void *dst, size_t dstsize);
extern void heap_root_push(void *root);
extern void heap_root_pop(void *root);	/* must match last push */
extern void heap_root_register_allocator(struct heap_root_allocator *roots);
extern void heap_root_deregister_allocator(struct heap_root_allocator *roots);
extern void heap_register_vpu(struct vpu *vpu);
extern void *heap_validate(void *datum);
extern void heap_validate_full(void);

#endif /* LARK_VPU_HEAP_H */
