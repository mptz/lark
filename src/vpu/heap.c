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
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <util/message.h>

#include "heap.h"
#include "vpu.h"

/*
 * At some point the heap will become resizeable but for now this keeps
 * things nice and simple.
 */
#define HEAPWORDS 10000000

/*
 * Heap magic cookies used to detect memory corruption.
 */
#define HH_MAGIC	0xDEADBEEF
#define HF_MAGIC	((void*) 0xFEEDCAFE)

/*
 * Heap header metadata.
 */
#define HH_LOCBITS	2	/* block location vis a vis heap */
#define HH_LOCMASK	0x3	/* corresponding mask */
#define HH_OUTSIDE	0	/* this block remains outside the heap */
#define HH_COPY_IN	1	/* copy in if possible during GC */
#define HH_COPY_OUT	2	/* copy out during GC */
#define HH_INSIDE	3	/* lives & remains in heap */

#define HH_PTRBITS	2	/* permits up to 8 header types; 4 in use */
#define HH_PTRMASK	0xC	/* corresponding mask */
#define HH_PTRFREE	0	/* heap-allocated block, no heap pointers */
#define HH_PTRFULL	4	/* heap-allocated block of pointers to heap */
#define HH_PTRMIX	8	/* heap-allocated, use bits to distinguish */

#define HH_METABITS	4	/* total number of bits used above */
#define HH_METAMASK	0xF	/* mask for all of above */

/*
 * During GC, we need to forward a heap object into the to-space.  We do this
 * by writing the forwarding address in data[0], and marking the header as
 * forwarded by setting the size to 0.  Both imply that we can't allocate a
 * 0-word object, but there would be no need to do so in the movable heap
 * anyway--we could always use a reference to a singleton, permanent value.
 */

/*
 * A token heap-managed object which can be used as a placeholder (null
 * value) in heap-managed registers.
 */
static struct heap_token {
	struct heap_header header;
	word token;		/* Heap doesn't support 0-length objects */
	struct heap_footer footer;
} the_heap_token_object;
void *the_heap_token = &the_heap_token_object.token;

#define WORDBYTES (sizeof (uintptr_t))
#define HEADERBYTES (sizeof (struct heap_header))
#define HEADERWORDS ((HEADERBYTES + WORDBYTES - 1) / WORDBYTES)
#define FOOTERBYTES (sizeof (struct heap_footer))
#define FOOTERWORDS ((FOOTERBYTES + WORDBYTES - 1) / WORDBYTES)
#define EXTRABYTES (HEADERBYTES + FOOTERBYTES)
#define EXTRAWORDS (HEADERWORDS + FOOTERWORDS)

static uintptr_t the_heap_a [HEAPWORDS],	/* A space */
		 the_inter_space_pad,		/* fixes in_X_space boundary
						   condition misdetection */
		 the_heap_b [HEAPWORDS],	/* B space */
		 *the_heap = the_heap_a, *the_heap_bound;

#define HWORDS (sizeof the_heap_a / sizeof the_heap_a[0])

/*
 * Roots of the heap.  We offer both a basic LIFO way to add & remove roots,
 * as well as an interface to register and deregister roots without adhering
 * to LIFO discipline.  Finally, we track a single VM stack.
 *
 * XXX should update the above comments... we have an API to register a
 * VPU (which provides access to its full register set and stack) and need
 * to determine how many other items need to be registered... though an
 * ad-hoc registration mechanism will still be necessary.
 *
 * A root can be a single pointer, a fixed-size pointer array (unimplemented),
 * or a growable pointer array, in which case we require both the base
 * address and the address of the current-value pointer.
 */
#define HEAPROOTS 64
static void *the_root_stack [HEAPROOTS];
static size_t root_stack_next;
static struct circlist the_roots_sentinel;
static struct circlist the_vpu_sentinel;

/*
 * A cycle counter to keep track of when we run GC.  Could be used for
 * profiling, but right now is just useful info in the heap dump.
 */
static unsigned the_gc_cycle;

/*
 * Are we currently in a GC cycle?  If so our validity checks have to
 * be a little bit more permissive.
 */
static unsigned during_gc;

static inline bool in_a_space(const uintptr_t *p)
	{ return p >= the_heap_a && p <= the_heap_a + HWORDS; }
static inline bool in_b_space(const uintptr_t *p)
	{ return p >= the_heap_b && p <= the_heap_b + HWORDS; }
