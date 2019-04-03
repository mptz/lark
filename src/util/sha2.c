/*
 * sha2.c: An implementation of the SHA-2 secure hash function as
 * specified in FIPS 180-2 (available from the NIST site, nist.gov).
 *
 * Currently only the 256-bit variant (SHA-256) is implemented.
 *
 * Although SHA-2 is specified for arbitrary bitstreams, this
 * implementation (like many) allows only streams of bytes (this
 * would not be too hard to change).
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

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <util/assert.h>
#include <util/fdutil.h>
#include <util/message.h>
#include <util/sha2.h>
#include <util/util.h>

/* hash of the null data stream */
const char SHA256_NULL_HASH[] =
	"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

/*
 * In my tests, increasing this value did not make SHA faster at all.
 * Don't increase it unless you have tested and seen a measurable gain.
 */
#define IO_BUFFER_SIZE (16*1024)

/* byte swapping for endian conversions (SHA is big-endian) */
#include <endian.h>
#if __BYTE_ORDER == __BIG_ENDIAN
#define swap32(n) (n)
#elif __BYTE_ORDER == __LITTLE_ENDIAN
static inline uint32_t
swap32(uint32_t n)
{
	uint32_t v = (n << 16) | (n >> 16);
	return ((v & 0xff00ff00) >> 8) | ((v & 0x00ff00ff) << 8);
}
#else
#error "can't determine byte order on this platform"
#endif

#define SHA_BLOCK_BYTES 64

/*
 * The core SHA-256 transform.  Handles blocks of exactly 512 bits
 * (64 bytes, or 16 32-bit words), which must be uint32_t aligned
 * as indicated here by the use of a uint32_t* to pass the data.
 */
static void sha256_transform(struct sha256_state *state, const uint32_t *data);

static const uint32_t *
ptr32(const void *ptr)
{
	assert((3 & (uintptr_t) ptr) == 0);
	return (const uint32_t*) ptr;
}

void
sha256_hash(void *hash, const void *src, size_t srcsize)
{
	struct sha256_state state;

	sha256_stream_start(&state);
	sha256_stream_hash(&state, src, srcsize);
	sha256_stream_finish(&state, hash);
}

void
sha256_ascii_hash(char *hash, const void *src, size_t srcsize)
{
	sha256_hash(hash, src, srcsize);
	sha256_to_ascii(hash, hash);
}

int
sha256_compare(const void *expected, const void *actual)
{
	char estring [SHA256_STR_BYTES], astring [SHA256_STR_BYTES];
	sha256_to_string(estring, expected);
	sha256_to_string(astring, actual);
	return sha256_compare_string(estring, astring);
}

int
sha256_compare_string(const char *expected, const char *actual)
{
	if (strcmp(expected, actual)) {
		err("SHA-256 digest mismatch; file changed or corrupted\n");
		errf("Want: %s\n", expected);
		errf("Have: %s\n", actual);
		return -1;
	}
	return 0;
}

int
sha256_hash_file(void *hash, const char *pathname)
{
	int fd, retval;

	if ((fd = open(pathname, O_RDONLY)) == -1) {
		perrf("open(\"%s\", O_RDONLY)", pathname);
		return -1;
	}
	retval = sha256_hash_fd(hash, fd);
	p_close(fd);
	return retval;
}

int sha256_hash_fd(void *hash, int fd)
{
	struct sha256_state state;
	ssize_t count;

	sha256_stream_start(&state);
	do {
		unsigned char buf [IO_BUFFER_SIZE];

		do {
			count = read(fd, buf, sizeof buf);
		} while ((count == -1) && (errno == EINTR));
		if (count == -1) {
			perr("read");
			return -1;
		} else if (count)
			sha256_stream_hash(&state, buf, count);
	} while (count);
	sha256_stream_finish(&state, hash);
	return 0;
}

void
sha256_to_ascii(char *aschash, const void *binhash)
{
	int i;
	for (i = SHA256_BIN_BYTES - 1; i >= 0; --i) {
		unsigned char c = ((unsigned char*) binhash)[i];
		aschash[2*i] = hexit(c >> 4);
		aschash[2*i+1] = hexit(c & 0xf);
	}
}

void
sha256_stream_start(struct sha256_state *state)
{
	arg_assert(state);
	state->nbuffered = 0;
	state->nhashed = 0;

	/*
	 * The magic numbers H0..H7 are the first 32 bits of the fractional
	 * parts of the square roots of the first 8 primes 2..19.
	 */
	state->H0 = 0x6a09e667;
	state->H1 = 0xbb67ae85;
	state->H2 = 0x3c6ef372;
	state->H3 = 0xa54ff53a;
	state->H4 = 0x510e527f;
	state->H5 = 0x9b05688c;
	state->H6 = 0x1f83d9ab;
	state->H7 = 0x5be0cd19;
}

