#ifndef LARK_UTIL_WORDBUF_H
#define LARK_UTIL_WORDBUF_H
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

#include <stddef.h>

#include "word.h"

struct wordbuf {
	size_t capacity, used;
	word *data;
};

extern void wordbuf_init(struct wordbuf *buf);
extern void wordbuf_fini(struct wordbuf *buf);
extern void wordbuf_append(struct wordbuf *buf, const word *data, size_t count);
extern void wordbuf_free_clear(struct wordbuf *buf);

static inline word
wordbuf_at(const struct wordbuf *buf, size_t index)
	{ return buf->data[index]; }

static inline void
wordbuf_clear(struct wordbuf *buf)
	{ buf->used = 0; }

static inline void
wordbuf_pop(struct wordbuf *buf)
	{ if (buf->used) buf->used--; }

static inline void
wordbuf_push(struct wordbuf *buf, word word)
{
	if (buf->used < buf->capacity)
		buf->data[buf->used++] = word;
	else
		wordbuf_append(buf, &word, 1);
}

static inline void
wordbuf_replace(struct wordbuf *buf, word word)
	{ buf->data[buf->used - 1] = word; }

static inline size_t
wordbuf_used(const struct wordbuf *buf)
	{ return buf->used; }

#endif /* LARK_UTIL_WORDBUF_H */