static inline uintptr_t * heap_base(const uintptr_t *p)
	{ return in_a_space(p) ? the_heap_a : the_heap_b; }

static void heap_gc(void);

void
heap_init(void)
{
	assert(HEADERWORDS == 3);
	assert(EXTRAWORDS == 4);
	assert(sizeof the_heap_a == sizeof the_heap_b);
	(void) the_inter_space_pad;	/* avoids unused warning */
	the_heap_bound = the_heap + HWORDS;
	circlist_init(&the_roots_sentinel);
	circlist_init(&the_vpu_sentinel);

	/* initialize token object */
	the_heap_token_object.header.hmagic = HH_MAGIC;
	the_heap_token_object.header.nwords = EXTRAWORDS + 1;
	the_heap_token_object.header.meta = HH_OUTSIDE | HH_PTRFREE;
	the_heap_token_object.token = 42;
	the_heap_token_object.footer.fmagic = (word) HF_MAGIC;
}

static void *
heap_alloc(uintptr_t nwords, uintptr_t meta)
{
	if (!nwords)
		panic("Can't allocate a zero-sized block!\n");
	struct heap_header *header = (struct heap_header*) the_heap;
	nwords += EXTRAWORDS;	/* overhead for heap header & footer */
	if (nwords > HEAPWORDS)
		panic("Allocation larger than heap\n");	/* XXX not needed? */
	if ((the_heap += nwords) > the_heap_bound) {
		the_heap -= nwords;
		heap_gc();
		header = (struct heap_header*) the_heap;
		if ((the_heap += nwords) > the_heap_bound)
			panicf("Heap exhausted; can't allocate "
				"%zu (%zu + %zu) word(s)\n",
				nwords, nwords - EXTRAWORDS, EXTRAWORDS);
	}
	header->hmagic = HH_MAGIC;
	header->data[nwords - EXTRAWORDS] = HF_MAGIC;
	header->nwords = nwords;
	header->meta = meta;
	return header->data;
}

void *
heap_alloc_managed_words(size_t nwords)
{
	return heap_alloc(nwords, HH_INSIDE | HH_PTRFULL);
}

void *
heap_alloc_unmanaged_bytes(size_t size)
{
	return heap_alloc((size + WORDBYTES - 1) / WORDBYTES,
			  HH_INSIDE | HH_PTRFREE);
}

void *
heap_alloc_unmanaged_words(size_t nwords)
{
	return heap_alloc(nwords, HH_INSIDE | HH_PTRFREE);
}

size_t
heap_datum_size(const void *datum)
{
	assert(datum);
	const struct heap_header *header = datum;
	--header;	/* Offset from stored data to heap header */
	return header->nwords * WORDBYTES;
}

void
heap_dump(void)
{
	uintptr_t *base = heap_base(the_heap);
	fprintf(stderr, "Heap using %c space, cycle: %u\n"
			"Words used: %"PRIuPTR", free: %"PRIuPTR"\n"
			"Bound roots: %zu, free: %zu\n",
		(base == the_heap_a) ? 'A' : 'B', the_gc_cycle,
		the_heap - base, the_heap_bound - the_heap,
		root_stack_next, HEAPROOTS - root_stack_next);

	struct circlist_iter roots_iter;
	circlist_iter_init(&the_roots_sentinel, &roots_iter);
	const struct heap_root_allocator *entry;
	while ((entry = (const struct heap_root_allocator *)
			circlist_iter_next(&roots_iter))) {
		fprintf(stderr, "Root: %s\n", entry->name);
	}
}

void
heap_dump_datum(const void *datum)
{
	assert(datum);
	const struct heap_header *header = datum;
	--header;	/* Offset from stored data to heap header */
	fprintf(stderr, "Heap header at 0x%08"PRIXPTR" (datum 0x%08"PRIXPTR")\n"
			"nwords: %zu\n" "meta: %zu\n"
			"hmagic: %08"PRIXPTR"\n" "fmagic: %08"PRIXPTR"\n",
		(uintptr_t) header, (uintptr_t) datum,
		header->nwords, header->meta, header->hmagic,
		(uintptr_t) header->data[header->nwords - EXTRAWORDS]);
	fflush(stderr);
}

void
heap_force_gc(void)
{
	heap_gc();
}

/*
 * The given reference is the address of a pointer into the heap, i.e. it's
 * the address of an address.  When we dereference the outer address, we
 * should find a valid pointer into the heap, from which we can determine
 * the size of the pointed-at block.  We move the block to the destination
 * address and update the given reference to point to the block's new home.
 */
