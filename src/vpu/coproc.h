#ifndef LARK_VPU_COPROC_H
#define LARK_VPU_COPROC_H
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

/*
 * Nothing here yet--just tracking ideas for virtual coprocessors that
 * I'll use to manage the size of the main VPU dispatch table.  Note that
 * for these to be considered for implementation, they should be tightly
 * coupled to the VPU design; otherwise external C code makes more sense.
 *
 * For example, a coprocessor instruction to split a string into an array
 * of characters makes sense; one to match a regular expression should be
 * done either natively or in C.
 *
 * Note that the assembler should recognize symbolic coprocessor and
 * coprocessor operation names.
 */

/*
 * Coprocessor possibilities:

	floating point
	random numbers
	cryptography
	string operations
	vector/array operations
	bignum math
	hashing
	memory management/garbage collection
	tracing & debugging
	timing operations
	interrupt handling
	input/output

 */

#endif /* LARK_VPU_COPROC_H */
