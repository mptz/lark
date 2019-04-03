#ifndef LARK_UTIL_BASE64_H
#define LARK_UTIL_BASE64_H
/*
 * Copyright (c) 2001-2019 Michael P. Touloumtzis.
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

#include <limits.h>	/* UCHAR_MAX */
#include <stddef.h>	/* size_t */
#include <stdint.h>

/*
 * Streaming base64 converter.
 */

enum base64_direction {
	BASE64_INVALID = 0,
	BASE64_DECODE,
	BASE64_ENCODE,
};

struct base64_state {
	size_t consumed;
	unsigned char cache [4];
	unsigned char direction;
	unsigned char cached;
	unsigned char wrap;
	unsigned char column;
};

static inline void
base64_stream_start(struct base64_state *state, enum base64_direction direction)
{
	state->consumed = 0;
	state->direction = direction;
	state->cached = 0;
	state->wrap = 0;
	state->column = 0;
}

/*
 * Set a wrap column for Base64 encoding.  If the wrap column is
 * set, a newline will be inserted whenever the output data would
 * exceed the given wrap column.  A wrap column of 0 (the default)
 * results in no output wrapping.  Note that the generated newline
 * is a single '\n' (linefeed) character, not an Internet standard
 * end-of-line ("\r\n", or carriage return-linefeed).  That makes
 * this Base64 implementation less than ideal for Internet mail
 * applications.
 *
 * Resetting a stream with base64_stream_start() resets the wrap
 * column, but finishing a stream with base64_stream_finish(), then
 * immediately beginning a new stream, preserves the wrap column.
 *
 * The wrap column has no effect on Base64 decoding.
 *
 * If nonzero, the value of 'wrap' must be between 4 and UCHAR_MAX
 * (typically 255).
 */
extern void base64_set_wrap(struct base64_state *state, int wrap);

/*
 * Encoded or decode data, depending on the direction specified in
 * base64_stream_start().  Returns the number of bytes written into
 * the destination buffer 'dst'.  Check base64_stream_consumed() to
 * get the count of bytes read from src.
 */
extern size_t
base64_stream(struct base64_state *state, void *dst, size_t dstsize,
	      const void *src, size_t srcsize);

/*
 * These stateless variants always completely consume their input
 * provided the output buffer size is ceil(4/3) the input buffer
 * size (when encoding) or ceil(3/4) the input buffer size (for
 * decoding).  If wrapping is enabled when encoding, the number
 * could increase by one newline per line of encoded data (line
 * length depends on the wrap column).
 *
 * They return the number of bytes written into the destination
 * buffer, or (size_t) -1 if the destination buffer was not large
 * enough to hold the decoded/encoded version of the given data.
 *
 * base64_decode() requires that 'srcsize' be a multiple of 4.
 */
extern size_t
base64_decode(void *dst, size_t dstsize, const void *src, size_t srcsize);
extern size_t
base64_encode(void *dst, size_t dstsize, const void *src, size_t srcsize);

/*
 * Get the number of source bytes consumed in the last call to
 * base64_stream().  The return value of that function is the number
 * of destination bytes written.
 */
static inline size_t
base64_stream_consumed(const struct base64_state *state)
{
	return state->consumed;
}

/*
 * Finish off a Base64 stream.  Returns count of bytes written to
 * dst, or ((size_t) -1) on error.
 *
 * When encoding, base64_stream_finish() inserts the proper padding
 * needed to generate a Base64 stream whose total length is a
 * multiple of four bytes.  If a wrap column is set, the data will
 * additionally be newline-terminated.
 *
 * A 'dstsize' of at least 6 bytes guarantees success when encoding;
 * this corresponds to 6 bytes of Base64-encoded data, plus a newline
 * at the wrap column, plus a terminating newline.
 *
 * When decoding, base64_stream_finish() returns ((size_t) -1) if the
 * streamed Base64 data was truncated (that is, if the total number of
 * Base64 bytes was not a multiple of four).  Successful return values
 * are 0 (if the cache was empty) or 3 (if the state cache was full),
 * so 'dstsize' should be at least 3.
 */
extern size_t
base64_stream_finish(struct base64_state *state, void *dst, size_t dstsize);

#endif /* LARK_UTIL_BASE64_H */