static size_t
heap_gc_move(void **ref, void *tospace)
{
	heap_validate(*ref);
	struct heap_header *src = (struct heap_header *) *ref;
	--src;	/* Offset from stored data to heap header */

	if ((src->meta & HH_LOCMASK) == HH_OUTSIDE) {
		/*
		 * A non-heap-managed block such as a constant literal;
		 * we're not allowed to move it, and it's not allowed to
		 * point into the heap.
		 */
		return 0;
	}
	if ((src->meta & HH_LOCMASK) != HH_INSIDE)
		panic("Copy in/out not yet supported!\n");
	if (src->nwords == 0) {
		/*
		 * This data has already been forwarded.  Relocate the
		 * reference to point to the forwarded address, but there's
		 * no need to copy anything.
		 */
		*ref = src->data[0];
		heap_validate(*ref);
		return 0;
	}

	memcpy(tospace, src, src->nwords * WORDBYTES);
	struct heap_header *dst = tospace;
	++dst;	/* Offset from heap header to stored data */
	*ref = (void*) dst;		/* Point reference at moved block */
	src->data[0] = dst;		/* Leave forwarding pointer */
	heap_validate(*ref);
	heap_validate(src->data[0]);
	size_t tmp = src->nwords;	/* We're going to clobber this */
	src->nwords = 0;		/* Mark source as forwarded */
	return tmp;
}

static void *
heap_gc_vpu(struct vpu *vpu, uintptr_t *dst)
{
	infof("Copying VPU registers for '%s'...\n", vpu->name);
	if (vpu->mm & 0x0001) dst += heap_gc_move((void**) &vpu->r0, dst);
	if (vpu->mm & 0x0002) dst += heap_gc_move((void**) &vpu->r1, dst);
	if (vpu->mm & 0x0004) dst += heap_gc_move((void**) &vpu->r2, dst);
	if (vpu->mm & 0x0008) dst += heap_gc_move((void**) &vpu->r3, dst);
	if (vpu->mm & 0x0010) dst += heap_gc_move((void**) &vpu->r4, dst);
	if (vpu->mm & 0x0020) dst += heap_gc_move((void**) &vpu->r5, dst);
	if (vpu->mm & 0x0040) dst += heap_gc_move((void**) &vpu->r6, dst);
	if (vpu->mm & 0x0080) dst += heap_gc_move((void**) &vpu->r7, dst);
	if (vpu->mm & 0x0100) dst += heap_gc_move((void**) &vpu->r8, dst);
	if (vpu->mm & 0x0200) dst += heap_gc_move((void**) &vpu->r9, dst);
	if (vpu->mm & 0x0400) dst += heap_gc_move((void**) &vpu->rA, dst);
	if (vpu->mm & 0x0800) dst += heap_gc_move((void**) &vpu->rB, dst);
	if (vpu->mm & 0x1000) dst += heap_gc_move((void**) &vpu->rC, dst);
	if (vpu->mm & 0x2000) dst += heap_gc_move((void**) &vpu->rD, dst);
	if (vpu->mm & 0x4000) dst += heap_gc_move((void**) &vpu->rE, dst);
	if (vpu->mm & 0x8000) dst += heap_gc_move((void**) &vpu->rF, dst);
	dst += heap_gc_move(&vpu->h0, dst);
	dst += heap_gc_move(&vpu->h1, dst);
	dst += heap_gc_move(&vpu->h2, dst);
	dst += heap_gc_move(&vpu->h3, dst);
	dst += heap_gc_move(&vpu->h4, dst);
	dst += heap_gc_move(&vpu->h5, dst);
	dst += heap_gc_move(&vpu->h6, dst);
	dst += heap_gc_move(&vpu->h7, dst);
	return dst;
}

static inline uintptr_t *
heap_gc_ptrmap(struct heap_header *header, uintptr_t *dst)
{
	uintptr_t i, map;
	for (i = 0, map = header->meta >> HH_METABITS; map; ++i, map >>= 1) {
		if (map & 1)
			dst += heap_gc_move(header->data + i, dst);
	}
	return dst;
}

