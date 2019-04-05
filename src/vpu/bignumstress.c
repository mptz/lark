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

/*
 * A stress test which uses GMP to generate random bignums, then tests our
 * arithmetic operations on them against GMP controls.  Not integrated into the
 * unit test harness; runs until interrupted.
 */

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <gmp.h>	/* must come after stdio.h; checks for FILE defined */

#include <util/memutil.h>
#include <util/message.h>

#include "bignum.h"
#include "heap.h"

static void gmp2nat(mpz_t x, nat_mt *n)
{
	const char *t = mpz_get_str(NULL, 10, x);
	*n = str2nat(t);
	xfree(t);
}

static void gmp2int(mpz_t x, int_mt *i)
{
	const char *t = mpz_get_str(NULL, 10, x);
	*i = str2int(t);
	xfree(t);
}

/*
 * We compare string representations as well as using nat_cmp to take
 * a hard dependency on nat_cmp correctness out of the equation.
 * We also always ensure that the string comparison and nat_cmp agree,
 * as a way to ensure we cover both comparison false positives & negatives.
 */
static void eqtestn(const char *op, mpz_t x, mpz_t y, mpz_t z, nat_mt *n)
{
	char *t0 = mpz_get_str(NULL, 10, z);
	const char *t1 = nat2str(*n);

	nat_mt m;	/* not registered as root... allocs could change */
	gmp2nat(z, &m);

	bool streq = !strcmp(t0, t1),
	     cmpeq = !nat_cmp(m, *n);
	if (streq != cmpeq) {
		gmp_fprintf(stderr, "COMPARISON MISMATCH %s: "
				    "x = %Zd, y = %Zd, z = %s, "
				    "n = %s\n", op, x, y, t0, t1);
		abort();
	}
	if (!streq || !cmpeq) {
		gmp_fprintf(stderr, "OPERATION MISMATCH %s: "
				    "x = %Zd, y = %Zd, z = %s, "
				    "n = %s\n", op, x, y, t0, t1);
		abort();
	}

	free(t0);
	xfree(t1);
}

static void eqtestz(const char *op, mpz_t x, mpz_t y, mpz_t z, int_mt *i)
{
	char *t0 = mpz_get_str(NULL, 10, z);
	const char *t1 = int2str(*i);
	int pos = (t1[0] == '+');

	int_mt j;	/* not registered as root... allocs could change */
	gmp2int(z, &j);

	bool streq = !strcmp(t0, t1 + pos),  /* skip leading '+' if present */
	     cmpeq = !int_cmp(j, *i);
	if (streq != cmpeq) {
		gmp_fprintf(stderr, "COMPARISON MISMATCH %s: "
				    "x = %Zd, y = %Zd, z = %s, "
				    "i = %s\n", op, x, y, t0, t1);
		abort();
	}
	if (!streq || !cmpeq) {
		gmp_fprintf(stderr, "OPERATION MISMATCH %s: "
				    "x = %Zd, y = %Zd, z = %s, "
				    "i = %s\n", op, x, y, t0, t1);
		abort();
	}

	free(t0);
	xfree(t1);
}

static unsigned random_magnitude(void)
{
	double x = drand48();
	if (x < 0.01) return floor(drand48() * 10.0);
	if (x < 0.1)  return floor(drand48() * 100.0);
	double y = floor(-log(drand48()) * (x < 0.5 ? 1000.0 : 10000.0));
	return y > 100000.0 ? 100000.0 : y;
}

