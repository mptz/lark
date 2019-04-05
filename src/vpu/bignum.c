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

#include <assert.h>
#include <math.h>
#include <signal.h>
#include <string.h>

#include <util/message.h>
#include <util/memutil.h>

#include "bignum.h"
#include "heap.h"

/*
 * Should this be global?  Default name is a mouthful.
 */
#define halloc heap_alloc_unmanaged_bytes

/*
 * Specialized versions of arithmetic operations for cases in which
 * some operands are of restricted type.
 */
static void nat_divt_remt1(nat_mt u, uint32_t v, nat_mt *q, uint32_t *r);
static nat_mt nat_mac1(nat_mt n, uint32_t m, uint32_t a);

static inline int
nat_is_zero(nat_mt n)
{
	return n->nlimbs == 0;
}

/*
 * Our representation should never have all-0 most-significant limbs, but
 * during operations when the result size is not known in advance, we
 * over-allocate and normalize later.
 */
static inline nat_mt
nat_normalize(struct natrep *n)
{
	while (n->nlimbs && !n->limbs[n->nlimbs-1])
		n->nlimbs--;
	return n;
}

/*
 * We could use a single value repeatedly if we were to register it
 * permanently with the heap.  The pointer we return might vary across GC
 * runs but the underlying block would be unchanged.
 */
static nat_mt
nat_zero(void)
{
	struct natrep *r = halloc(sizeof *r);
	r->nlimbs = 0;
	return r;
}

static nat_mt
nat_small(uint32_t limb)
{
	if (!limb) return nat_zero();
	struct natrep *r = halloc(sizeof *r + sizeof r->limbs[0]);
	r->nlimbs = 1;
	r->limbs[0] = limb;
	return r;
}

/*
 * Convert a string to a natural number; implemented as a radix conversion
 * of base 1,000,000,000 (the largest power of 10 which fits into a 32-bit
 * word) to base 2^32.  This means we convert the input string in blocks
 * of 9 characters at a time.
 */
nat_mt
str2nat(const char *s)
{
	const uint32_t base1b = 1000000000;
	size_t len = strlen(s);
	uint32_t w;

	/*
	 * Convert leading block of len % 9 digits.
	 */
	w = 0;
	switch (len % 9) {
	case 8: w = w * 10 + (*s++ - '0');
	case 7: w = w * 10 + (*s++ - '0');
	case 6: w = w * 10 + (*s++ - '0');
	case 5: w = w * 10 + (*s++ - '0');
	case 4: w = w * 10 + (*s++ - '0');
	case 3: w = w * 10 + (*s++ - '0');
	case 2: w = w * 10 + (*s++ - '0');
	case 1: w = w * 10 + (*s++ - '0');
	case 0: break;
	}

	nat_mt n = nat_small(w);
	heap_root_push(&n);

	/*
	 * Convert remaining base-1,000,000,000 limbs.
	 */
	for (len -= len % 9; len >= 9; len -= 9) {
		w = 0;
		for (const char *b = s + 9; s < b; /* nada */)
			w = w * 10 + (*s++ - '0');
		n = nat_mac1(n, base1b, w);
	}
	assert(len == 0);

	heap_root_pop(&n);
	return n;
}

/*
 * Convert a natural number to a string; implemented as a radix conversion
 * of base 2^32 to base 1,000,000,000 (the largest power of 10 which fits
 * into a 32-bit word).
 */
char *
nat2str(nat_mt n)
{
	/*
	 * Determine the maximum number of digits d we may need to represent
	 * a binary number of b bits, which is given by b times the base-10
	 * information content of a bit:
	 *		d = ceil( b * log_10(2) )
	 * Since floating point calculations are inexact, we add 1 as a
	 * lucky rabbit's foot.
	 */
	const unsigned d = ceil(n->nlimbs * 32 * log10(2.0)) + 1;
	char *s = xmalloc(d + 1 /* for trailing NUL */),
	     *p = s + d + 1;		/* --p will point to next character */
	*--p = '\0';			/* terminate output string */

	const uint32_t base1b = 1000000000;
	uint32_t r = 0;
	heap_root_push(&n);

	/*
	 * Multi-limb case.
	 */
	while (n->nlimbs > 1) {
		/*
		 * Specialized division which uses a single-limb divisor.
		 */
		nat_divt_remt1(n, base1b, &n, &r);
		assert(r < base1b);

		/*
		 * Convert from radix 1,000,000,000 to radix 10 within a word.
		 */
		for (unsigned i = 0; i < 9; ++i) {
			*--p = r % 10 + '0';
			r /= 10;
		}
	}

	/*
	 * Single-limb case.
 	 */
	uint32_t w = n->nlimbs ? n->limbs[0] : 0;
	for (/* nada */; w >= base1b; w /= base1b) {
		r = w % base1b;
		for (unsigned i = 0; i < 9; ++i) {
			*--p = r % 10 + '0';
			r /= 10;
		}
	}
	if (w) {
		/*
		 * Same conversion to base 10 as above without leading 0s.
		 */
		r = w;
		for (unsigned i = 0; r && i < 9; ++i) {
			*--p = r % 10 + '0';
			r /= 10;
		}
	}
	if (!*p) *--p = '0';	/* add a leading 0 for 0 itself */

	heap_root_pop(&n);
	p = xstrdup(p), free(s);
	return p;
}

#if 0
/*
 * Below are believed to work... but aren't currently used.
 */
nat_mt
word2nat(word w)
{
	if (w <= UINT32_MAX) return nat_small(w);
	assert(sizeof (word) == sizeof (uint64_t));
	struct natrep *r = halloc(sizeof *r + sizeof r->limbs[0] * 2);
	r->nlimbs = 2;
	r->limbs[0] = w;
	r->limbs[1] = ((uint64_t) w) >> 32;	/* cast avoids warning */
	return r;
}

word
nat2word_capped(nat_mt n)
{
	if (n->nlimbs == 0) return 0;
	if (n->nlimbs == 1) return n->limbs[0];
	if (sizeof (word) > sizeof (uint32_t)) {
		assert(sizeof (word) == sizeof (uint64_t));
		if (n->nlimbs == 2)
			return ((uint64_t) n->limbs[1]) << 32 |
			       n->limbs[0];
	}

	/* XXX need WORD_MAX */
	assert(sizeof (word) == sizeof (uintptr_t));
	return UINTPTR_MAX;
}
#endif