void
sha256_stream_hash(struct sha256_state *state, const void *src, size_t srcsize)
{
	arg_assert(state);
	arg_assert(src);
	const unsigned char *srcb = src;

	/* handle internally buffered data first */
	if (state->nbuffered) {
		size_t count;
		assert(state->nbuffered < SHA_BLOCK_BYTES);
		if (state->nbuffered + srcsize < SHA_BLOCK_BYTES) {
			memcpy(state->buf + state->nbuffered, srcb, srcsize);
			state->nbuffered += srcsize;
			return;
		}
		count = SHA_BLOCK_BYTES - state->nbuffered;
		memcpy(state->buf + state->nbuffered, srcb, count);
		srcb += count;
		srcsize -= count;
		sha256_transform(state, ptr32(state->buf));
		state->nbuffered = 0;
	}
	assert(!state->nbuffered);

	/* handle full SHA blocks, as many as we have */
	while (srcsize >= SHA_BLOCK_BYTES) {
		const uint32_t *ptr = (const uint32_t*) srcb;
		if (3 & (uintptr_t) ptr) {
			/* unaligned fixup */
			memcpy(state->buf, srcb, SHA_BLOCK_BYTES);
			ptr = ptr32(state->buf);
		}
		sha256_transform(state, ptr);
		srcb += SHA_BLOCK_BYTES;
		srcsize -= SHA_BLOCK_BYTES;
	}

	/* store remainder in buffer */
	if (srcsize) {
		memcpy(state->buf, srcb, srcsize);
		state->nbuffered = srcsize;
	}
}

void
sha256_stream_finish(struct sha256_state *state, void *hash)
{
	uint32_t bytes;

	arg_assert(state);
	bytes = state->nbuffered;
	assert(bytes < SHA_BLOCK_BYTES);

	/* fold buffered bytes into byte count, then pad as per spec */
	state->nhashed += bytes;
	state->buf[bytes++] = 0x80;
	if (bytes > 56) {
		memset(state->buf + bytes, 0, SHA_BLOCK_BYTES - bytes);
		sha256_transform(state, ptr32(state->buf));
		state->nhashed -= SHA_BLOCK_BYTES;
		bytes = 0;
	}
	if (bytes != 56)
		memset(state->buf + bytes, 0, 56 - bytes);

	/* finish padding w/count of bits hashed */
	{
		uint32_t *ptr32 = (uint32_t*) (state->buf + 56);
		state->nhashed <<= 3;
		*ptr32++ = swap32(state->nhashed >> 32);
		*ptr32 = swap32(state->nhashed & 0xffffffff);
	}
	sha256_transform(state, ptr32(state->buf));

	/* store result in 'hash' in little-endian format */
	arg_assert(hash);
	if (3 & (uintptr_t) hash) {
		uint32_t hash32 [8];
		hash32[0] = swap32(state->H0);
		hash32[1] = swap32(state->H1);
		hash32[2] = swap32(state->H2);
		hash32[3] = swap32(state->H3);
		hash32[4] = swap32(state->H4);
		hash32[5] = swap32(state->H5);
		hash32[6] = swap32(state->H6);
		hash32[7] = swap32(state->H7);
		memcpy(hash, hash32, sizeof hash32);
	} else {
		uint32_t *hash32 = hash;
		*hash32++ = swap32(state->H0);
		*hash32++ = swap32(state->H1);
		*hash32++ = swap32(state->H2);
		*hash32++ = swap32(state->H3);
		*hash32++ = swap32(state->H4);
		*hash32++ = swap32(state->H5);
		*hash32++ = swap32(state->H6);
		*hash32 = swap32(state->H7);
	}

	/* wipe state for subsequent hashing */
	sha256_stream_start(state);
}

/*
 * These constants are the first 32 bits of the fractional parts of the
 * cube roots of the first 64 primes 2..311.
 */