static void
heap_gc(void)
{
	struct timespec timespec;
	static double timep = 0.0;
	double time0, time1;

	/* Track GC invocations for diagnostics */
	++the_gc_cycle;
	/* Use CLOCK_MONOTONIC_COARSE once we care about performance */
	clock_gettime(CLOCK_MONOTONIC, &timespec);
	time0 = (double) timespec.tv_sec +
		((double) timespec.tv_nsec / 1000000000.0);

	infof("GC start, cycle %u...\n", the_gc_cycle);
	during_gc = 1;
	info("GC prevalidation starting...\n");
	heap_validate_full();
	info("GC prevalidation complete\n");

	/* Determine source and target space for copy */
	uintptr_t *srcbase = heap_base(the_heap),
		  *dstbase = (srcbase == the_heap_a ? the_heap_b : the_heap_a),
		  *dstcurr = dstbase,
		  *dstbound = dstbase + HWORDS;

	/* Copy root stack; these are allowed to be NULL */
	size_t i;
	info("Copying root stack...\n");
	for (i = 0; i < root_stack_next; ++i)
		if (*(void **) the_root_stack[i])
			dstcurr += heap_gc_move(the_root_stack[i], dstcurr);
	info("Root stack copy complete\n");

	/* Copy registered root allocators */
	struct circlist_iter roots_iter;
	circlist_iter_init(&the_roots_sentinel, &roots_iter);
	const struct heap_root_allocator *entry;
	info("Copying registered allocators...\n");
	while ((entry = (const struct heap_root_allocator *)
			circlist_iter_next(&roots_iter))) {
		void **base = entry->base;
		for (i = 0; i < *entry->used; ++i)
			dstcurr += heap_gc_move(base + i, dstcurr);
	}
	info("Registered allocator copy complete\n");

	/* Copy VM stack (grows down, pointer points to top-of-stack value) */
	/* XXX should do this on per-VPU basis, once VPUs have stacks */
#if 0
	info("Copying VM stack...\n");
	for (void **p = *vm_psp; p < vm_stack_base; /* nada */)
		dstcurr += heap_gc_move(p++, dstcurr);
	info("VM stack copy complete\n");
#endif

	/* Copy registers and stacks of registered VPUs */
	/* XXX stacks not yet implemented in VPU */
	struct circlist_iter vpus_iter;
	circlist_iter_init(&the_vpu_sentinel, &vpus_iter);
	info("Copying VPU roots...\n");
	struct vpu *vpu;
	while ((vpu = (struct vpu *) circlist_iter_next(&vpus_iter)))
		dstcurr = heap_gc_vpu(vpu, dstcurr);
	info("VPU roots copy complete\n");

	/* Cheney copy of referenced blocks */
	info("Starting Cheney copy...\n");
	while (dstbase < dstcurr) {
		struct heap_header *header = (struct heap_header *) dstbase;
		heap_validate(header + 1);

		switch (header->meta & HH_PTRMASK) {
		case HH_PTRFULL: {
			const size_t j = header->nwords - EXTRAWORDS;
			for (i = 0; i < j; ++i)
				dstcurr += heap_gc_move(header->data + i,
							dstcurr);
			break;
		}
		case HH_PTRFREE:
			break;
		default:
			panicf("Unhandled heap metadata: %zX\n", header->meta);
		}
		dstbase += header->nwords;
	}
	info("Cheney copy complete\n");
	assert(dstbase == dstcurr);

	/* Obliterate source heap, retarget pointers */
	/*
	 * Note: this innocent-looking memset() takes up >99% of the GC
	 * time in many cases.  It's here since the heap is under active
	 * development and I want to fail fast in case of issues, but it
	 * will absolutely have to be conditionalized for good performance.
	 */
	memset(srcbase, 0, (the_heap_bound - srcbase) * WORDBYTES);
	the_heap = dstcurr;
	the_heap_bound = dstbound;
	info("GC postvalidation starting...\n");
	heap_validate_full();
	info("GC postvalidation complete\n");

	clock_gettime(CLOCK_MONOTONIC, &timespec);
	time1 = (double) timespec.tv_sec +
		((double) timespec.tv_nsec / 1000000000.0);
	infof("GC done, cycle %u, dt %.6fs, duty %.2f%%\n", the_gc_cycle,
	      time1 - time0, 100.0 * (time1 - time0) / (time0 - timep));
	timep = time1;
	during_gc = 0;
}

size_t
heap_header_size(void)
{
	return HEADERBYTES;
}

/*
 * We can currently only move pointer-free, unreferenced structures out of
 * the managed heap.  To do otherwise would require a pointer-forwarding
 * exercise similar to a full GC pass; not worth implementing unless there's
 * a demonstrated need.
 */
