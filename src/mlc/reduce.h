#ifndef LARK_MLC_REDUCE_H
#define LARK_MLC_REDUCE_H
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

struct node;

/*
 * Deep reduction penetrates ("reduces under") abstractions and other
 * deferred subexpressions; surface reduction does not.  The standard
 * terminology here is strong/weak reduction but I'm experimenting with
 * names which are more evocative of the reduction behavior, especially
 * for those who aren't familiar with the literature.
 */
enum reduction { REDUCTION_DEEP, REDUCTION_SURFACE };

extern struct node *reduce(struct node *node, enum reduction reduction);
extern void print_eval_stats(void);
extern void reset_eval_stats(void);

#endif /* LARK_MLC_REDUCE_H */