int_mt
nat_pos(nat_mt n)
{
	heap_root_push(&n);
	struct intrep *r = halloc(sizeof *r + sizeof r->limbs[0] * n->nlimbs);
	heap_root_pop(&n);
	r->sign = +1;
	r->nlimbs = n->nlimbs;
	memcpy(r->limbs, n->limbs, sizeof r->limbs[0] * r->nlimbs);
	return r;
}

int_mt
nat_neg(nat_mt n)
{
	heap_root_push(&n);
	struct intrep *r = halloc(sizeof *r + sizeof r->limbs[0] * n->nlimbs);
	heap_root_pop(&n);
	r->sign = nat_is_zero(n) ? +1 : -1;
	r->nlimbs = n->nlimbs;
	memcpy(r->limbs, n->limbs, sizeof r->limbs[0] * r->nlimbs);
	return r;
}

nat_mt
nat_inc(nat_mt n)
{
	if (nat_is_zero(n))
		return nat_small(1);

	/*
	 * The number of the words in the result should be the same as in
	 * the input, unless carry-out is possible.  If there is at least
	 * one zero in the high word to absorb the increment, a carry can't
	 * occur (the number must be all 1's for an increment to carry out).
	 */
	assert(n->nlimbs);
	unsigned extra = ! ~n->limbs[n->nlimbs - 1];
	size_t rlimbs = n->nlimbs + extra;
	heap_root_push(&n);
	struct natrep *r = halloc(sizeof *r + sizeof r->limbs[0] * rlimbs);
	heap_root_pop(&n);
	r->nlimbs = rlimbs;

	/*
	 * Increment LSW to MSW, then handle carry at the end.
	 */
	size_t i = 0;
	unsigned carry = 1;
	for (/* nada */; carry && i < n->nlimbs; ++i) {
		r->limbs[i] = n->limbs[i] + carry;
		carry = !r->limbs[i];
	}
	for (/* nada */; i < n->nlimbs; ++i)
		r->limbs[i] = n->limbs[i];
	if (carry) {
		assert(carry == 1);
		assert(r->nlimbs == n->nlimbs + 1);
		r->limbs[i] = carry;
	} else if (extra)
		r->nlimbs--;		/* make r as long as n */

	return r;
}

nat_mt
nat_dec(nat_mt n)
{
	/*
	 * Subtraction on nats is only defined for positive n.
	 */
	if (nat_is_zero(n)) {
		raise(SIGFPE);
		panic("Nat decrement not defined for zero\n");
	}

	heap_root_push(&n);
	struct natrep *r = halloc(sizeof *r + sizeof r->limbs[0] * n->nlimbs);
	heap_root_pop(&n);
	r->nlimbs = n->nlimbs;

	/*
	 * Subtract one by starting with a borrow.
	 */
	size_t i = 0;
	int borrow = 1;
	for (/* nada */; borrow && i < n->nlimbs; ++i) {
		r->limbs[i] = n->limbs[i] - borrow;
		borrow = n->limbs[i] == 0;
	}
	for (/* nada */; i < n->nlimbs; ++i)
		r->limbs[i] = n->limbs[i];
	assert(!borrow);	/* since n > 0 */

	/*
	 * Normalize by decreasing nlimbs if we decremented to a number
	 * with a shorter representation.
	 */
	assert(r->nlimbs);
	if (r->limbs[r->nlimbs-1] == 0)
		r->nlimbs--;
	return r;
}

nat_mt
nat_add(nat_mt m, nat_mt n)
{
	/*
	 * To simplify cases below, m is at least as long as n.
	 */
	if (m->nlimbs < n->nlimbs) {
		nat_mt t = m;
		m = n, n = t;
	}
	assert(m->nlimbs >= n->nlimbs);

	/*
	 * Do this before we examine the last limb for possible carry.
	 */
	if (nat_is_zero(n))
		return m;

	/*
	 * The number of the words in the result should be the same as in
	 * the larger of the two inputs, unless carry-out is possible.
	 * If the numbers have the same number of limbs, we provide room
	 * for carry-out without further testing; if m is longer, we
	 * provide room for carry-out if m has no 0 bits in its high limb.
	 */
	assert(m->nlimbs && n->nlimbs);
	unsigned extra = (m->nlimbs == n->nlimbs) ||
			 ! ~m->limbs[m->nlimbs - 1];
	size_t rlimbs = m->nlimbs + extra;
	heap_root_push(&m), heap_root_push(&n);
	struct natrep *r = halloc(sizeof *r + sizeof r->limbs[0] * rlimbs);
	heap_root_pop(&n), heap_root_pop(&m);
	r->nlimbs = rlimbs;

	/*
	 * Add the overlapping words, then the tail of m, then the carry.
	 */
	size_t i = 0;
	unsigned carry = 0;
	for (/* nada */; i < n->nlimbs; ++i) {
		r->limbs[i] = m->limbs[i] + n->limbs[i] + carry;
		/*
		 * The presence of a carry-in determines the inequality used
		 * to test for carry-out.  Without a carry-in, in order to
		 * wrap around completely so as to satisfy the equations
		 *	r == m + n	(mod b), and
		 *	r == m		(mod b)
		 * n would have to be the base b, an impossibility.  With
		 * a carry-in of 1, n could be the representable b-1 in
		 *	r == m + n + c	(mod b), and
		 *	r == m		(mod b).
		 * However, note in this case that since c is 1, the case
		 *	r == m
		 * indicates a carry-out; we're adding at least 1 to m, so
		 * without a carry-out the sum should be strictly greater
		 * than m.
		 */
		carry = carry ? r->limbs[i] <= m->limbs[i]
			      : r->limbs[i] <  m->limbs[i];
	}
	for (/* nada */; i < m->nlimbs; ++i) {
		r->limbs[i] = m->limbs[i] + carry;
		carry = r->limbs[i] < m->limbs[i];
	}
	if (carry) {
		assert(r->nlimbs == m->nlimbs + 1);
		r->limbs[i] = carry;
	} else if (extra)
		r->nlimbs--;		/* make r as long as m */

	return r;
}

