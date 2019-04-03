#ifndef LARK_UTIL_BYTEBUF_H
#define LARK_UTIL_BYTEBUF_H
/*
 * Copyright (c) 2009-2017 Michael P. Touloumtzis.
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

#include <stddef.h>
#include <stdint.h>

/*
 * XXX should implement both bytebuf and wordbuf in terms of a common buffer.
 */
struct bytebuf {
	size_t capacity, used;
	uint8_t *data;
};

extern void bytebuf_init(struct bytebuf *buf);
extern void bytebuf_fini(struct bytebuf *buf);
extern void bytebuf_append(struct bytebuf *buf,
			   const uint8_t *data, size_t size);
extern size_t bytebuf_complete(struct bytebuf *buf);
extern void bytebuf_grow(struct bytebuf *buf, size_t size);

static inline void
bytebuf_append_string(struct bytebuf *buf, const char *str, size_t size)
	{ return bytebuf_append(buf, (const uint8_t*) str, size); }

static inline void
bytebuf_append_byte(struct bytebuf *buf, uint8_t byte)
{
	if (buf->used < buf->capacity)
		buf->data[buf->used++] = byte;
	else
		bytebuf_append(buf, &byte, sizeof byte);
}

static inline void
bytebuf_append_char(struct bytebuf *buf, char c)
	{ bytebuf_append_byte(buf, (uint8_t) c); }

static inline size_t
bytebuf_used(const struct bytebuf *buf)
	{ return buf->used; }

#endif /* LARK_UTIL_BYTEBUF_H */
