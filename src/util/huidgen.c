/*
 * Copyright (c) 2009-2024 Michael P. Touloumtzis.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "base64.h"
#include "huid.h"
#include "huidrand.h"
#include "message.h"
#include "sha2.h"

/*
 * CLOCK_BOOTTIME is not POSIX; prepare a fallback.
 */
#ifdef CLOCK_BOOTTIME
#define CLOCK_MONOTONIC_ALTERNATE CLOCK_BOOTTIME
#else
#define CLOCK_MONOTONIC_ALTERNATE CLOCK_MONOTONIC
#endif

typedef uint8_t bits256 [32];
struct uint128 { uint64_t lo, hi; };

/*
 * HUID integrity check: 144 bits are random, followed by the N leading
 * bits of the SHA-256 hash of the random bits.
 */
#define RANDOM_BYTES 18		/* 144 bits */
#define CHECK_BYTES 6		/* 48 bits */

static void make_redundant(bits256 bits)
{
	bits256 hash;
	sha256_hash(hash, bits, RANDOM_BYTES);
	memcpy(bits + RANDOM_BYTES, hash, CHECK_BYTES);
}

static int check_redundant(bits256 bits)
{
	bits256 hash;
	sha256_hash(hash, bits, RANDOM_BYTES);
	return !memcmp(bits + RANDOM_BYTES, hash, CHECK_BYTES);
}

static void usage(void) __attribute__ ((noreturn));
static void usage(void)
{
	fprintf(stderr,
"Usage: %s -N <nonce> -A <attestor> [-n <# of HUIDs to generate> | -s] [-r]\n"
"    *** PLEASE READ ***\n"
"This tool generates 'HUIDs', Hopefully-Unique IDs.  If you use it carefully,\n"
"you can be confident that no one else has these HUIDs.  However, random ID\n"
"generation has numerous pitfalls, so read the following with care:\n"
"    -N <nonce>\n"
"       A string which should, to the best of your knowledge, never have been\n"
"       provided to a previous invocation of %s by anyone including you.\n"
"       e.g. -N 'bona fide randomness: iWnPs$gXz3sd+w@a?Ym9sRDP!fFH2'\n"
"            -N 'Our hero Bionic Banana nabbed the Pernicious Pea!!'\n"
"            -N 'My password is neither tL982@hH3aq nor Y7E2#29d-9b'\n"
"            -N 'troubadour kindle cromulent ambassador emporium'\n"
"       Be creative and don't use those examples--they're taken!  The nonce\n"
"       you use cannot be recovered from the HUID; it's present in case other\n"
"       sources of randomness are not reliably random.  Don't script %s; if\n"
"       you do, use a distinct, high-entropy nonce per call.\n"
"    -A <attestor>\n"
"       The person or group attesting that this nonce has never been used\n"
"       before.  Also not recoverable from the HUID.  Honor system.\n"
"       e.g. -A 'Jane Q. Example <jane.q.example@example.org>'\n"
"            -A 'Evacuation Preparedness Team, Yoyodyne Propulsion Systems'\n"
"            -A 'Fingerprint: BC16 9E98 1A08 9BDE 52D8  "
			     "183E AF34 CCD7 448F 268F'\n"
"    -r\n"
"       Generate a HUID with internal redundancy; such a HUID is 192 bits\n"
"       long, with 144 random bits and 48 bits of checkable redundancy.\n"
"    -s\n"
"       Streaming operation: generate an endless list of HUIDs.  Use with\n"
"       caution as with the exception of fine-grained time information, new\n"
"       sources of entropy aren't incorporated after initialization.\n"
	,
	execname, execname, execname
	);
	exit(EXIT_FAILURE);
}

static void genkeys(unsigned nk, int streaming, int redundant)
{
	bits256 huidbuf;
	char b64buf [100];

	while (streaming || nk--) {
		/*
		 * Generate a fresh base HUID (without redundancy).
		 */
		huid_fresh(huidbuf, sizeof huidbuf);

		/*
		 * Embed redundancy into the output via a 96-bit check
		 * over the leading 144 bits.
		 */
		if (redundant) {
			make_redundant(huidbuf);
			if (!check_redundant(huidbuf))
				panic("Failed immediate redundancy check\n");
		}

		/*
		 * Base64-encode the output; multiples-of-three byte counts
		 * yield integral numbers of base64 digits, so no padding.
		 */
		size_t n = base64_encode(b64buf, sizeof b64buf, huidbuf,
					 RANDOM_BYTES +
					     (redundant ? CHECK_BYTES : 0));
		b64buf[n] = '\0';
		size_t len = strlen(b64buf);
		printf("%.8s", b64buf);
		for (size_t i = 8; i < len; i += 8)
			printf(".%.8s", b64buf + i);
		putchar('\n');
	}
}

int main(int argc, char *argv[])
{
	const char *attestor = NULL, *nonce = NULL;
	unsigned long nk = 1;
	int c, redundant = 0, streaming = 0;

	set_execname(argv[0]);
	while ((c = getopt(argc, argv, "A:n:N:rs")) != -1) {
		switch (c) {
		case 'A': attestor = optarg; break;
		case 'n': {
			char *end;
			nk = strtoul(optarg, &end, 10);
			if (*end != '\0') {
				errf("Not a valid key count: %s\n", optarg);
				usage();
			}
			break;
		}
		case 'N': nonce = optarg; break;
		case 'r': redundant = 1; break;
		case 's': streaming = 1; break;
		default: usage();
		}
	}
	if (!nonce) {
		err("Missing argument: -N <nonce>\n");
		usage();
	}
	if (!attestor) {
		err("Missing argument: -A <attestor>\n");
		usage();
	}

	huid_init(nonce, attestor);
	genkeys(nk, streaming, redundant);
	return 0;
}
