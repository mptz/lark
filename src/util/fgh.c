/*
 * Copyright (c) 2009-2015 Michael P. Touloumtzis.
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

#include <endian.h>

#include "fgh.h"

#if UINTPTR_MAX == UINT32_MAX

/*
 * Version with 32-bit internal state & mixing.
 */
uint32_t
fgh32(const void *key, size_t size, const uintptr_t k)
{
	const uint32_t *p = key;
	size_t i = size >> 4;
	uint32_t h = k, h2 = size;

	switch ((size >> 2) & 3) {
	case 0: while (i--) {
			h -= *p++;
			h ^= h >> 19;
			h *= k;
	case 3: 	h2 -= *p++;
			h2 ^= h2 >> 15;
			h2 *= k;
	case 2: 	h -= *p++;
			h ^= h >> 23;
			h *= k;
	case 1: 	h2 -= *p++;
			h2 ^= h2 >> 17;
			h2 *= k;
			h ^= h2 << 13 | h2 >> 19;
		};
	}

	/*
	 * The endian-specific variants dereference past the end of the
	 * given key, but not past the word containing the end.  I'm not
	 * aware of any current architectures which provide memory
	 * protection at byte granularity, but if they are around, they
	 * should use the NO_ENDIAN version.
	 *
	 * Also use NO_ENDIAN if you care about generating the same hash
	 * values on different architectures.  These are optimized for
	 * performance rather than for consistency.
	 */
#ifdef NO_ENDIAN
	switch (size & 3) {
	case 3: h ^= ((uint8_t*) p)[2]; h += h << 8; /* fall through */
	case 2: h ^= ((uint8_t*) p)[1]; h += h << 8; /* fall through */
	case 1: h ^= ((uint8_t*) p)[0]; h ^= h >> 13; /* fall through */
	case 0: break;
	};
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	switch (size & 3) {
	case 0: break;
	case 1: h ^= *p << 24; h ^= h >> 21; break;
	case 2: h ^= *p << 16; h ^= h >> 17; break;
	case 3: h ^= *p << 8; h ^= h >> 5; break;
	};
#elif __BYTE_ORDER == __BIG_ENDIAN
	switch (size & 3) {
	case 0: break;
	case 1: h ^= *p >> 24; h += h << 19; break;
	case 2: h ^= *p >> 16; h += h << 17; break;
	case 3: h ^= *p >> 8; h += h << 13; break;
	};
#else
#error "Can't determine native byte order"
#endif

	h ^= h >> 15;
	h *= k;
	h ^= h >> 15;
	h *= k;
	h ^= h >> 15;
	return h;
}

#else

/*
 * Version with 64-bit internal state & mixing.
 */
uint32_t
fgh32(const void *key, size_t size, const uintptr_t k)
{
	const uint64_t *p = key;
	size_t i = size >> 5;
	uint64_t h = k, h2 = size;

	switch ((size >> 3) & 3) {
	case 0: while (i--) {
			h -= *p++;
			h ^= h >> 31;
			h *= k;
	case 3: 	h2 -= *p++;
			h2 ^= h2 >> 31;
			h2 *= k;
	case 2: 	h -= *p++;
			h ^= h >> 31;
			h *= k;
	case 1: 	h2 -= *p++;
			h2 ^= h2 >> 31;
			h2 *= k;
			h ^= h2 << 23 | h2 >> 41;
		};
	}

	/*
	 * The endian-specific variants dereference past the end of the
	 * given key, but not past the word containing the end.  I'm not
	 * aware of any current architectures which provide memory
	 * protection at byte granularity, but if they are around, they
	 * should use the NO_ENDIAN version.
	 *
	 * Also use NO_ENDIAN if you care about generating the same hash
	 * values on different architectures.  These are optimized for
	 * performance rather than for consistency.
	 */
#ifdef NO_ENDIAN
#error "not ported to 64-bit!"
	switch (size & 3) {
	case 3: h ^= ((uint8_t*) p)[2]; h += h << 8; /* fall through */
	case 2: h ^= ((uint8_t*) p)[1]; h += h << 8; /* fall through */
	case 1: h ^= ((uint8_t*) p)[0]; h ^= h >> 13; /* fall through */
		h ^= h >> 31; h *= k;
	case 0: break;
	};
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	switch (size & 7) {
	case 0: break;
	case 1: h ^= *p << 56; h ^= h >> 53; h *= k; break;
	case 2: h ^= *p << 48; h ^= h >> 45; h *= k; break;
	case 3: h ^= *p << 40; h ^= h >> 37; h *= k; break;
	case 4: h ^= *p << 32; h ^= h >> 31; h *= k; break;
	case 5: h ^= *p << 24; h ^= h >> 37; h *= k; break;
	case 6: h ^= *p << 16; h ^= h >> 39; h *= k; break;
	case 7: h ^= *p << 8; h ^= h >> 5; h *= k; break;
	};
#elif __BYTE_ORDER == __BIG_ENDIAN
#error "not ported to 64-bit!"
	switch (size & 3) {
	case 1: h ^= *p >> 24; h += h << 3; break;
	case 2: h ^= *p >> 16; h ^= h >> 3; break;
	case 3: h ^= *p >> 8; h ^= h >> 11; break;
	};
#else
#error "Can't determine native byte order"
#endif

	/*
	 * Reduced final mix since we only need to mix into 32 bits.
	 */
	h ^= h >> 31;
	h *= k;
	return h >> 32;
}