static const uint32_t K [64] =
	{ 0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
	  0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
	  0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7,
	  0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
	  0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
	  0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
	  0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
	  0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
	  0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
	  0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
	  0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	  0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

/*
 * Various size/speed tradeoffs are available.  Choose 0..2.
 *
 * Code size 0 is a straightforward, small implementation, mainly for
 * verification as it's very unlikely to be the fastest.  It is open
 * coded without macros beyond the rotate-right operation.
 *
 * Code size 1 unrolls the 64 rounds into 8 loops, using macros for each
 * round.  This isn't just a loop unrolling... it also eliminates the
 * assignments used to rotate the state variables A through H.
 *
 * Code size 2 is the same technique as size 1, with all 64 loops fully
 * unrolled.
 *
 * Size 2 seems to be the fastest, although it presumably also hits the
 * icache hardest, so size 1 would also be a reasonable choice.
 */
#define SHA_CODE_SIZE 2

/*
 * Right rotate and shift operations.
 */
#define ROR(X, n) (((X) >> n) | ((X) << (32 - n)))
#define LSR(X, n) ((X) >> n)

/*
 * These are the guts of SHA-2.
 */
#define F1(a, b, c) \
	((ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) + (c & (a ^ b))))
#define F2(e, f, g, h) \
	(h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + (g ^ (e & (f ^ g))))
#define ROUND(a, b, c, d, e, f, g, h, i) do { \
		uint32_t ROUND_tmp = F2(e, f, g, h) + K[i] + W[i]; \
		d += ROUND_tmp; h = ROUND_tmp + F1(a, b, c); \
	} while (0)

