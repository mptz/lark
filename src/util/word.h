#ifndef LARK_UTIL_WORD_H
#define LARK_UTIL_WORD_H
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

#include <stdint.h>

/*
 * A machine word.  Specifically, the minimal size which is big enough
 * to hold either a pointer or a value from a general purpose register.
 * On modern mainstream systems these notions coincide.
 */
typedef uintptr_t word;

#define WORD_MAX UINTPTR_MAX
#define WORD_MIN 0
#define WORD_SIZE (sizeof (word))
#define WORD_ALIGNED(x) (((x) & (sizeof (word) - 1)) == 0)

/*
 * Same thing, signed.
 */
typedef intptr_t offset;

#define OFFSET_MAX INTPTR_MAX
#define OFFSET_MIN INTPTR_MIN
#define OFFSET_SIZE (sizeof (offset))

/*
 * Word-sized floating point numbers.
 */
#if UINTPTR_MAX == UINT32_MAX
typedef float fpw;
#else
typedef double fpw;
#endif

#endif /* LARK_UTIL_WORD_H */