nat_mt
nat_sub(nat_mt m, nat_mt s)
{
	/*
	 * Subtraction on nats is only defined when m >= s.  We could perhaps
	 * trap this more efficiently below without a separate nat_cmp call.
	 */
	if (nat_cmp(m, s) < 0) {
		raise(SIGFPE);
		panic("Nat subtraction not defined for subtrahend > minuend\n");
	}

	heap_root_push(&m), heap_root_push(&s);
	struct natrep *r = halloc(sizeof *r + sizeof r->limbs[0] * m->nlimbs);
	heap_root_pop(&s), heap_root_pop(&m);
	r->nlimbs = m->nlimbs;

	/*
	 * Subtract the overlapping words, then handle tail of m & borrow.
	 */
	size_t i = 0;
	int borrow = 0;
	for (/* nada */; i < s->nlimbs; ++i) {
		r->limbs[i] = m->limbs[i] - s->limbs[i] - borrow;
		/*
		 * See the carry calculation comment above; a similar
		 * situation arises here.
		 *
		 * Where could our borrow calculation go wrong?  Let's
		 * abbreviate m for minuend, s for subtrahend, w for
		 * borrow, and b for the representation base.  Naively,
		 * we need to borrow when m - w < s, or equivalently when
		 * m < s + w.  But in modular arithmetic, the former could
		 * underflow when m == 0, while the latter could overflow
		 * when s == 2^b - 1.  These under/overflow conditions,
		 * can only arise with nonzero borrow.
		 *
		 * So, when borrow is 0, we can safely test m < s.
		 * When borrow is nonzero, we test m <= s instead since
		 * the m == s case leads to a borrow.
		 */
		borrow = borrow ? m->limbs[i] <= s->limbs[i]
				: m->limbs[i] <  s->limbs[i];
	}
	for (/* nada */; i < m->nlimbs; ++i) {
		r->limbs[i] = m->limbs[i] - borrow;
		borrow = m->limbs[i] < borrow;
	}
	assert(!borrow);	/* since m >= s */

	return nat_normalize(r);
}

/*
 * Multiply-accumulate with single-limb multiplicand and addend.
 */
static nat_mt
nat_mac1(nat_mt n, uint32_t m, uint32_t a)
{
	if (nat_is_zero(n) || m == 0)
		return nat_small(a);

	size_t rlimbs = n->nlimbs + 1;	/* for one-limb multiplicand */
	heap_root_push(&n);
	struct natrep *r = halloc(sizeof *r + sizeof r->limbs[0] * rlimbs);
	heap_root_pop(&n);
	r->nlimbs = rlimbs;

	size_t i;
	r->limbs[0] = a;
	for (i = 1; i < rlimbs; ++i)
		r->limbs[i] = 0;
	uint32_t carry = 0;
	for (i = 0; i < n->nlimbs; ++i) {
		/*
		 * Note that this can't overflow out of a 64-bit product;
		 * consider, for the largest number x = 2^b - 1 that fits
		 * in a b-bit word,
		 *
		 *	x * x + x + x	= x^2 + 2x
		 *
		 * ... while the overflow threshold for a 2b-bit word is:
		 *
		 *	2^(2b)		= 2^b * 2^b
		 *			= (x + 1) * (x + 1)
		 *			= x^2 * 2x + 1
		 *
		 * which is one greater than the value derived above.  QED.
		 */
		uint64_t prod = ((uint64_t) n->limbs[i]) * ((uint64_t) m) +
				((uint64_t) r->limbs[i]) + ((uint64_t) carry);
		r->limbs[i] = prod;
		carry = (prod >> 32);
	}
	r->limbs[i] = carry;

	return nat_normalize(r);
}

/*
 * Blackboard multiplication.  O(N^2) but simple.
 *
 * Asymptotically faster algorithms (Karatsuba, Toom-Cook, Schonhage-
 * Strassen) exist but if/when those are implemented we may want to make
 * use conditional on magnitude, as their constant factors are higher.
 */
nat_mt
nat_mul(nat_mt m, nat_mt n)
{
	/*
	 * To simplify cases below, m is at least as long as n.
	 */
	if (m->nlimbs < n->nlimbs) {
		nat_mt t = m;
		m = n, n = t;
	}

	if (nat_is_zero(n))
		return nat_zero();
	else if (n->nlimbs == 1)
		return nat_mac1(m, n->limbs[0], 0);

	size_t rlimbs = m->nlimbs + n->nlimbs;
	heap_root_push(&m), heap_root_push(&n);
	struct natrep *r = halloc(sizeof *r + sizeof r->limbs[0] * rlimbs);
	heap_root_pop(&n), heap_root_pop(&m);
	r->nlimbs = rlimbs;

	size_t i;
	for (i = 0; i < rlimbs; ++i)
		r->limbs[i] = 0;
	for (i = 0; i < n->nlimbs; ++i) {
		uint32_t carry = 0;
		size_t j;
		for (j = 0; j < m->nlimbs; ++j) {
			uint64_t prod = ((uint64_t) m->limbs[j]) *
					((uint64_t) n->limbs[i]) +
					((uint64_t) r->limbs[i+j]) +
					((uint64_t) carry);
			r->limbs[i+j] = prod;
			carry = (prod >> 32);
		}
		r->limbs[i+j] = carry;
	}

	return nat_normalize(r);
}

void
nat_divt_remt1(nat_mt u, uint32_t v, nat_mt *q, uint32_t *r)
{
	/*
	 * Dispatch the special cases up front.
 	 */
	if (v == 0) {
		raise(SIGFPE);
		panic("Nat division not defined for a divisor of 0\n");
	}
	if (nat_is_zero(u)) {
		/*
		 * I believe this code is correct, but it's not reachable with
		 * the current code base.  If it becomes reachable, remove this
		 * marker and make sure a unit test covers it.
		 */
		unreachable();
		*q = nat_zero();
		if (r) *r = 0;
		return;
	}

	heap_root_push(&u);
	struct natrep *x = halloc(sizeof *x + sizeof x->limbs[0] * u->nlimbs);
	heap_root_pop(&u);
	x->nlimbs = u->nlimbs;

	/*
	 * Handle the dividend 32 bits at a time using 64-bit divide.
	 */
	uint64_t remainder = 0;
	for (size_t i = u->nlimbs; i--; /* nada */) {
		uint64_t d = (remainder << 32) | u->limbs[i];
		x->limbs[i] = d / v;
		remainder = d - x->limbs[i] * (uint64_t) v;
	}
	assert(remainder <= UINT32_MAX);
	assert(remainder < v);

	*q = nat_normalize(x);
	if (r) *r = remainder;
}