int main(int argc, char *argv[])
{
	mpz_t x, y, z;
	nat_mt m, n, s;
	int_mt i, j, k;

	set_execname(argv[0]);
	global_message_threshold = 100;	/* traces, etc */

	/*
	 * Garbage collection can happen any time we do bignum arithmetic,
	 * so the address of each local which can point to a bignum must
	 * be registered with GC... a copying collection will rewrite these
	 * pointers.
	 */
	heap_init();
	m = n = s = str2nat("0");	/* must point at heap datum before
					   registering as a heap root */
	i = j = k = str2int("+0");	/* ditto */
	heap_root_push(&m);
	heap_root_push(&n);
	heap_root_push(&s);
	heap_root_push(&i);
	heap_root_push(&j);
	heap_root_push(&k);

	/*
	 * We use GMP to generate large random numbers; we use the C
	 * stdlib to provide ordinary random numbers to drive the tests.
	 */
	gmp_randstate_t state;
	gmp_randinit_mt(state);
	mpz_t seed;
	mpz_init_set_str(seed, "THISISANOTHERRANDOMSEED", 36);
	gmp_randseed(state, seed);

	mpz_init(x);
	mpz_init(y);
	mpz_init(z);

	while (1) {
		/*
		 * The urandom calls are uniform... the rrandom calls
		 * generate numbers with long strings of 1's and 0's,
		 * which are likely to trigger bignum arithmetic bugs.
		 */
		if (drand48() < 0.5) {
			mpz_urandomb(x, state, random_magnitude());
			mpz_urandomb(y, state, random_magnitude());
		} else {
			mpz_rrandomb(x, state, random_magnitude());
			mpz_rrandomb(y, state, random_magnitude());
		}
		gmp2nat(x, &m);
		gmp2nat(y, &n);

		/* POSn/NEGn */
		k = nat_pos(m);
		/* nothing to do to x */
		eqtestz("nat_pos", x, x, x, &k);
		mpz_neg(z, y);
		k = nat_neg(n);
		eqtestz("nat_neg", y, y, z, &k);

		/* INCn/DECn */
		mpz_add_ui(z, x, 1);
		s = nat_inc(m);
		eqtestn("nat_inc", x, x, z, &s);
		if (mpz_sgn(x) > 0) {
			mpz_sub_ui(z, x, 1);
			s = nat_dec(m);
			eqtestn("nat_dec", x, x, z, &s);
		}

		/* ADDn */
		mpz_add(z, x, y);
		s = nat_add(m, n);
		eqtestn("nat_add", x, y, z, &s);

		/* MULn */
		mpz_mul(z, x, y);
		s = nat_mul(m, n);
		eqtestn("nat_mul", x, y, z, &s);

		/* SUBn (smaller from larger only) */
		if (mpz_cmp(x, y) < 0) {
			mpz_sub(z, y, x);
			s = nat_sub(n, m);
			eqtestn("nat_sub", y, x, z, &s);
		} else {
			mpz_sub(z, x, y);
			s = nat_sub(m, n);
			eqtestn("nat_sub", x, y, z, &s);
		}

		/* DIVTn/REMTn (nonzero divisor only) */
		if (mpz_sgn(y) != 0) {
			mpz_div(z, x, y);
			s = nat_divt(m, n);
			eqtestn("nat_divt", x, y, z, &s);

			mpz_mod(z, x, y);
			s = nat_remt(m, n);
			eqtestn("nat_remt", x, y, z, &s);
		}

		/*
		 * Now integer tests... choose random signs.
		 */
		if (drand48() < 0.5)
			mpz_neg(x, x);
		if (drand48() < 0.5)
			mpz_neg(y, y);
		gmp2int(x, &i);
		gmp2int(y, &j);

		/* ABS/MAG */
		mpz_abs(z, x);
		k = int_abs(i);
		eqtestz("int_abs", x, x, z, &k);
		mpz_abs(z, y);
		s = int_mag(j);
		eqtestn("int_mag", y, y, z, &s);

		/* NEGz */
		mpz_neg(z, x);
		k = int_neg(i);
		eqtestz("int_neg", x, x, z, &k);
		mpz_neg(z, y);
		k = int_neg(j);
		eqtestz("int_neg", y, y, z, &k);

		/* INCz/DECz */
		mpz_add_ui(z, x, 1);
		k = int_inc(i);
		eqtestz("int_inc", x, x, z, &k);
		mpz_sub_ui(z, y, 1);
		k = int_dec(j);
		eqtestz("int_dec", y, y, z, &k);

		/* ADDz/SUBz */
		mpz_add(z, x, y);
		k = int_add(i, j);
		eqtestz("int_add", x, y, z, &k);
		mpz_sub(z, x, y);
		k = int_sub(i, j);
		eqtestz("int_sub", x, y, z, &k);

		/* MULz */
		mpz_mul(z, x, y);
		k = int_mul(i, j);
		eqtestz("int_mul", x, y, z, &k);

		/* DIVTn/REMTn (nonzero divisor only) */
		if (mpz_sgn(y) != 0) {
			mpz_tdiv_q(z, x, y);
			k = int_divt(i, j);
			eqtestz("int_divt", x, y, z, &k);

			mpz_tdiv_r(z, x, y);
			k = int_remt(i, j);
			eqtestz("int_remt", x, y, z, &k);
		}
	}

	/*
	 * Can't currently get here since the test doesn't terminate,
	 * but leaving around in case things get reconfigured.
	 */
	mpz_clear(x);
	mpz_clear(y);
	mpz_clear(z);

	gmp_randclear(state);
	mpz_clear(seed);

	heap_root_pop(&k);
	heap_root_pop(&j);
	heap_root_pop(&i);
	heap_root_pop(&s);
	heap_root_pop(&n);
	heap_root_pop(&m);
	return 0;
}
