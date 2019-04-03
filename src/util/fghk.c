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

#include "fghk.h"

/*
 * Here are some primes to use with the FGH.  They don't have any specific
 * properties; I just plugged some numbers into a "find next prime" CGI.
 * These are the primes following:
 *
 * K0: 0xF0F0F0F0 (retired as they don't mix as well).
 * K1: 0xCCCCCCCC
 * K2: 0xAAAAAAAA
 *
 * Others preserve the pattern of taking a number with an equal number of
 * 1 and 0 bits and finding the first prime greater than it.
 *
 * CGI at: http://wims.unice.fr/wims/wims.cgi?module=tool/number/primes.en
 */
const uintptr_t FGHK [] = {
#ifdef RETIRED_PRIMES
	4042322173U,	/* above 11110000111100001111000011110000 */
	252645131U,	/* below 00001111000011110000111100001111 */
#endif
	3435973859U,	/* above 11001100110011001100110011001100 */
	858993433U,	/* below 00110011001100110011001100110011 */
	2863311551U,	/* above 10101010101010101010101010101010 */
	1431655751U,	/* below 01010101010101010101010101010101 */
};
