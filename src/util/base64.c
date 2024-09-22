/*
 * Copyright (c) 2001-2015 Michael P. Touloumtzis.
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

#include <string.h>

#include "assert.h"
#include "base64.h"
#include "message.h"

#define EQ 254
#define NG 255

static size_t
base64_stream_decode(struct base64_state *state,
		     unsigned char *dst, size_t dstsize,
		     const unsigned char *src, size_t srcsize);
static size_t
base64_stream_encode(struct base64_state *state,
		     unsigned char *dst, size_t dstsize,
		     const unsigned char *src, size_t srcsize);

static size_t
base64_finish_decode(struct base64_state *state,
		     unsigned char *dst, size_t dstsize);

static size_t
base64_finish_encode(struct base64_state *state,
		     unsigned char *dst, size_t dstsize);


/*
 * Lookup table for base64 decoding.  Should consider moving 0-31 from
 * lookup table to algorithm to reduce cache footprint.
 */
static const unsigned char decode_table[] = {
	NG, NG, NG, NG, NG, NG, NG, NG,		/* 0-7 */
	NG, NG, NG, NG, NG, NG, NG, NG,		/* 8-15 */
	NG, NG, NG, NG, NG, NG, NG, NG,		/* 16-23 */
	NG, NG, NG, NG, NG, NG, NG, NG,		/* 24-31 */

	NG, NG, NG, NG, NG, NG, NG, NG,		/* 32-39 */
	NG, NG, NG, NG, NG, 62, NG, NG,		/* 40-47 */
	52, 53, 54, 55, 56, 57, 58, 59,		/* 48-55 */
	60, 61, NG, NG, NG, EQ, NG, NG,		/* 56-63 */

	NG,  0,  1,  2,  3,  4,  5,  6,		/* 64-71 */
	 7,  8,  9, 10, 11, 12, 13, 14,		/* 72-79 */
	15, 16, 17, 18, 19, 20, 21, 22,		/* 80-87 */
	23, 24, 25, NG, NG, NG, NG, 63,		/* 88-95 */

	NG, 26, 27, 28, 29, 30, 31, 32,		/* 96-103 */
	33, 34, 35, 36, 37, 38, 39, 40,		/* 104-111 */
	41, 42, 43, 44, 45, 46, 47, 48,		/* 112-119 */
	49, 50, 51, NG, NG, NG, NG, NG		/* 120-127 */
};

/*
 * Lookup table for base64 encoding.  There are only 64 valid results
 * of an encoding, because 6 bits are encoded at once.
 */
static const unsigned char encode_table[] = {
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
	'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
	'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
	'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
	'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
	'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
	'w', 'x', 'y', 'z', '0', '1', '2', '3',
	'4', '5', '6', '7', '8', '9', '-', '_'
};

/* decode a byte using the decode table */
static inline unsigned char
decode_byte(unsigned char c)
{
	/* bytes 128 and up contain no valid decodings */
	return (c & 0x80) ? NG : decode_table[c];
}

#define SIXBITS 63

/* encode 3 data bytes into 4 base64 bytes */
static inline unsigned char*
encode_trio(unsigned char *dst, const unsigned char *trio)
{
	*dst++ = encode_table[trio[0] >> 2];
	*dst++ = encode_table[((trio[0] << 4) | (trio[1] >> 4)) & SIXBITS];
	*dst++ = encode_table[((trio[1] << 2) | (trio[2] >> 6)) & SIXBITS];
	*dst++ = encode_table[trio[2] & SIXBITS];
	return dst;
}

/* same as above, but insert a newline before the specified position */
static inline unsigned char*
encode_trio_and_wrap(unsigned char *dst, const unsigned char *trio,
		     unsigned pos)
{
	assert(pos < 4);

	switch (pos) {
	case 0: break;
	case 1: *dst++ = encode_table[trio[0] >> 2];
		break;
	case 2: *dst++ = encode_table[trio[0] >> 2];
		*dst++ = encode_table[((trio[0] << 4) |
				      (trio[1] >> 4)) & SIXBITS];
		break;
	case 3: *dst++ = encode_table[trio[0] >> 2];
		*dst++ = encode_table[((trio[0] << 4) |
				      (trio[1] >> 4)) & SIXBITS];
		*dst++ = encode_table[((trio[1] << 2) |
				      (trio[2] >> 6)) & SIXBITS];
		break;
	}
	*dst++ = '\n';

	/* use fallthrough on this one */
	switch (pos) {
	case 0: *dst++ = encode_table[trio[0] >> 2];
	case 1: *dst++ = encode_table[((trio[0] << 4) |
				      (trio[1] >> 4)) & SIXBITS];
	case 2: *dst++ = encode_table[((trio[1] << 2) |
				      (trio[2] >> 6)) & SIXBITS];
	case 3: *dst++ = encode_table[trio[2] & SIXBITS];
	}
	return dst;
}

