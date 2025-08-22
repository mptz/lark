/*
 * Copyright (c) 2008-2025 Michael P. Touloumtzis.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "endian.h"
#include "fgh.h"

uint64_t
fghs64(const void *key, size_t size, uint64_t seed)
{
	static const uint64_t K = 0x63CFA97B40D4BB53ULL;
	const uint64_t *p = key;
	size_t blocks = size >> 5;	/* # of 32-bit blocks in key */
	uint64_t g = seed, h = size;

	/*
	 * Hand the ragged tail end of the key first.
	 *
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
#if (FGH_BIG_ENDIAN || FGH_LITTLE_ENDIAN)
	unsigned lo = size & 7;
	size >>= 3;
	if (lo) {
#if FGH_BIG_ENDIAN
		g ^= p[size] >> ((8 - lo) * 8);
#else
		g ^= p[size] & (~(uint64_t) 0 >> ((8 - lo) * 8));
#endif
		g *= K;
	}
#else /* FGH_NO_ENDIAN */
	const uint8_t *p8 = (const uint8_t *) p;
	switch (size & 7) {
	case 7: g ^= p8[--size]; g ^= g << 8;	/* fall through */
	case 6: g ^= p8[--size]; g ^= g << 8;	/* fall through */
	case 5: g ^= p8[--size]; g ^= g << 8;	/* fall through */
	case 4: g ^= p8[--size]; g ^= g << 8;	/* fall through */
	case 3: g ^= p8[--size]; g ^= g << 8;	/* fall through */
	case 2: g ^= p8[--size]; g ^= g << 8;	/* fall through */
	case 1: g ^= p8[--size]; g *= K;	/* fall through */
	case 0: size >>= 3;
	}
#endif

	switch (size & 3) {
		while (blocks--) {
			h ^= *p++ ^ (h << 15 | h >> 49); h *= K;
	case 3: 	g ^= *p++ ^ (g << 17 | g >> 47); g *= K;
	case 2: 	h ^= *p++ ^ (h << 15 | h >> 49); h *= K;
	case 1: 	g ^= *p++ ^ (g << 17 | g >> 47); g *= K;
	case 0:		;
		};
	}

	h ^= g;
	h ^= (h <<  8 | h >> 56) * K;
	h ^= (h << 25 | h >> 39) * K;
	h ^= (h <<  9 | h >> 55) * K;
	return h;
}
