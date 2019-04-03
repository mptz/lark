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

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "assert.h"
#include "fdutil.h"
#include "message.h"
#include "memutil.h"
#include "util.h"

char
hex2bin64(const char *hex, uint64_t *bin)
{
	uint64_t u = 0;
	size_t n;
	char h;

	arg_assert(hex);
	arg_assert(bin);

	for (n = sizeof u * 2; (h = *hex++) && n; --n) {
		if (h >= '0' && h <= '9') {
			u <<= 4;
			u |= h - '0';
		} else if (h >= 'a' && h <= 'f') {
			u <<= 4;
			u |= 10 + h - 'a';
		} else if (h >= 'A' && h <= 'F') {
			u <<= 4;
			u |= 10 + h - 'A';
		} else
			break;
	}
	*bin = u;
	return h;
}

FILE *
basic_fopen(const char *path)
{
	FILE *f;

	arg_assert(path);
	if (!strcmp("-", path))
		return stdin;
	if ((f = fopen(path, "r")))
		return f;
	ppanic(path);
}

int
basic_open(const char *path)
{
	int fd;

	arg_assert(path);
	if (!strcmp("-", path))
		return STDIN_FILENO;
	if ((fd = open(path, O_RDONLY)) != -1)
		return fd;
	ppanic(path);
}

struct slurp
p_slurp(const char *path)
{
	struct slurp result;
	int fd;

	arg_assert(path);
	fd = basic_open(path);
	result.size = p_fstat_size(fd);
	result.data = xmalloc(result.size);
	p_readall(fd, result.data, result.size);
	p_close(fd);
	return result;
}

int
make_writeonly_file(const char *pathname, const char *description)
{
	int fd;

	infof("Creating %s file '%s'\n", description, pathname);
	fd = open(pathname, O_WRONLY|O_CREAT|O_EXCL, 0666);
	if (fd == -1) {
		xperror("open");
		errf("Couldn't create %s file '%s'\n", description, pathname);
		return -1;
	}
	return fd;
}