void
nat_divt_remt(nat_mt u, nat_mt v, nat_mt *q, nat_mt *r)
{
	uint32_t carry, i, j;

	if (nat_is_zero(v)) {
		raise(SIGFPE);
		panic("Nat division not defined for a divisor of 0\n");
	}

	/*
	 * After handling this case we know the dividend is numerically
	 * at least as big as the divisor, which implies it also has at
	 * least as many limbs.
	 */
	if (nat_cmp(u, v) < 0) {
		heap_root_push(&u);
		*q = nat_zero();
		if (r) *r = u;
		heap_root_pop(&u);
		return;
	}

	/*
	 * Dispatch to nat_divt_remt1 for small divisors.
	 */
	if (v->nlimbs == 1) {
		uint32_t r32;
		nat_divt_remt1(u, v->limbs[0], q, &r32);
		if (r) {
			heap_root_push(q);
			*r = nat_small(r32);
			heap_root_pop(q);
		}
		return;
	}

	/*
	 * Divisor normalization; ensure most-significant-bit of divisor
	 * is set by multiplying both dividend and divisor by an appropriate
	 * power of 2.  We open code this left-shift because we have unusual
	 * requirements (truncating, adding a denormal 0 word as the MSW
	 * of u for the algorithm below) which tell against our exposing
	 * this particular shift as a generic LSL operator.
	 */
	const size_t vn = v->nlimbs;
	const uint32_t shift = __builtin_clz(v->limbs[vn-1]);
	uint32_t *vw = xmalloc(sizeof *vw * vn);
	for (carry = 0, i = 0; i < vn; ++i) {
		uint64_t bits = ((uint64_t) v->limbs[i]) << shift;
		vw[i] = bits | carry;
		carry = bits >> 32;
	}
	assert(carry == 0);		  /* should have shifted just enough */
	assert(vw[vn - 1] & 0x80000000);  /* the whole point of normalization */

	const size_t un = u->nlimbs + 1;  /* extra room needed below */
	uint32_t *uw = xmalloc(sizeof *uw * un);
	for (carry = 0, i = 0; i < un - 1; ++i) {
		uint64_t bits = ((uint64_t) u->limbs[i]) << shift;
		uw[i] = bits | carry;
		carry = bits >> 32;
	}
	uw[i] = carry;

	/*
	 * Allocate the quotient.  We don't allocate the remainder as a nat
	 * unless the caller requested it; it gets incrementally generated
	 * in one of the (C runtime heap-allocated) structures above.  Note
	 * that after this allocation u and v are clobbered; we don't need them
	 * anymore since we're now working with the normalized versions.
	 */
	u = v = NULL;		/* may be clobbered, no cheating! */
	struct natrep *qr = halloc(sizeof *qr + sizeof qr->limbs[0] * (un-vn));
	qr->nlimbs = un - vn;

	/*
	 * This is the main loop of the division.
 	 */
	const uint64_t modulus = 1ull << 32;
	for (i = un - vn; i--; /* nada */) {
		/*
		 * Estimate the next limbs of the quotient and remainder using
		 * trial division of the most-significant limbs of the dividend
		 * and divisor.  A detailed discussion of this estimation
		 * process can be found in Knuth's presentation of Algorithm D.
		 */
		uint64_t term = ((uint64_t) uw[vn + i]) << 32 |
				((uint64_t) uw[vn + i - 1]),
			 qhat = term / (uint64_t) vw[vn - 1],
			 rhat = term - qhat * (uint64_t) vw[vn - 1];
	refine:
		if (qhat >= modulus || 
		    qhat * vw[vn - 2] > rhat * modulus + uw[i + vn - 2]) {
			--qhat;
			rhat += vw[vn - 1];
			if (rhat < modulus) goto refine;
		}

		/*
		 * Multiply divisor by estimated quotient digit and
		 * subtract from dividend.
		 */
		assert(qhat < modulus);
		/*
		 * Really need to audit & test the signed vs. unsigned here.
		 */
		int64_t try;
		for (carry = 0, j = 0; j < vn; ++j) {
			uint64_t prod = qhat * ((uint64_t) vw[j]);
			try = ((int64_t) uw[i+j]) -
			      ((int64_t) carry) -
			      ((int64_t) (prod & UINT32_MAX));
			uw[i+j] = try;
			carry = (prod >> 32) - (try >> 32);
		}
		try = ((int64_t) uw[i+j]) - ((int64_t) carry);
		uw[i+j] = try;

		/*
		 * Record the next digit of the quotient; in very rare cases
		 * (probability about 1/2^31 per random word) our quotient
		 * estimate overshot by 1, in which case we need to walk that
		 * back out.
		 */
		qr->limbs[i] = qhat;
		if (try < 0) {
			qr->limbs[i] -= 1;
			for (carry = 0, j = 0; j < vn; ++j) {
				try = ((uint64_t) uw[i+j]) +
				      ((uint64_t) vw[j]) +
				      ((uint64_t) carry);
				uw[i+j] = try;
				carry = try >> 32;
			}
			uw[i+j] += carry;
		}
	}

	*q = nat_normalize(qr);
	if (r) {
		heap_root_push(q);
		struct natrep *rr = halloc(sizeof *rr +
					   sizeof rr->limbs[0] * vn);
		heap_root_pop(q);
		rr->nlimbs = vn;
		const uint32_t unshift = 32 - shift;
		for (carry = 0, i = vn; i--; /* nada */) {
			uint64_t bits = ((uint64_t) uw[i]) << unshift;
			rr->limbs[i] = (bits >> 32) | carry;
			carry = bits;
		}
		*r = nat_normalize(rr);
	}
	free(vw), free(uw);
}