#endif

/*
 * Version with fully mixed 64-bit output.
 */
uint64_t
fgh64(const void *key, size_t size, const uintptr_t k)
{
	const uint64_t *p = key;
	size_t i = size >> 5;
	uint64_t h = k, h2 = size;

	switch ((size >> 3) & 3) {
	case 0: while (i--) {
			h -= *p++;
			h ^= h >> 31;
			h *= k;
	case 3: 	h2 -= *p++;
			h2 ^= h2 >> 31;
			h2 *= k;
	case 2: 	h -= *p++;
			h ^= h >> 31;
			h *= k;
	case 1: 	h2 -= *p++;
			h2 ^= h2 >> 31;
			h2 *= k;
			h ^= h2 << 23 | h2 >> 41;
		};
	}

	/*
	 * The endian-specific variants dereference past the end of the
	 * given key, but not past the word containing the end.  I'm not
	 * aware of any current architectures which provide memory
	 * protection at byte granularity, but if they are around, they
	 * should use the NO_ENDIAN version.
	 *
	 * Also use NO_ENDIAN if you care about generating the same hash
	 * values on different architectures.  These are optimized for
	 * performance rather than for consistency.
	 */
#ifdef NO_ENDIAN
#error "not ported to 64-bit!"
	switch (size & 3) {
	case 3: h ^= ((uint8_t*) p)[2]; h += h << 8; /* fall through */
	case 2: h ^= ((uint8_t*) p)[1]; h += h << 8; /* fall through */
	case 1: h ^= ((uint8_t*) p)[0]; h ^= h >> 13; /* fall through */
	case 0: break;
	};
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	switch (size & 7) {
	case 0: break;
	case 1: h ^= *p << 56; h ^= h >> 37; break;
	case 2: h ^= *p << 48; h ^= h >> 37; break;
	case 3: h ^= *p << 40; h ^= h >> 37; break;
	case 4: h ^= *p << 32; h ^= h >> 32; break;
	case 5: h ^= *p << 24; h ^= h >> 21; break;
	case 6: h ^= *p << 16; h ^= h >> 17; break;
	case 7: h ^= *p << 8;  h ^= h >> 5;  break;
	};
#elif __BYTE_ORDER == __BIG_ENDIAN
	switch (size & 3) {
	case 0: break;
	case 1: h ^= *p >> 56; h += h << 37; break;
	case 2: h ^= *p >> 48; h += h << 37; break;
	case 3: h ^= *p >> 40; h += h << 37; break;
	case 4: h ^= *p >> 32; h += h << 32; break;
	case 5: h ^= *p >> 24; h += h << 21; break;
	case 6: h ^= *p >> 16; h += h << 17; break;
	case 7: h ^= *p >> 8;  h += h >> 5;  break;
	};
#else
#error "Can't determine native byte order"
#endif

	h ^= h >> 31;
	h *= k;
	h ^= h >> 31;
	h *= k;
	h ^= h >> 31;
	return h;
}

