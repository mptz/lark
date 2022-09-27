#ifndef LARK_UTIL_HUID_H
#define LARK_UTIL_HUID_H
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

#define HUID_RANDOM_BYTES	18	/* 144 bits */
#define HUID_CHECK_BYTES	6	/* 48 bits */
#define HUID_BYTES		18	/* 144 bits */
#define HUID_REDUNDANT_BYTES	24	/* 192 bits */

#define HUID_CHARS		26	/* 3 x 8 chars + 2 x delimiter */
#define HUID_STR		27	/* ... plus one for NUL terminator */
#define HUID_REDUNDANT_CHARS	35	/* 4 x 8 chars + 3 x delimiter */

extern void huid_decode(void *huidb, const char *huidc);
extern void huid_encode(char *huidc, const void *huidb);
extern const void *huid_c2b(const char *huidc);	/* allocates */

#endif /* LARK_UTIL_HUID_H */