nat_mt
nat_divt(nat_mt m, nat_mt n)
{
	nat_mt q;
	nat_divt_remt(m, n, &q, NULL);
	return q;
}

nat_mt
nat_remt(nat_mt m, nat_mt n)
{
	nat_mt q, r;
	nat_divt_remt(m, n, &q, &r);
	return r;
}

int
nat_cmp(nat_mt m, nat_mt n)
{
	if (m == n)
		return 0;
	if (m->nlimbs < n->nlimbs)
		return -1;
	if (m->nlimbs > n->nlimbs)
		return +1;
	for (size_t i = m->nlimbs; i--; /* nada */) {
		if (m->limbs[i] < n->limbs[i])
			return -1;
		if (m->limbs[i] > n->limbs[i])
			return +1;
	}
	return 0;
}

/*
 * The int code below is a pretty brutal cut, paste, and hack of the nat
 * code above.  If you fix a bug, fix it in both places.  Documentation
 * has been stripped below to reduce redundancy... look for the comments
 * in the corresponding nat code--this is a reminder that this code is
 * not the master copy.
 *
 * Luckily the specs of natural numbers and integers are slow to change!
 */

static void int_divt_remt1(int_mt u, uint32_t v,
			   int_mt *q, uint32_t *r, int sign);
static int_mt int_mac1(int_mt n, uint32_t m, uint32_t a, int sign);

static inline int
int_is_zero(int_mt z)
{
	return z->nlimbs == 0;
}

static inline int_mt
int_normalize(struct intrep *z)
{
	while (z->nlimbs && !z->limbs[z->nlimbs-1])
		z->nlimbs--;
	/* normalize int zero to +0 */
	if (z->nlimbs == 0) z->sign = +1;
	return z;
}

static int_mt
int_zero(int sign)
{
	struct intrep *r = halloc(sizeof *r);
	r->sign = sign;
	r->nlimbs = 0;
	return r;
}

static int_mt
int_small(uint32_t limb, int sign)
{
	if (!limb) return int_zero(sign);
	struct intrep *r = halloc(sizeof *r + sizeof r->limbs[0]);
	r->sign = sign;
	r->nlimbs = 1;
	r->limbs[0] = limb;
	return r;
}

int_mt
str2int(const char *s)
{
	const uint32_t base1b = 1000000000;
	size_t len = strlen(s);
	uint32_t w;
	int sign = +1;

	/*
	 * Unlike nats, consume optional leading sign.  This code accepts
	 * positive integers which aren't prefixed by '+', although the
	 * language syntax rejects them.
	 */
	assert(len);
	if (*s == '+')
		++s, --len;
	else if (*s == '-')
		sign = -1, ++s, --len;

	w = 0;
	switch (len % 9) {
	case 8: w = w * 10 + (*s++ - '0');
	case 7: w = w * 10 + (*s++ - '0');
	case 6: w = w * 10 + (*s++ - '0');
	case 5: w = w * 10 + (*s++ - '0');
	case 4: w = w * 10 + (*s++ - '0');
	case 3: w = w * 10 + (*s++ - '0');
	case 2: w = w * 10 + (*s++ - '0');
	case 1: w = w * 10 + (*s++ - '0');
	case 0: break;
	}

	int_mt z = int_small(w, sign);
	heap_root_push(&z);

	/*
	 * Convert remaining base-1,000,000,000 limbs.
	 */
	for (len -= len % 9; len >= 9; len -= 9) {
		w = 0;
		for (const char *b = s + 9; s < b; /* nada */)
			w = w * 10 + (*s++ - '0');
		z = int_mac1(z, base1b, w, z->sign);
	}
	assert(len == 0);

	heap_root_pop(&z);
	return int_is_zero(z) ? int_zero(+1) : z;	/* avoid -0 */
}

char *
int2str(int_mt z)
{
	const unsigned d = ceil(z->nlimbs * 32 * log10(2.0)) + 1;
	char *s = xmalloc(d + 2 /* for sign & trailing NUL */),
	     *p = s + d + 2;		/* --p will point to next character */
	*--p = '\0';			/* terminate output string */

	const uint32_t base1b = 1000000000;
	uint32_t r = 0;
	heap_root_push(&z);

	/*
	 * Multi-limb case.
	 */
	while (z->nlimbs > 1) {
		/*
		 * Specialized division which uses a single-limb divisor.
		 */
		int_divt_remt1(z, base1b, &z, &r, z->sign);
		assert(r < base1b);

		/*
		 * Convert from radix 1,000,000,000 to radix 10 within a word.
		 */
		for (unsigned i = 0; i < 9; ++i) {
			*--p = r % 10 + '0';
			r /= 10;
		}
	}

	/*
	 * Single-limb case.
 	 */
	uint32_t w = z->nlimbs ? z->limbs[0] : 0;
	for (/* nada */; w >= base1b; w /= base1b) {
		r = w % base1b;
		for (unsigned i = 0; i < 9; ++i) {
			*--p = r % 10 + '0';
			r /= 10;
		}
	}
	if (w) {
		/*
		 * Same conversion to base 10 as above without leading 0s.
		 */
		r = w;
		for (unsigned i = 0; r && i < 9; ++i) {
			*--p = r % 10 + '0';
			r /= 10;
		}
	}
	if (!*p) *--p = '0';	/* add a leading 0 for 0 itself */

	/*
	 * Sign.
	 */
	*--p = z->sign < 0 ? '-' : '+';

	heap_root_pop(&z);
	p = xstrdup(p), free(s);
	return p;
}

int_mt
int_abs(int_mt z)
{
	assert(z->sign == +1 || z->sign == -1);
	assert(z->sign == +1 || !int_is_zero(z));
	if (z->sign >= 0)
		return z;

	heap_root_push(&z);
	struct intrep *r = halloc(sizeof *r + sizeof r->limbs[0] * z->nlimbs);
	heap_root_pop(&z);
	r->sign = +1;
	r->nlimbs = z->nlimbs;
	memcpy(r->limbs, z->limbs, sizeof r->limbs[0] * r->nlimbs);
	return r;
}

