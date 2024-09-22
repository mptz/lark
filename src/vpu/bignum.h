#ifndef LARK_VPU_BIGNUM_H
#define LARK_VPU_BIGNUM_H
/*
 * Copyright (c) 2009-2019 Michael P. Touloumtzis.
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

#include <stddef.h>	/* size_t */
#include <stdint.h>

/*
 * Representation for bignats; number of limbs used followed by the limbs.
 * Each limb is a 32-bit 'digit' (i.e. base 32); the order is LSW-to-MSW.
 */
struct natrep {
	size_t nlimbs;
	uint32_t limbs[];
};

/*
 * Right now there is lots of redundancy between nats and ints, and I'm
 * not trying to stamp it out (yet).  Ints are sign-magnitude, with an
 * entire word being devoted to the sign bit.  Is this inefficient?  Yes,
 * but it'll be fixed later.
 */
struct intrep {
	int sign;
	size_t nlimbs;
	uint32_t limbs[];
};

/*
 * Const-ness is baked into the typedefs since numeric representations should
 * be abstract and immutable outside this code.  None of the arithmetic
 * functions below mutate their arguments.
 */
typedef const struct natrep *nat_mt;
static inline size_t natrepsize(nat_mt n)
	{ return sizeof (struct natrep) + (n->nlimbs * sizeof n->limbs[0]); }
typedef const struct intrep *int_mt;
static inline size_t intrepsize(int_mt z)
	{ return sizeof (struct intrep) + (z->nlimbs * sizeof z->limbs[0]); }

/*
 * Create numeric values from strings.  These should only be used when
 * the strings are known to be lexically valid, e.g. when they come from
 * the front-end.
 */
extern nat_mt str2nat(const char *s);
extern int_mt str2int(const char *s);

/*
 * Convert numeric values to strings.  malloc'd, caller frees.
 */
extern char *nat2str(nat_mt n);
extern char *int2str(int_mt z);

/*
 * Bignum arithmetic.
 */
extern int_mt nat_pos(nat_mt n);
extern int_mt nat_neg(nat_mt n);
extern nat_mt nat_inc(nat_mt n);
extern nat_mt nat_dec(nat_mt n);
extern nat_mt nat_add(nat_mt m, nat_mt n);
extern nat_mt nat_sub(nat_mt m, nat_mt s);
extern nat_mt nat_mul(nat_mt m, nat_mt n);
extern void nat_divt_remt(nat_mt u, nat_mt v, nat_mt *q, nat_mt *r);
extern nat_mt nat_divt(nat_mt u, nat_mt v);
extern nat_mt nat_remt(nat_mt u, nat_mt v);
extern int nat_cmp(nat_mt m, nat_mt n);
extern int_mt int_abs(int_mt z);
extern nat_mt int_mag(int_mt z);
extern int_mt int_neg(int_mt z);
extern int_mt int_inc(int_mt z);
extern int_mt int_dec(int_mt z);
extern int_mt int_add(int_mt x, int_mt y);
extern int_mt int_sub(int_mt x, int_mt y);
extern int_mt int_mul(int_mt x, int_mt y);
extern void int_divt_remt(int_mt u, int_mt v, int_mt *q, int_mt *r);
extern int_mt int_divt(int_mt u, int_mt v);
extern int_mt int_remt(int_mt u, int_mt v);
extern int int_cmp(int_mt x, int_mt y);

static inline int nat_is_zero(nat_mt n)
	{ return n->nlimbs == 0; }

#endif /* LARK_VPU_BIGNUM_H */