void
base64_set_wrap(struct base64_state *state, int wrap)
{
	assert(state);
	assert(!wrap || (wrap >= 4 && wrap <= UCHAR_MAX));
	state->wrap = wrap;
}

size_t
base64_decode(void *dst, size_t dstsize, const void *src, size_t srcsize)
{
	const size_t error = (size_t) -1;
	struct base64_state state;
	size_t decoded, finished;

	base64_stream_start(&state, BASE64_DECODE);
	decoded = base64_stream(&state, dst, dstsize, src, srcsize);
	srcsize -= base64_stream_consumed(&state);	/* for assert */
	finished = base64_stream_finish(&state,
					(unsigned char*) dst + decoded,
					dstsize - decoded);
	if (finished == error)
		return error;
	assert(!srcsize);
	return decoded + finished;
}

size_t
base64_encode(void *dst, size_t dstsize, const void *src, size_t srcsize)
{
	const size_t error = (size_t) -1;
	struct base64_state state;
	size_t encoded, finished;

	base64_stream_start(&state, BASE64_ENCODE);
	encoded = base64_stream_encode(&state, dst, dstsize, src, srcsize);
	srcsize -= base64_stream_consumed(&state);	/* for assert */
	finished = base64_stream_finish(&state,
					(unsigned char *) dst + encoded,
					dstsize - encoded);
	if (finished == error)
		return error;
	assert(!srcsize);
	return encoded + finished;
}

static size_t
base64_stream_decode(struct base64_state *state,
		     unsigned char *dst, size_t dstsize,
		     const unsigned char *src, size_t srcsize)
{
	size_t remaining = srcsize;
	size_t written = 0;

	assert(state->cached <= 4);
	state->consumed = 0;
	if (!dstsize)
		return 0;
	while (1) {
		if (state->cached == 4) {
			if (dstsize < 3)
				break;
			/*
			 * Padding can consist of zero, one, or two
			 * equal signs.  See MIME specs for details.
			 */
			*dst++ = (state->cache[0] << 2) |
				((state->cache[1] >> 4) & 3);
			--dstsize, ++written;

			if (state->cache[2] != EQ) {
				*dst++ = ((state->cache[1] << 4) & 0xF0) |
					((state->cache[2] >> 2) & 0x0F);
				--dstsize, ++written;

				if (state->cache[3] != EQ) {
					*dst++ = ((state->cache[2] << 6) &
						0xC0) | state->cache[3];
					--dstsize, ++written;
				}
			}
			state->cached = 0;
		}
		if (!remaining)
			break;
		state->cache[state->cached] = decode_byte(*src++);
		--remaining;
		if (state->cache[state->cached] != NG)
			++state->cached;
	}
	state->consumed = srcsize - remaining;
	return written;
}

static size_t
base64_stream_encode(struct base64_state *state,
		     unsigned char *dst, size_t dstsize,
		     const unsigned char *src, size_t srcsize)
{
	const unsigned char *ptr = src;
	size_t written = 0;

	assert(state->cached <= 3);
	while (1) {
		/* do we have enough bytes to generate output? */
		if (state->cached == 3) {
			if (state->wrap) {
				/* will wrap on failure cases */
				state->column += 4;

				if (state->column > state->wrap) {
					/* need to output newline */
					if (dstsize < 5) {
						state->column -= 4;
						break;
					}
					state->column -= state->wrap;
					assert(state->column);
					assert(state->column <= 4);
					dst = encode_trio_and_wrap(dst,
						state->cache,
						4 - state->column);
					dstsize -= 5, written += 5;
				} else {
					/* no newline yet */
					if (dstsize < 4) {
						state->column -= 4;
						break;
					}
					dst = encode_trio(dst, state->cache);
					dstsize -= 4, written += 4;
				}
			} else {
				if (dstsize < 4)
					break;
				dst = encode_trio(dst, state->cache);
				dstsize -= 4, written += 4;
			}
			if (state->wrap)
				assert(state->column <= state->wrap);
			state->cached = 0;
		}
		if (!srcsize)
			break;
		state->cache[state->cached++] = *ptr++;
		--srcsize;
	}
	state->consumed = ptr - src;
	return written;
}