nat_mt
int_mag(int_mt z)
{
	assert(z->sign == +1 || z->sign == -1);
	assert(z->sign == +1 || !int_is_zero(z));

	heap_root_push(&z);
	struct natrep *r = halloc(sizeof *r + sizeof r->limbs[0] * z->nlimbs);
	heap_root_pop(&z);
	r->nlimbs = z->nlimbs;
	memcpy(r->limbs, z->limbs, sizeof r->limbs[0] * r->nlimbs);
	return r;
}

/*
 * Increment magnitude, i.e. away from zero; preserve sign.
 */
static int_mt
int_inc_mag(int_mt z)
{
	/* caller-ensured */
	assert(!int_is_zero(z));

	assert(z->nlimbs);
	unsigned extra = ! ~z->limbs[z->nlimbs - 1];
	size_t rlimbs = z->nlimbs + extra;
	heap_root_push(&z);
	struct intrep *r = halloc(sizeof *r + sizeof r->limbs[0] * rlimbs);
	heap_root_pop(&z);
	r->sign = z->sign;
	r->nlimbs = rlimbs;

	size_t i = 0;
	unsigned carry = 1;
	for (/* nada */; carry && i < z->nlimbs; ++i) {
		r->limbs[i] = z->limbs[i] + carry;
		carry = !r->limbs[i];
	}
	for (/* nada */; i < z->nlimbs; ++i)
		r->limbs[i] = z->limbs[i];
	if (carry) {
		assert(carry == 1);
		assert(r->nlimbs == z->nlimbs + 1);
		r->limbs[i] = carry;
	} else if (extra)
		r->nlimbs--;		/* make r as long as z */

	return r;
}

/*
 * Decrement magnitude, i.e. towards zero; preserve sign.
 */
static int_mt
int_dec_mag(int_mt z)
{
	/* caller-ensured */
	assert(!int_is_zero(z));

	heap_root_push(&z);
	struct intrep *r = halloc(sizeof *r + sizeof r->limbs[0] * z->nlimbs);
	heap_root_pop(&z);
	r->sign = z->sign;
	r->nlimbs = z->nlimbs;

	/*
	 * Subtract one by starting with a borrow.
	 */
	size_t i = 0;
	int borrow = 1;
	for (/* nada */; borrow && i < z->nlimbs; ++i) {
		r->limbs[i] = z->limbs[i] - borrow;
		borrow = z->limbs[i] == 0;
	}
	for (/* nada */; i < z->nlimbs; ++i)
		r->limbs[i] = z->limbs[i];
	assert(!borrow);	/* since z != 0 */

	/*
	 * Normalize by decreasing nlimbs if we decremented to a number
	 * with a shorter representation.  If we decremented to zero,
	 * ensure sign is positive.
	 */
	assert(r->nlimbs);
	if (r->limbs[r->nlimbs-1] == 0)
		r->nlimbs--;
	if (r->nlimbs == 0)
		r->sign = +1;
	return r;
}

int_mt
int_neg(int_mt z)
{
	if (int_is_zero(z)) return z;
	heap_root_push(&z);
	struct intrep *r = halloc(sizeof *r + sizeof r->limbs[0] * z->nlimbs);
	heap_root_pop(&z);
	r->sign = -z->sign;
	r->nlimbs = z->nlimbs;
	memcpy(r->limbs, z->limbs, sizeof r->limbs[0] * r->nlimbs);
	return r;
}

int_mt
int_inc(int_mt z)
{
	return	int_is_zero(z) ? int_small(1, +1) :
		z->sign < 0 ? int_dec_mag(z) :
		int_inc_mag(z);
}

int_mt
int_dec(int_mt z)
{
	return	int_is_zero(z) ? int_small(1, -1) :
		z->sign > 0 ? int_dec_mag(z) :
		int_inc_mag(z);
}

static inline int
int_cmp_mag(int_mt x, int_mt y)
{
	if (x == y)
		return 0;
	if (x->nlimbs < y->nlimbs)
		return -1;
	if (x->nlimbs > y->nlimbs)
		return +1;
	for (size_t i = x->nlimbs; i--; /* nada */) {
		if (x->limbs[i] < y->limbs[i])
			return -1;
		if (x->limbs[i] > y->limbs[i])
			return +1;
	}
	return 0;
}

static int_mt
int_add_mag(int_mt x, int_mt y, int sign)
{
	/*
	 * The caller ensures that neither is zero, and that x is at
	 * least as long as y.  The code below actually works with
	 * x < y as long as they have the same number of limbs.
	 */
	assert(!int_is_zero(x));
	assert(!int_is_zero(y));
	assert(x->nlimbs >= y->nlimbs);

	/*
	 * For comments on the code below, refer to nat_add().
	 */
	assert(x->nlimbs && y->nlimbs);
	unsigned extra = (x->nlimbs == y->nlimbs) ||
			 ! ~x->limbs[x->nlimbs - 1];
	size_t rlimbs = x->nlimbs + extra;
	heap_root_push(&x), heap_root_push(&y);
	struct intrep *r = halloc(sizeof *r + sizeof r->limbs[0] * rlimbs);
	heap_root_pop(&y), heap_root_pop(&x);
	r->sign = sign;
	r->nlimbs = rlimbs;

	size_t i = 0;
	unsigned carry = 0;
	for (/* nada */; i < y->nlimbs; ++i) {
		r->limbs[i] = x->limbs[i] + y->limbs[i] + carry;
		carry = carry ? r->limbs[i] <= x->limbs[i]
			      : r->limbs[i] <  x->limbs[i];
	}
	for (/* nada */; i < x->nlimbs; ++i) {
		r->limbs[i] = x->limbs[i] + carry;
		carry = r->limbs[i] < x->limbs[i];
	}
	if (carry) {
		assert(r->nlimbs == x->nlimbs + 1);
		r->limbs[i] = carry;
	} else if (extra)
		r->nlimbs--;		/* make r as long as x */

	return r;
}

