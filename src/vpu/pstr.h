#ifndef LARK_VPU_PSTR_H
#define LARK_VPU_PSTR_H
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

#include <inttypes.h>

/*
 * Strings are length-prefixed so that they can contain embedded NUL chars.
 * They are UTF-8 encoded, and the length itself is a UTF-8 encoded
 * quantity, thus taking 1 byte for strings whose lengths are < 128.  This
 * is not a totally obvious time-speed tradeoff and I may revisit.
 */
typedef uint32_t strsize_mt;

typedef const uint8_t *str_mt;

extern void *stralloc(const uint8_t *data, strsize_mt size);
extern int strcmp3(str_mt a, str_mt b);	/* not a lexical order! */
extern str_mt strconcat(str_mt a, str_mt b);
extern uint8_t *strdata(str_mt s);
extern uint8_t *strempty(strsize_mt size);
extern strsize_mt strsize(str_mt s);	/* bytes, not including prefix */
extern size_t strrepsize(str_mt s);	/* total size of representation */
extern void strpack(uint8_t *dst, const uint8_t *src, strsize_mt size);
extern const uint8_t *strunpack(const uint8_t *src, strsize_mt *size);

#endif /* LARK_VPU_PSTR_H */
