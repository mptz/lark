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

#include "utf8.h"

/*
 * Mark placed in the leading byte of a UTF-8 sequence to indicate length.
 */
static const uint8_t leading_mark [7] = {
	0x00 /* unused */, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC,
};

/*
 * Error included in adding up raw UTF-8 byte values.  Rather than mask
 * out the value part on a byte-per-byte basis, we add raw values and
 * subtract the accumulated junk at the end.
 */
static const uint32_t cumulative_error [6] = {
	0x00000000UL, 0x00003080UL, 0x000E2080UL, 
	0x03C82080UL, 0xFA082080UL, 0x82082080UL
};

/*
 * Lookup table mapping leading byte values to # of remaining bytes.
 */
static const uint8_t trailing_bytes [256] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 
	3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5,
};

static inline uint_fast8_t
encoded_size(uint32_t n)
{
	if (n < 0x00000080) return 1;
	if (n < 0x00000800) return 2;
	if (n < 0x00010000) return 3;
	if (n < 0x00200000) return 4;
	if (n < 0x04000000) return 5;
	return 6;
}

uint_fast8_t
utf8_encoded_size(uint32_t n)
{
	return encoded_size(n);
}

const uint8_t *
utf8_decode(const uint8_t *src, uint32_t *n)
{
	uint_fast8_t rest = trailing_bytes[*src];
	uint32_t m = 0;

	switch (rest) {
	case 5: m += *src++; m <<= 6; /* fall through... */
	case 4: m += *src++; m <<= 6; /* fall through... */
	case 3: m += *src++; m <<= 6; /* fall through... */
	case 2: m += *src++; m <<= 6; /* fall through... */
	case 1: m += *src++; m <<= 6; /* fall through... */
	case 0: m += *src++;
	}

	*n = m - cumulative_error[rest];
	return src;
}

uint8_t *
utf8_encode(uint8_t *dst, uint32_t n)
{
	static const uint32_t mask = 0xBF;
	static const uint32_t mark = 0x80;
	uint_fast8_t size = encoded_size(n);

	dst += size;
	switch (size) {
	case 6: *--dst = (n | mark) & mask; n >>= 6; /* fall through... */
	case 5: *--dst = (n | mark) & mask; n >>= 6; /* fall through... */
	case 4: *--dst = (n | mark) & mask; n >>= 6; /* fall through... */
	case 3: *--dst = (n | mark) & mask; n >>= 6; /* fall through... */
	case 2: *--dst = (n | mark) & mask; n >>= 6; /* fall through... */
	case 1: *--dst = (n | leading_mark[size]);
	}

	return dst + size;	/* Need to re-increment after backing up */
}
