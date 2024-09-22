/*
 * Copyright (c) 2009-2019 Michael P. Touloumtzis.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memutil.h"
#include "message.h"

static char out_of_memory[] = "Virtual memory exhausted\n";

void *xmalloc(size_t size)
{
	if (!size) return NULL;	/* allowed but not required by standard */
	void *value = malloc(size);
	if (!value) panic(out_of_memory);
	return value;
}

void xfree(const void *ptr)
{
	free((void*) ptr);
}

void *xrealloc(void *ptr, size_t size)
{
	void *value = realloc(ptr, size);
	if (!value) panic(out_of_memory);
	return value;
}

void *xmemdup(const void *ptr, size_t size)
{
	void *value = xmalloc(size);
	memcpy(value, ptr, size);
	return value;
}

void *xmemdup0(const void *ptr, size_t size)
{
	void *value = xmalloc(size + 1);
	memcpy(value, ptr, size);
	((char*) value)[size] = '\0';
	return value;
}

char *xstrdup(const char *src)
{
	size_t size = strlen(src) + 1;
	char *value = xmalloc(size);
	memcpy(value, src, size);
	return value;
}