int_mt
int_sub_mag(int_mt x, int_mt y, int sign)
{
	/*
	 * The caller ensures that neither is zero, and that x's magnitude
	 * is greater than or equal to y's.
	 */
	assert(!int_is_zero(x));
	assert(!int_is_zero(y));
	assert(x->nlimbs >= y->nlimbs);

	/*
	 * For comments on the code below, refer to nat_sub().
	 */
	heap_root_push(&x), heap_root_push(&y);
	struct intrep *r = halloc(sizeof *r + sizeof r->limbs[0] * x->nlimbs);
	heap_root_pop(&y), heap_root_pop(&x);
	r->sign = sign;
	r->nlimbs = x->nlimbs;

	size_t i = 0;
	int borrow = 0;
	for (/* nada */; i < y->nlimbs; ++i) {
		r->limbs[i] = x->limbs[i] - y->limbs[i] - borrow;
		borrow = borrow ? x->limbs[i] <= y->limbs[i]
				: x->limbs[i] <  y->limbs[i];
	}
	for (/* nada */; i < x->nlimbs; ++i) {
		r->limbs[i] = x->limbs[i] - borrow;
		borrow = x->limbs[i] < borrow;
	}
	assert(!borrow);	/* since m >= s */

	return int_normalize(r);
}

int_mt
int_add(int_mt x, int_mt y)
{
	if (int_is_zero(x)) return y;
	if (int_is_zero(y)) return x;
	if (x->sign == y->sign)
		return	x->nlimbs < y->nlimbs ?
			int_add_mag(y, x, x->sign) :
			int_add_mag(x, y, x->sign);
	return	int_cmp_mag(x, y) < 0 ?
		int_sub_mag(y, x, y->sign) :
		int_sub_mag(x, y, x->sign);
}

int_mt
int_sub(int_mt x, int_mt y)
{
	if (int_is_zero(y)) return x;
	if (int_is_zero(x)) return int_neg(y);
	if (x->sign != y->sign)
		return	x->nlimbs < y->nlimbs ?
			int_add_mag(y, x, x->sign) :
			int_add_mag(x, y, x->sign);
	return	int_cmp_mag(x, y) < 0 ?
		int_sub_mag(y, x, -x->sign) :
		int_sub_mag(x, y, +x->sign);
}

static int_mt
int_mac1(int_mt z, uint32_t m, uint32_t a, int sign)
{
	if (int_is_zero(z) || m == 0)
		return int_small(a, z->sign);

	size_t rlimbs = z->nlimbs + 1;	/* for one-limb multiplicand */
	heap_root_push(&z);
	struct intrep *r = halloc(sizeof *r + sizeof r->limbs[0] * rlimbs);
	heap_root_pop(&z);
	r->sign = sign;
	r->nlimbs = rlimbs;

	size_t i;
	r->limbs[0] = a;
	for (i = 1; i < rlimbs; ++i)
		r->limbs[i] = 0;
	uint32_t carry = 0;
	for (i = 0; i < z->nlimbs; ++i) {
		uint64_t prod = ((uint64_t) z->limbs[i]) * ((uint64_t) m) +
				((uint64_t) r->limbs[i]) + ((uint64_t) carry);
		r->limbs[i] = prod;
		carry = (prod >> 32);
	}
	r->limbs[i] = carry;

	return int_normalize(r);
}

int_mt
int_mul(int_mt x, int_mt y)
{
	/*
	 * To simplify cases below, x is at least as long as y.
	 */
	if (x->nlimbs < y->nlimbs) {
		int_mt t = x;
		x = y, y = t;
	}

	if (int_is_zero(y))
		return int_zero(+1);

	int sign = x->sign == y->sign ? +1 : -1;
	if (y->nlimbs == 1)
		return int_mac1(x, y->limbs[0], 0, sign);

	size_t rlimbs = x->nlimbs + y->nlimbs;
	heap_root_push(&x), heap_root_push(&y);
	struct intrep *r = halloc(sizeof *r + sizeof r->limbs[0] * rlimbs);
	heap_root_pop(&y), heap_root_pop(&x);
	r->sign = sign;
	r->nlimbs = rlimbs;

	size_t i;
	for (i = 0; i < rlimbs; ++i)
		r->limbs[i] = 0;
	for (i = 0; i < y->nlimbs; ++i) {
		uint32_t carry = 0;
		size_t j;
		for (j = 0; j < x->nlimbs; ++j) {
			uint64_t prod = ((uint64_t) x->limbs[j]) *
					((uint64_t) y->limbs[i]) +
					((uint64_t) r->limbs[i+j]) +
					((uint64_t) carry);
			r->limbs[i+j] = prod;
			carry = (prod >> 32);
		}
		r->limbs[i+j] = carry;
	}

	return int_normalize(r);
}

void
int_divt_remt1(int_mt u, uint32_t v, int_mt *q, uint32_t *r, int sign)
{
	/*
	 * Dispatch the special cases up front.
 	 */
	if (v == 0) {
		raise(SIGFPE);
		panic("Int division not defined for a divisor of 0\n");
	}
	if (int_is_zero(u)) {
		/*
		 * I believe this code is correct, but it's not reachable with
		 * the current code base.  If it becomes reachable, remove this
		 * marker and make sure a unit test covers it.
		 */
		unreachable();
		*q = int_zero(+1);
		if (r) *r = 0;
		return;
	}

	heap_root_push(&u);
	struct intrep *x = halloc(sizeof *x + sizeof x->limbs[0] * u->nlimbs);
	heap_root_pop(&u);
	x->sign = sign;
	x->nlimbs = u->nlimbs;

	/*
	 * Handle the dividend 32 bits at a time using 64-bit divide.
	 */
	uint64_t remainder = 0;
	for (size_t i = u->nlimbs; i--; /* nada */) {
		uint64_t d = (remainder << 32) | u->limbs[i];
		x->limbs[i] = d / v;
		remainder = d - x->limbs[i] * (uint64_t) v;
	}
	assert(remainder <= UINT32_MAX);
	assert(remainder < v);

	*q = int_normalize(x);
	if (r) *r = remainder;
}

