/*
 * sha2test.c: Implement test vectors for SHA-2.
 *
 * Copyright (c) 2007 Michael P. Touloumtzis.
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
 * The test vectors are "official" and come from NIST/NSA; they are
 * installed in 'testdata'.  The first two vectors are just messages
 * followed by hashes; this file reads the message, then the hash,
 * and verifies correctness.  The third vector uses a feedback
 * algorithm to generate 1000 consecutive hashes from a single seed
 * message.
 *
 * Test information:
 *
 * The following tests should succeed:
 *	bzcat testdata/sha256-vector-{short,long}.bz2 | sha2test
 *	sha2test < testdata/sha256-vector-monte
 *
 * The following tests should fail:
 *	sha2test < testdata/sha256-failure-{1,2,3}
 *
 * Command line options:
 *
 * -v	Vary streaming: use variable, continually changing input sizes
 *	(down to 1 byte) in streaming hash calls.  When unset, all calls
 *	to streaming functions except the last in a message will use the
 *	same input size.
 */

#include <assert.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <util/message.h>
#include <util/sha2.h>
#include <util/twister.h>

static int option_vary_streaming;

static void compare_digests(const char *digest, const char *control);
static size_t next_threshold(size_t bufsize);
static void read_next_hash(char *buffer);

static inline unsigned char
hex2bin(unsigned char h)
{
	if (h >= '0' && h <= '9')
		return h - '0';
	if (h >= 'a' && h <= 'f')
		return 10 + h - 'a';
	if (h >= 'A' && h <= 'F')
		return 10 + h - 'A';
	panic("Invalid hex digit");
}

static inline unsigned char
hexes2bin(unsigned char high, unsigned char low)
{
	return (hex2bin(high) << 4) | hex2bin(low);
}

static inline int
interesting(int c)
{
	if ((c >= '0' && c <= '9') ||
	    (c >= 'a' && c <= 'f') ||
	    (c >= 'A' && c <= 'F') ||
	    (c == ':') || (c == '#'))
		return 1;
	return 0;
}


static void
compare_digests(const char *digest, const char *control)
{
	static int counter;

	assert(strlen(digest) == SHA256_HEX_BYTES);
	assert(strlen(control) == SHA256_HEX_BYTES);
	++counter;
	if (strcasecmp(digest, control)) {
		fprintf(stderr, "SHA-256 comparison against sample vector "
				"FAILED for sample #%d!\n"
				"message digest: %s\ncontrol digest: %s\n",
				counter, digest, control);
		exit(EXIT_FAILURE);
	}
}

static size_t
next_threshold(size_t bufsize)
{
	size_t threshold = bufsize;

	if (option_vary_streaming) {
		/* bufsize must be a power of two */
		assert(bufsize);
		assert(!(bufsize & (bufsize - 1)));
		threshold = (genrand() & (bufsize - 1)) + 1;
	}
	return threshold;
}

static void
read_next_hash(char *buffer)
{
	char *ptr = buffer;
	int c;

	while ((c = getchar()) != EOF) {
		if (!interesting(c))
			continue;
		assert(c != ':' && c != '#');
		*ptr++ = c;
		if (ptr - buffer == SHA256_HEX_BYTES) {
			*ptr = '\0';
			return;
		}
	}
	fputs("SHA-256 test FAILED: unexpected end of input!\n", stderr);
	exit(EXIT_FAILURE);
}

static void
sha256_monte(const char *seed, size_t seedsize)
{
	char feedback [4 * SHA256_BIN_BYTES];
	char digest [SHA256_STR_BYTES], control [SHA256_STR_BYTES];
	uint32_t i, j;

	assert(seedsize == SHA256_BIN_BYTES);
	memcpy(feedback + 3 * SHA256_BIN_BYTES, seed, seedsize);
	digest[SHA256_HEX_BYTES] = '\0';
	control[SHA256_HEX_BYTES] = '\0';

	for (j=0; j<100; j++) {
		memcpy(feedback + 2 * SHA256_BIN_BYTES,
			feedback + 3 * SHA256_BIN_BYTES,
			SHA256_BIN_BYTES);
		memcpy(feedback,
			feedback + 2 * SHA256_BIN_BYTES,
			2 * SHA256_BIN_BYTES);

		/* compute the next hash in the series */
		for (i=0; i<1000; i++) {
			sha256_hash(feedback + 3 * SHA256_BIN_BYTES,
				feedback, 3 * SHA256_BIN_BYTES);
			memmove(feedback,
				feedback + SHA256_BIN_BYTES,
				3 * SHA256_BIN_BYTES);
		}

		/* compare hash to version read from input */
		read_next_hash(control);
		sha256_to_ascii(digest, feedback + 2 * SHA256_BIN_BYTES);
		compare_digests(digest, control);
	}
}

int
main(int argc, char *argv[])
{
	char buf [256], *ptr = buf, digest [SHA256_STR_BYTES];
	struct sha256_state sha256_state;
	size_t threshold;
	int c;
	unsigned char high = 0;

	while ((c = getopt(argc, argv, "v")) != -1) {
		if (c == 'v')
			option_vary_streaming = 1;
	}

	digest[sizeof digest - 1] = '\0';
	sha256_stream_start(&sha256_state);
	threshold = next_threshold(sizeof buf);

	while ((c = getchar()) != EOF) {
		if (!interesting(c))
			continue;

		if (c == 's') {
			/* seed for monte verification */
			sha256_monte(buf, ptr - buf);
			ptr = buf;

		} else if (c == ':') {
			/* message complete; finish off digest */
			assert(ptr - buf <= threshold);
			sha256_stream_hash(&sha256_state, buf, ptr - buf);
			ptr = buf;
			sha256_stream_finish(&sha256_state, digest);
			sha256_to_ascii(digest, digest);

			/* get hash and compare */
			read_next_hash(buf);
			compare_digests(digest, buf);

		} else if (high) {
			/* now have complete byte; add to buffer */
			*ptr++ = hexes2bin(high, c);
			high = 0;
			assert(ptr - buf <= threshold);
			if (ptr - buf == threshold) {
				/* stream buffer through hash */
				sha256_stream_hash(&sha256_state, buf, ptr - buf);
				ptr = buf;
				threshold = next_threshold(sizeof buf);
			}

		} else {
			/* halfway through a byte; save for later */
			high = c;
		}
	}
	return 0;
}
