#ifndef LARK_VPU_POOL_H
#define LARK_VPU_POOL_H
/*
 * Copyright (c) 2009-2018 Michael P. Touloumtzis.
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
 * Literal pool support.  The literal pool's responsibilities are:
 *	1) Tracking value representations which we need to reproduce in
 *	   the assembler's output.
 *	2) Consolidating identical instances of immutable atomic values such
 *	   as strings and bignums, to save space and improve load times.
 * To these, in theory, we could add ordering value representations for best
 * locality of reference, but we're not worrying about that yet.
 *
 * Although we are given heap-managed values, we don't protect them from GC
 * by making them heap roots; they're copied out of the heap.
 */ 

#include "bignum.h"
#include "pstr.h"

extern void pool_init(void);
extern void *pool_base(void);
extern size_t pool_int(int_mt z);
extern size_t pool_nat(nat_mt n);
extern size_t pool_str(str_mt s);
extern size_t pool_size(void);

#endif /* LARK_VPU_POOL_H */