void
int_divt_remt(int_mt u, int_mt v, int_mt *q, int_mt *r)
{
	uint32_t carry, i, j;

	if (int_is_zero(v)) {
		raise(SIGFPE);
		panic("Nat division not defined for a divisor of 0\n");
	}
	int qsign = u->sign == v->sign ? +1 : -1,
	    rsign = u->sign;

	/*
	 * After handling this case we know the dividend is numerically
	 * at least as big as the divisor, which implies it also has at
	 * least as many limbs.
	 */
	if (int_cmp_mag(u, v) < 0) {
		heap_root_push(&u);
		*q = int_zero(+1);
		if (r) *r = u;
		heap_root_pop(&u);
		return;
	}

	/*
	 * Dispatch to int_divt_remt1 for small divisors.
	 */
	if (v->nlimbs == 1) {
		uint32_t r32;
		int_divt_remt1(u, v->limbs[0], q, &r32, qsign);
		if (r) {
			heap_root_push(q);
			*r = r32 ? int_small(r32, rsign) : int_zero(+1);
			heap_root_pop(q);
		}
		return;
	}

	/*
	 * Divisor normalization; ensure most-significant-bit of divisor
	 * is set by multiplying both dividend and divisor by an appropriate
	 * power of 2.  We open code this left-shift because we have unusual
	 * requirements (truncating, adding a denormal 0 word as the MSW
	 * of u for the algorithm below) which tell against our exposing
	 * this particular shift as a generic LSL operator.
	 */
	const size_t vn = v->nlimbs;
	const uint32_t shift = __builtin_clz(v->limbs[vn-1]);
	uint32_t *vw = xmalloc(sizeof *vw * vn);
	for (carry = 0, i = 0; i < vn; ++i) {
		uint64_t bits = ((uint64_t) v->limbs[i]) << shift;
		vw[i] = bits | carry;
		carry = bits >> 32;
	}
	assert(carry == 0);		  /* should have shifted just enough */
	assert(vw[vn - 1] & 0x80000000);  /* the whole point of normalization */

	const size_t un = u->nlimbs + 1;  /* extra room needed below */
	uint32_t *uw = xmalloc(sizeof *uw * un);
	for (carry = 0, i = 0; i < un - 1; ++i) {
		uint64_t bits = ((uint64_t) u->limbs[i]) << shift;
		uw[i] = bits | carry;
		carry = bits >> 32;
	}
	uw[i] = carry;

	/*
	 * Allocate the quotient.  We don't allocate the remainder as a nat
	 * unless the caller requested it; it gets incrementally generated
	 * in one of the (C runtime heap-allocated) structures above.  Note
	 * that after this allocation u and v are clobbered; we don't need them
	 * anymore since we're now working with the normalized versions.
	 */
	u = v = NULL;		/* may be clobbered, no cheating! */
	struct intrep *qr = halloc(sizeof *qr + sizeof qr->limbs[0] * (un-vn));
	qr->sign = qsign;
	qr->nlimbs = un - vn;

	/*
	 * This is the main loop of the division.
 	 */
	const uint64_t modulus = 1ull << 32;
	for (i = un - vn; i--; /* nada */) {
		/*
		 * Estimate the next limbs of the quotient and remainder using
		 * trial division of the most-significant limbs of the dividend
		 * and divisor.  A detailed discussion of this estimation
		 * process can be found in Knuth's presentation of Algorithm D.
		 */
		uint64_t term = ((uint64_t) uw[vn + i]) << 32 |
				((uint64_t) uw[vn + i - 1]),
			 qhat = term / (uint64_t) vw[vn - 1],
			 rhat = term - qhat * (uint64_t) vw[vn - 1];
	refine:
		if (qhat >= modulus || 
		    qhat * vw[vn - 2] > rhat * modulus + uw[i + vn - 2]) {
			--qhat;
			rhat += vw[vn - 1];
			if (rhat < modulus) goto refine;
		}

		/*
		 * Multiply divisor by estimated quotient digit and
		 * subtract from dividend.
		 */
		assert(qhat < modulus);
		/*
		 * Really need to audit & test the signed vs. unsigned here.
		 */
		int64_t try;
		for (carry = 0, j = 0; j < vn; ++j) {
			uint64_t prod = qhat * ((uint64_t) vw[j]);
			try = ((int64_t) uw[i+j]) -
			      ((int64_t) carry) -
			      ((int64_t) (prod & UINT32_MAX));
			uw[i+j] = try;
			carry = (prod >> 32) - (try >> 32);
		}
		try = ((int64_t) uw[i+j]) - ((int64_t) carry);
		uw[i+j] = try;

		/*
		 * Record the next digit of the quotient; in very rare cases
		 * (probability about 1/2^31 per random word) our quotient
		 * estimate overshot by 1, in which case we need to walk that
		 * back out.
		 */
		qr->limbs[i] = qhat;
		if (try < 0) {
			qr->limbs[i] -= 1;
			for (carry = 0, j = 0; j < vn; ++j) {
				try = ((uint64_t) uw[i+j]) +
				      ((uint64_t) vw[j]) +
				      ((uint64_t) carry);
				uw[i+j] = try;
				carry = try >> 32;
			}
			uw[i+j] += carry;
		}
	}

	*q = int_normalize(qr);
	if (r) {
		heap_root_push(q);
		struct intrep *rr = halloc(sizeof *rr +
					   sizeof rr->limbs[0] * vn);
		heap_root_pop(q);
		rr->nlimbs = vn;
		rr->sign = vn ? rsign : +1;	/* avoid -0 */
		const uint32_t unshift = 32 - shift;
		for (carry = 0, i = vn; i--; /* nada */) {
			uint64_t bits = ((uint64_t) uw[i]) << unshift;
			rr->limbs[i] = (bits >> 32) | carry;
			carry = bits;
		}
		*r = int_normalize(rr);
	}
	free(vw), free(uw);
}

int_mt
int_divt(int_mt u, int_mt v)
{
	int_mt q;
	int_divt_remt(u, v, &q, NULL);
	return q;
}

int_mt
int_remt(int_mt u, int_mt v)
{
	int_mt q, r;
	int_divt_remt(u, v, &q, &r);
	return r;
}

int
int_cmp(int_mt z, int_mt w)
{
	if (z == w)
		return 0;
	return	z->sign < 0 ?
		(w->sign > 0 ? -1 : -int_cmp_mag(z, w)) :
		(w->sign < 0 ? +1 : +int_cmp_mag(z, w));
}
