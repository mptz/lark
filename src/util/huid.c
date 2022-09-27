/*
 * Copyright (c) 2009-2021 Michael P. Touloumtzis.
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
#include <string.h>

#include "base64.h"
#include "huid.h"
#include "memutil.h"

void huid_decode(void *huidb, const char *huidc)
{
	/*
	 * No extra work when decoding since the decoder skips non-base64
	 * characters, so we don't have to special-case the delimiters.
	 */
	size_t n = base64_decode(huidb, HUID_BYTES, huidc, HUID_CHARS);
	assert(n == HUID_BYTES);
}

void huid_encode(char *huidc, const void *huidb)
{
	/*
	 * Base64-encode a huid; multiples-of-three byte counts
	 * yield integral numbers of base64 digits, so no padding.
	 */
	size_t n = base64_encode(huidc, HUID_CHARS, huidb, HUID_BYTES);
	assert(n == 24);

	/*
	 * Insert delimiters.
	 *
	 * 0       8       16      24
	 * XXXXXXXXyyyyyyyyZZZZZZZZ    to
	 * XXXXXXXX.yyyyyyyy.ZZZZZZZZ
	 */
	memmove(huidc + 18, huidc + 16, 8);
	memmove(huidc + 9,  huidc + 8,  8);
	huidc[8] = huidc[17] = '.';
	huidc[26] = '\0';
}

const void *huid_c2b(const char *huidc)
{
	char *buf = xmalloc(HUID_BYTES);
	huid_decode(buf, huidc);
	return buf;
}