size_t
heap_perm(const void *datum, void *dst, size_t dstsize)
{
	heap_validate((void*) datum);
	struct heap_header *src = (struct heap_header*) datum - 1;
	if ((src->meta & HH_PTRMASK) != HH_PTRFREE)
		panic("Can't move pointerful object to the permanent heap\n");
	size_t size = src->nwords * WORDBYTES;
	if (size > dstsize)
		panicf("Need %zu bytes, %zu available\n", size, dstsize);
	memcpy(dst, src, size);
	((struct heap_header*) dst)->meta &= ~HH_LOCMASK;	/* outside */
	return size;
}

/*
 * At the time we push a heap root, it should either be NULL or should point
 * at valid heap data.  It may change subsequently, of course; this is just
 * a sanity check.  Note that the root we're given is the *address* of the
 * root rather than the root pointer itself.
 */
void
heap_root_push(void *root)
{
	void *datum = *((void **) root);
	heap_validate(datum);
	if (root_stack_next >= HEAPROOTS)
		panic("Heap root stack exhausted\n");
	the_root_stack[root_stack_next++] = root;
}

/*
 * We also check validity when the root is popped.  We can't go as far as
 * asserting it's NULL; it might still be a valid heap pointer to an object
 * which is now referenced by another managed object and thus no longer
 * needs to be designated an explicit root.
 */
void
heap_root_pop(void *root)
{
	assert(root);
	void *datum = *((void **) root);
	assert(heap_validate(datum));
	if (!root_stack_next)
		panic("Heap root stack underflow\n");
	if (the_root_stack[--root_stack_next] != root)
		panic("Failed heap root FIFO check\n");
}

void
heap_root_register_allocator(struct heap_root_allocator *roots)
{
	circlist_add_tail(&the_roots_sentinel, &roots->entry);
}

void
heap_root_deregister_allocator(struct heap_root_allocator *roots)
{
	circlist_remove(&roots->entry);
}

void
heap_register_vpu(struct vpu *vpu)
{
	circlist_add_tail(&the_vpu_sentinel, &vpu->gc_entry);
}

static void *
heap_shallow_validate(void *datum)
{
	if (!datum)
		panic("Heap validation: datum is NULL\n");

	const struct heap_header *header = datum;
	--header;	/* Offset from stored data to heap header */
	if ((header->meta & HH_LOCMASK) == HH_OUTSIDE)
		return datum;			/* not a heap-managed object */
	if ((header->meta & HH_LOCMASK) != HH_INSIDE)
		panic("Copy in/out not yet supported!\n");
	if (!in_a_space(datum) && !in_b_space(datum))
		panicf("Datum 0x%"PRIXPTR" is outside managed space\n", datum);
	if (header->hmagic == HH_MAGIC &&
	    header->nwords == 0 && during_gc)	/* forwarded during GC */
		return datum;
	if (header->hmagic != HH_MAGIC ||
	    header->nwords < EXTRAWORDS ||
	    header->nwords >= HEAPWORDS ||
	    header->data[header->nwords - EXTRAWORDS] != HF_MAGIC) {
		heap_dump_datum(datum);
		panicf("Datum 0x%"PRIXPTR" has been mangled\n", datum);
	}

	return datum;
}

void *
heap_validate(void *datum)
{
	heap_shallow_validate(datum);

	/*
	 * One-layer-deep validation of pointers within this datum.
	 */
	const struct heap_header *header = datum;
	--header;	/* Offset from stored data to heap header */
	if (header->hmagic == HH_MAGIC &&
	    header->nwords == 0 && during_gc)	/* forwarded during GC */
		return datum;
	switch (header->meta & HH_PTRMASK) {
	case HH_PTRFREE:
		break;
	case HH_PTRFULL: {
		const uintptr_t b = header->nwords - EXTRAWORDS;
		uintptr_t i = 0;
		while (i < b) heap_shallow_validate(header->data[i++]);
		break;
	}
	default:
		panicf("Corrupt heap metadata: %zX\n", header->meta);
	}
	return datum;
}

void
heap_validate_full(void)
{
	info("Full heap validation starting...\n");
	uintptr_t *base = heap_base(the_heap);
	while (base < the_heap) {
		struct heap_header *header = (struct heap_header*) base;
		heap_validate(header + 1);
		assert(header->nwords > 0);
		assert(header->nwords < HWORDS);
		base += header->nwords;
	}
	info("Full heap validation complete\n");
}