static void
sha256_transform(struct sha256_state *state, const uint32_t *data)
{
	uint32_t W [64];
	uint32_t A, B, C, D, E, F, G, H;
	int i;

	/* preliminary expansion of 16 to 64 words. */
	arg_assert(data);
	assert((3 & (uintptr_t) data) == 0);
	for (i = 0; i < 16; i++)
		W[i] = swap32(data[i]);
	for (/* nada */; i < 64; i++) {
		uint32_t T1 = W[i - 15], T2 = W[i - 2];
		W[i] = W[i - 16] + (ROR(T1, 7) ^ ROR(T1, 18) ^ LSR(T1, 3)) +
			W[i - 7] + (ROR(T2, 17) ^ ROR(T2, 19) ^ LSR(T2, 10));
	}

	arg_assert(state);
	A = state->H0;
	B = state->H1;
	C = state->H2;
	D = state->H3;
	E = state->H4;
	F = state->H5;
	G = state->H6;
	H = state->H7;

	/*
	 * The majority and choice (x ? y : z) operations are described
	 * in the specification as:
	 *
	 * maj = (A & B) ^ (A & C) ^ (B & C)
	 * ch = (E & F) ^ (~E & G)
	 *
	 * ...but in this implementation they are replaced by the following
	 * equivalent formulations, which require fewer operations:
	 *
	 * maj = (A & B) + (C & (A ^ B))
	 * ch = G ^ (E & (F ^ G))
	 */
#if SHA_CODE_SIZE == 0
	for (i = 0; i < 64; ++i) {
		uint32_t T2 = (ROR(A, 2) ^ ROR(A, 13) ^ ROR(A, 22)) +
			((A & B) + (C & (A ^ B)));
		uint32_t T1 = H + (ROR(E, 6) ^ ROR(E, 11) ^ ROR(E, 25)) +
			(G ^ (E & (F ^ G))) + K[i] + W[i];

		H = G; G = F; F = E; E = D + T1;
		D = C; C = B; B = A; A = T1 + T2;
	}
#elif SHA_CODE_SIZE == 1
	for (i = 0; i < 64; /* nada */) {
		ROUND(A, B, C, D, E, F, G, H, i); ++i;
		ROUND(H, A, B, C, D, E, F, G, i); ++i;
		ROUND(G, H, A, B, C, D, E, F, i); ++i;
		ROUND(F, G, H, A, B, C, D, E, i); ++i;
		ROUND(E, F, G, H, A, B, C, D, i); ++i;
		ROUND(D, E, F, G, H, A, B, C, i); ++i;
		ROUND(C, D, E, F, G, H, A, B, i); ++i;
		ROUND(B, C, D, E, F, G, H, A, i); ++i;
	}
#elif SHA_CODE_SIZE == 2
	i = 0;

	ROUND(A, B, C, D, E, F, G, H, i); ++i;
	ROUND(H, A, B, C, D, E, F, G, i); ++i;
	ROUND(G, H, A, B, C, D, E, F, i); ++i;
	ROUND(F, G, H, A, B, C, D, E, i); ++i;
	ROUND(E, F, G, H, A, B, C, D, i); ++i;
	ROUND(D, E, F, G, H, A, B, C, i); ++i;
	ROUND(C, D, E, F, G, H, A, B, i); ++i;
	ROUND(B, C, D, E, F, G, H, A, i); ++i;

	ROUND(A, B, C, D, E, F, G, H, i); ++i;
	ROUND(H, A, B, C, D, E, F, G, i); ++i;
	ROUND(G, H, A, B, C, D, E, F, i); ++i;
	ROUND(F, G, H, A, B, C, D, E, i); ++i;
	ROUND(E, F, G, H, A, B, C, D, i); ++i;
	ROUND(D, E, F, G, H, A, B, C, i); ++i;
	ROUND(C, D, E, F, G, H, A, B, i); ++i;
	ROUND(B, C, D, E, F, G, H, A, i); ++i;

	ROUND(A, B, C, D, E, F, G, H, i); ++i;
	ROUND(H, A, B, C, D, E, F, G, i); ++i;
	ROUND(G, H, A, B, C, D, E, F, i); ++i;
	ROUND(F, G, H, A, B, C, D, E, i); ++i;
	ROUND(E, F, G, H, A, B, C, D, i); ++i;
	ROUND(D, E, F, G, H, A, B, C, i); ++i;
	ROUND(C, D, E, F, G, H, A, B, i); ++i;
	ROUND(B, C, D, E, F, G, H, A, i); ++i;

	ROUND(A, B, C, D, E, F, G, H, i); ++i;
	ROUND(H, A, B, C, D, E, F, G, i); ++i;
	ROUND(G, H, A, B, C, D, E, F, i); ++i;
	ROUND(F, G, H, A, B, C, D, E, i); ++i;
	ROUND(E, F, G, H, A, B, C, D, i); ++i;
	ROUND(D, E, F, G, H, A, B, C, i); ++i;
	ROUND(C, D, E, F, G, H, A, B, i); ++i;
	ROUND(B, C, D, E, F, G, H, A, i); ++i;

	ROUND(A, B, C, D, E, F, G, H, i); ++i;
	ROUND(H, A, B, C, D, E, F, G, i); ++i;
	ROUND(G, H, A, B, C, D, E, F, i); ++i;
	ROUND(F, G, H, A, B, C, D, E, i); ++i;
	ROUND(E, F, G, H, A, B, C, D, i); ++i;
	ROUND(D, E, F, G, H, A, B, C, i); ++i;
	ROUND(C, D, E, F, G, H, A, B, i); ++i;
	ROUND(B, C, D, E, F, G, H, A, i); ++i;

	ROUND(A, B, C, D, E, F, G, H, i); ++i;
	ROUND(H, A, B, C, D, E, F, G, i); ++i;
	ROUND(G, H, A, B, C, D, E, F, i); ++i;
	ROUND(F, G, H, A, B, C, D, E, i); ++i;
	ROUND(E, F, G, H, A, B, C, D, i); ++i;
	ROUND(D, E, F, G, H, A, B, C, i); ++i;
	ROUND(C, D, E, F, G, H, A, B, i); ++i;
	ROUND(B, C, D, E, F, G, H, A, i); ++i;

	ROUND(A, B, C, D, E, F, G, H, i); ++i;
	ROUND(H, A, B, C, D, E, F, G, i); ++i;
	ROUND(G, H, A, B, C, D, E, F, i); ++i;
	ROUND(F, G, H, A, B, C, D, E, i); ++i;
	ROUND(E, F, G, H, A, B, C, D, i); ++i;
	ROUND(D, E, F, G, H, A, B, C, i); ++i;
	ROUND(C, D, E, F, G, H, A, B, i); ++i;
	ROUND(B, C, D, E, F, G, H, A, i); ++i;

	ROUND(A, B, C, D, E, F, G, H, i); ++i;
	ROUND(H, A, B, C, D, E, F, G, i); ++i;
	ROUND(G, H, A, B, C, D, E, F, i); ++i;
	ROUND(F, G, H, A, B, C, D, E, i); ++i;
	ROUND(E, F, G, H, A, B, C, D, i); ++i;
	ROUND(D, E, F, G, H, A, B, C, i); ++i;
	ROUND(C, D, E, F, G, H, A, B, i); ++i;
	ROUND(B, C, D, E, F, G, H, A, i);
#else
#error illegal SHA_CODE_SIZE
#endif

	/* update generated digest in state */
	state->H0 += A;
	state->H1 += B;
	state->H2 += C;
	state->H3 += D;
	state->H4 += E;
	state->H5 += F;
	state->H6 += G;
	state->H7 += H;
	state->nhashed += SHA_BLOCK_BYTES;
}
