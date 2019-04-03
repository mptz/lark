#ifndef LARK_UTIL_MEMUTIL_H
#define LARK_UTIL_MEMUTIL_H
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
#include <string.h>

extern void *xmalloc(size_t size);
extern void xfree(const void *ptr);	/* avoids need to cast away const */
extern void *xrealloc(void *ptr, size_t size);
extern void *xmemdup(const void *src, size_t size);
extern char *xstrdup(const char *src);

/*
 * As xmemdup(), but make sure the data is padded with '\0' in case we
 * access it as a NUL-terminated C string.
 */
extern void *
xmemdup0(const void *src, size_t size);

static inline int ptrscmp(const void *a, const void *b, size_t n)
	{ return memcmp(a, b, n * sizeof a); }

#endif /* LARK_UTIL_MEMUTIL_H */