size_t
base64_stream(struct base64_state *state,
	      void *dst, size_t dstsize,
	      const void *src, size_t srcsize)
{
	arg_assert(state);
	arg_assert(src);
	arg_assert(dst);

	assert(state->direction == BASE64_DECODE ||
	       state->direction == BASE64_ENCODE);
	if (state->direction == BASE64_DECODE)
		return base64_stream_decode(state, dst, dstsize, src, srcsize);
	else
		return base64_stream_encode(state, dst, dstsize, src, srcsize);
}

size_t
base64_stream_finish(struct base64_state *state, void *dst, size_t dstsize)
{
	arg_assert(state);
	arg_assert(dst);

	assert(state->direction == BASE64_DECODE ||
	       state->direction == BASE64_ENCODE);
	return (state->direction == BASE64_ENCODE) ?
		base64_finish_encode(state, dst, dstsize) :
		base64_finish_decode(state, dst, dstsize);
}

static size_t
base64_finish_decode(struct base64_state *state,
		     unsigned char *dst, size_t dstsize)
{
	const size_t error = (size_t) -1;
	size_t count;

	/* finishing doesn't consume any source bytes */
	state->consumed = 0;

	if (!state->cached)
		return 0;
	if (state->cached != 4 || dstsize < 3)
		return error;

	count = base64_stream_decode(state, dst, dstsize, dst, 0);
	assert(count == 3);
	assert(!state->cached);
	assert(!state->consumed);
	return count;
}

static size_t
base64_finish_encode(struct base64_state *state,
		     unsigned char *dst, size_t dstsize)
{
	const size_t error = (size_t) -1;
	size_t count;

	/* finishing doesn't consume any source bytes */
	state->consumed = 0;

	/*
	 * If the cache is empty, the only thing we have to output is
	 * the final newline, which is conditional on a nonzero wrap
	 * column.
	 */
	if (!state->cached) {
		if (!state->wrap)
			return 0;	/* success */
		if (!dstsize)
			return error;	/* no room for trailing '\n' */
		*dst = '\n';
		return 1;
	}

	/*
	 * We can have 3 bytes in the cache, in the case in which we
	 * completely consumed the source data, but didn't have enough
	 * output buffer to write it out.  We can call the standard
	 * base64_stream_encode() with no source data to work out the
	 * problem.
	 */
	if (state->cached == 3) {
		count = base64_stream_encode(state, dst, dstsize, dst, 0);
		assert(count <= dstsize);
		assert(!state->consumed);
		assert(!count || !state->cached);
		if (!count)
			return error;
		if (!state->wrap)
			return count;	/* success */
		if (count == dstsize)
			return error;	/* no room for trailing '\n' */
		dst[count] = '\n';
		return count + 1;
	}

	/*
	 * In the remainder of cases we need to output some Base64
	 * padding in the form of '=' signs.  We first compute the
	 * encoded data, then decide how to output it.
	 */
	assert(state->cached > 0 && state->cached < 3);
	if (state->wrap) {
		if (state->column + 4 > state->wrap)
			count = 6;
		else
			count = 5;
	} else
		count = 4;
	if (dstsize < count)
		return error;

	/*
	 * At this point we are guaranteed to succeed.  Fill the
	 * remaining bytes of the cache with zeroes and encode to
	 * 'dst'.  If we don't need to wrap within the padded
	 * output we are basically done.
	 */
	switch (state->cached) {
	case 1: state->cache[1] = 0;
	case 2:	state->cache[2] = 0;
	}
	encode_trio(dst, state->cache);
	switch (state->cached) {
	case 1: dst[2] = '=';
	case 2: dst[3] = '=';
	}
	state->cached = 0;

	if (count < 6) {
		if (count == 5) {
			dst[4] = '\n';
			state->column = 0;
		}
		return count;
	}

	/*
	 * This is the real special case: padded output with a line
	 * wrap in the middle of the padded trio.  Manually insert
	 * the newline in the right place.
	 */
	state->column = state->column + 4 - state->wrap;
	assert(state->column > 0 && state->column <= 4);
	memmove(dst + (5 - state->column), dst + (4 - state->column),
		state->column);
	dst[4 - state->column] = '\n';
	dst[5] = '\n';
	return count;
}

void base64_convert(unsigned long n, void *dst, size_t dstsize)
{
	unsigned char *ptr = dst;
	do {
		if (!dstsize--) goto panic;
		*ptr++ = encode_table[n % 64];
		n /= 64;
	} while (n);
	if (!dstsize) goto panic;
	*ptr++ = '\0';
	return;
panic:
	panic("Output buffer too small!\n");
}
