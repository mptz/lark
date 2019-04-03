#ifndef LARK_UTIL_MINMAX_H
#define LARK_UTIL_MINMAX_H
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

/*
 * These versions are for simple constants.
 */
#define MIN_C(x,y)	((x) < (y) ? (x) : (y))
#define MAX_C(x,y)	((x) > (y) ? (x) : (y))

#define MIN(x,y)					\
	({						\
		const __typeof__(x) MIN_x = (x);	\
		const __typeof__(y) MIN_y = (y);	\
		(void) (&MIN_x == &MIN_y);		\
		MIN_x < MIN_y ? MIN_x : MIN_y;		\
	})

#define MAX(x,y)					\
	({						\
		const __typeof__(x) MAX_x = (x);	\
		const __typeof__(y) MAX_y = (y);	\
		(void) (&MAX_x == &MAX_y);		\
		MAX_x > MAX_y ? MAX_x : MAX_y;		\
	})

#define MIN_T(type,x,y)					\
	({						\
		const type MIN_x = (x), MIN_y = (y);	\
		MIN_x < MIN_y ? MIN_x : MIN_y;		\
	})

#define MAX_T(type,x,y)					\
	({						\
		const type MAX_x = (x), MAX_y = (y);	\
		MAX_x > MAX_y ? MAX_x : MAX_y;		\
	})

#endif /* LARK_UTIL_MINMAX_H */
