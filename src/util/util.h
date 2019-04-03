#ifndef LARK_UTIL_UTIL_H
#define LARK_UTIL_UTIL_H
/*
 * Copyright (c) 2001-2003 Michael P. Touloumtzis.
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

#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#include <util/types.h>

#define LINE_LENGTH 4096
#define CMDBUF_LENGTH 128

static inline unsigned char
hexit(unsigned char c)
{
	return (c < 10) ? (c + '0') : (c - 10 + 'a');
}

static inline int
size_is_power_of_two(size_t size)
{
	return !(size & (size - 1));
}

/* returns first character following number */
extern char
hex2bin64(const char *hex, uint64_t *bin);

/* basic file utility functions */
extern FILE *
basic_fopen(const char *path);

extern int
basic_open(const char *path);

struct slurp {
	void *data;
	size_t size;
};

/* this one is mainly for testing code */
extern struct slurp
p_slurp(const char *path);

extern int
make_writeonly_file(const char *pathname, const char *description);

#endif /* LARK_UTIL_UTIL_H */
