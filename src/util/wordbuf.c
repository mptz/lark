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

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "wordbuf.h"
#include "memutil.h"
#include "minmax.h"

/*
 * The max size is a soft limit--we can go over it, but we'll realloc
 * down to the max size once we no longer need such a large buffer.
 */
static const size_t wordbuf_min_count = 32;
static const size_t wordbuf_max_count = 4 * 1024 * 1024;

void
wordbuf_init(struct wordbuf *buf)
{
	buf->capacity = buf->used = 0;
	buf->data = NULL;
}

void
wordbuf_fini(struct wordbuf *buf)
{
	free(buf->data);
	wordbuf_init(buf);
}

void
wordbuf_append(struct wordbuf *buf, const word *data, size_t count)
{
	size_t need = buf->used + count;

	assert(buf->used <= buf->capacity);
	if (!buf->data) {
		buf->capacity = MAX(need, wordbuf_min_count);
		buf->data = xmalloc(buf->capacity * sizeof *data);
	} else if (need > buf->capacity) {
		buf->capacity = MAX(need, buf->capacity * 2);
		buf->data = xrealloc(buf->data, buf->capacity * sizeof *data);
	} else if (buf->capacity > wordbuf_max_count &&
		   need <= wordbuf_max_count) {
		buf->capacity = wordbuf_max_count;
		buf->data = xrealloc(buf->data, buf->capacity * sizeof *data);
	}

	memcpy(buf->data + buf->used, data, count * sizeof *data);
	buf->used += count;
}

void
wordbuf_free_clear(struct wordbuf *buf)
{
	for (size_t i = 0; i < buf->used; ++i)
		free((void*) buf->data[i]);
	wordbuf_clear(buf);
}
