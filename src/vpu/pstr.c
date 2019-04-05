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

#include <string.h>

#include <util/utf8.h>

#include "heap.h"
#include "pstr.h"

void *
stralloc(const uint8_t *data, strsize_mt size)
{
	uint8_t *rep = heap_alloc_unmanaged_bytes(
		utf8_encoded_size(size) + size);
	strpack(rep, data, size);
	return rep;
}

int
strcmp3(str_mt a, str_mt b)
{
	strsize_mt asize, bsize;
	const uint8_t *adata = utf8_decode(a, &asize);
	const uint8_t *bdata = utf8_decode(b, &bsize);
	if (asize < bsize) return -1;
	if (bsize > asize) return +1;
	return memcmp(adata, bdata, asize);
}

str_mt
strconcat(str_mt a, str_mt b)
{
	strsize_mt asize, bsize;
	const uint8_t *adata = utf8_decode(a, &asize);
	const uint8_t *bdata = utf8_decode(b, &bsize);
	strsize_mt csize = asize + bsize;
	uint8_t *c = heap_alloc_unmanaged_bytes(
		utf8_encoded_size(csize) + csize);
	memcpy(mempcpy(utf8_encode(c, csize), adata, asize), bdata, bsize);
	return (const uint8_t*) c;
}

uint8_t *
strdata(str_mt s)
{
	strsize_mt size;
	return (uint8_t *) utf8_decode(s, &size);
}

uint8_t *
strempty(strsize_mt size)
{
	uint8_t *rep = heap_alloc_unmanaged_bytes(
		utf8_encoded_size(size) + size);
	utf8_encode(rep, size);
	return rep;
}

strsize_mt
strsize(str_mt s)
{
	strsize_mt size;
	utf8_decode(s, &size);
	return size;
}

size_t
strrepsize(str_mt s)
{
	strsize_mt size;
	const uint8_t *d = utf8_decode(s, &size);
	return size + (d - s);
}

void
strpack(uint8_t *dst, const uint8_t *src, strsize_mt size)
{
	memcpy(utf8_encode(dst, size), src, size);
}

const uint8_t *
strunpack(const uint8_t *src, strsize_mt *size)
{
	return utf8_decode(src, size);
}
