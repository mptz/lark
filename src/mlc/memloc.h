#ifndef LARK_MLC_MEMLOC_H
#define LARK_MLC_MEMLOC_H
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

/*
 * Generate shorter (string) representations of pointers which are easier
 * to visually parse than long hexadecimal numbers.  We use two tricks:
 * (1) Represent pointers not as offsets from the beginning of virtual
 *     memory, but as distances from an arbitrary zero point in the heap.
 *     Encode positive and negative numbers as even and odd respectively.
 * (2) Represent numbers in base 62 rather than base 10 or base 16.
 *
 * memloc returns pointers to a small pool of buffers which we cycle through
 * with each call.  It's not reentrant, but we could switch to thread-local
 * buffer pools to gain thread safety.  The intended use in printf, panicf,
 * and other diagnostic outputs only requires persistence for the duration
 * of the calling output function.  Use strdup() if the addresses will be
 * retained non-ephemerally.
 */
extern const char *memloc(const void *addr);

#endif /* LARK_MLC_MEMLOC_H */
