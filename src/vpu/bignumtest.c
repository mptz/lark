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
 * Generate random bignum assembly language code for installation in the
 * unit tests.  Uses GMP to independently calculate the answers.
 */

#include <getopt.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <gmp.h>	/* must come after stdio.h; checks for FILE defined */

/*
 * Super lame use of stderr for the reference output... but this is a
 * one-off tool to generate static test cases.
 */
static void print_op2_case(const char *insn, mpz_t x, mpz_t y, mpz_t z)
{
	gmp_printf(
"	LDLz	R0, %+Zd\n"
"	LDLz	R1, %+Zd\n"
"	%s	R0, R1\n"
"	PRINTz	R0\n"
"	PRINTc	RF\n",
			x, y, insn);
	gmp_fprintf(stderr, "%+Zd\n", z);
}

/*
 * For comparisons, we have to mutate to guarantee a fresh value,
 * otherwise literal pooling will assign the same pointer to both
 * literals, short-circuiting like comparisons.
 */
static void print_cmp_case(const char *insn, int cmp, mpz_t x, mpz_t y)
{
	gmp_printf(
"	LDLz	R0, %+Zd\n"
"	MOV	R1, R0\n"
"	INCz	R1\n"
"	DECz	R1\n"
"	LDLz	R2, %+Zd\n"
"	%s	R0, R1\n"
"	LDRR	RC\n"
"	PRINTo	RC\n"
"	PRINTc	RF\n"
"	%s	R0, R2\n"
"	LDRR	RC\n"
"	PRINTo	RC\n"
"	PRINTc	RF\n",
			x, y, insn, insn);
	fprintf(stderr, "#+0\n" "#%+d\n", cmp);
}

static unsigned random_magnitude(void)
{
	double x = drand48();
	if (x < 0.01) return floor(drand48() * 10.0);
	if (x < 0.1)  return floor(drand48() * 100.0);
	double y = floor(-log(drand48()) * (x < 0.5 ? 1000.0 : 10000.0));
	return y > 100000.0 ? 100000.0 : y;
}

static void usage(void)
{
	fprintf(stderr, "Usage: bignumtest -[asmdr]\n");
	exit(EXIT_FAILURE);
}

enum operation { OpInvalid, OpAdd, OpSub, OpMul, OpDivT, OpRemT, OpCmp };

int main(int argc, char *argv[])
{
	enum operation op = OpInvalid;
	mpz_t x, y, z;

	gmp_randstate_t state;
	gmp_randinit_mt(state);
	mpz_t seed;
	mpz_init_set_str(seed, "BIGNUMTEST", 36);
	gmp_randseed(state, seed);

	mpz_init(x);
	mpz_init(y);
	mpz_init(z);

	int c;
	while ((c = getopt(argc, argv, "asmdrc")) != -1) {
		switch (c) {
		case 'a':	op = OpAdd; break;
		case 's':	op = OpSub; break;
		case 'm':	op = OpMul; break;
		case 'd':	op = OpDivT; break;
		case 'r':	op = OpRemT; break;
		case 'c':	op = OpCmp; break;
		}
	}
	if (optind != argc)
		usage();

	unsigned n = 100;
	while (n--) {
		/*
		 * The urandom calls are uniform... the rrandom calls
		 * generate numbers with long strings of 1's and 0's,
		 * which are likely to trigger bignum arithmetic bugs.
		 */
	tryagain:
		if (drand48() < 0.5) {
			mpz_urandomb(x, state, random_magnitude());
			mpz_urandomb(y, state, random_magnitude());
		} else {
			mpz_rrandomb(x, state, random_magnitude());
			mpz_rrandomb(y, state, random_magnitude());
		}

		/*
		 * Integer tests... choose random signs.
		 */
		if (drand48() < 0.5)
			mpz_neg(x, x);
		if (drand48() < 0.5)
			mpz_neg(y, y);

		switch (op) {
		case OpAdd:
			mpz_add(z, x, y);
			print_op2_case("ADDz", x, y, z);
			break;
		case OpSub:
			mpz_sub(z, x, y);
			print_op2_case("SUBz", x, y, z);
			break;
		case OpMul:
			mpz_mul(z, x, y);
			print_op2_case("MULz", x, y, z);
			break;
		case OpDivT:
			if (mpz_sgn(y) == 0) goto tryagain;
			mpz_tdiv_q(z, x, y);
			print_op2_case("DIVTz", x, y, z);
			break;
		case OpRemT:
			if (mpz_sgn(y) == 0) goto tryagain;
			mpz_tdiv_r(z, x, y);
			print_op2_case("REMTz", x, y, z);
			break;
		case OpCmp: {
			int cmp = mpz_cmp(x, y);
			if (cmp < 0) cmp = -1;
			if (cmp > 0) cmp = +1;
			print_cmp_case("CMPz", cmp, x, y);
			break;
		}
		default: abort();
		}
	}

	mpz_clear(x);
	mpz_clear(y);
	mpz_clear(z);

	gmp_randclear(state);
	mpz_clear(seed);

	return 0;
}
