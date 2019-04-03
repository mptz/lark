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

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "bytebuf.h"
#include "memutil.h"
#include "minmax.h"

/*
 * The max size is a soft limit--we can go over it, but we'll realloc
 * down to the max size once we no longer need such a large buffer.
 */
static const size_t bytebuf_min_size = 4 * 1024;
static const size_t bytebuf_max_size = 4 * 1024 * 1024;

void
bytebuf_init(struct bytebuf *buf)
{
	buf->capacity = buf->used = 0;
	buf->data = NULL;
}

void
bytebuf_fini(struct bytebuf *buf)
{
	free(buf->data);
	bytebuf_init(buf);
}

void
bytebuf_append(struct bytebuf *buf, const uint8_t *data, size_t size)
{
	size_t pos = buf->used;
	bytebuf_grow(buf, size);
	memcpy(buf->data + pos, data, size);
}

size_t
bytebuf_complete(struct bytebuf *buf)
{
	assert(buf->used <= buf->capacity);
	size_t have = buf->used;
	buf->used = 0;
	return have;
}

void
bytebuf_grow(struct bytebuf *buf, size_t size)
{
	size_t need = buf->used + size;
	assert(buf->used <= buf->capacity);
	if (!buf->data) {
		buf->capacity = MAX(need, bytebuf_min_size);
		buf->data = xmalloc(buf->capacity);
	} else if (need > buf->capacity) {
		buf->capacity = MAX(need, buf->capacity * 2);
		buf->data = xrealloc(buf->data, buf->capacity);
	} else if (buf->capacity > bytebuf_max_size &&
		   need <= bytebuf_max_size) {
		buf->capacity = bytebuf_max_size;
		buf->data = xrealloc(buf->data, buf->capacity);
	}
	buf->used = need;
}
