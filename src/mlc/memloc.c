/*
 * Copyright (c) 2009-2025 Michael P. Touloumtzis.
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

#include "memloc.h"
#include "node.h"

/*
 * Start with letters, not numbers, to make our base-62 numerals look
 * more like valid identifiers (typically... this doesn't guarantee
 * they won't start with a numeral).
 */
static const char base62digits [] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"abcdefghijklmnopqrstuvwxyz"
	"0123456789";

#define MEMLOC_BUFFERS 32	/* enough for any printf? */
#define BASE62_WORD_SIZE 12	/* enough for 2^64 + '\0' in base 62 */

static char memloc_buffers [MEMLOC_BUFFERS][BASE62_WORD_SIZE];
static unsigned memloc_buffer;

static void base62(uintptr_t n, char *buf)
{
	if (!n) {
		buf[0] = 'A', buf[1] = '\0';	/* n.b. 'A' is 0th digit */
		return;
	}

	char *ptr = buf;
	for (/* nada */; n; n /= 62)
		*ptr++ = base62digits[n % 62];
	*ptr-- = '\0';
	while (ptr > buf) {
		char tmp = *ptr;
		*ptr-- = *buf;
		*buf++ = tmp;
	}
}

const char *memloc(const void *addr)
{
	static uintptr_t base = 0;
	if (!base) {
		/*
		 * We use a heap pointer to a representative-sized
		 * object as the zero point for our numbers.
		 */
		struct node *node = NodeNum(NULL, 0, 1);
		base = (uintptr_t) node;
		node_free(node);
	}
	uintptr_t curr = (uintptr_t) addr,
		  diff = curr > base ?
			(curr - base) * 2 + 1 :
			(base - curr) * 2;
	if (++memloc_buffer >= MEMLOC_BUFFERS)
		memloc_buffer = 0;
	base62(diff, memloc_buffers[memloc_buffer]);
	return memloc_buffers[memloc_buffer];
}
