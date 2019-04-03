#ifndef LARK_UTIL_SHA2_H
#define LARK_UTIL_SHA2_H
/*
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

#include <stddef.h>	/* for size_t */
#include <stdint.h>

/*
 * Number of bytes in binary and hex ASCII SHA-2 digests.
 */
#define SHA256_BIN_BYTES 32
#define SHA256_HEX_BYTES (SHA256_BIN_BYTES * 2)
#define SHA256_STR_BYTES (SHA256_HEX_BYTES + 1)

/*
 * SHA-256 digest of an empty (null) data stream.
 */
extern const char SHA256_NULL_HASH[];

/*
 * Compute SHA-256 hash for 'srcsize' bytes contained in 'src',
 * putting the 32-byte resulting hash in 'hash'.
 */
extern void
sha256_hash(void *hash, const void *src, size_t srcsize);

/*
 * Compute SHA-256 hash for 'src' as above, but format the
 * results as a 64-byte ASCII hex string in 'hash'.  This
 * function does not NULL-terminate the string.
 */
extern void
sha256_ascii_hash(char *hash, const void *src, size_t srcsize);

/*
 * These spit out an error message if the digests don't match.
 */
extern int
sha256_compare(const void *expected, const void *actual);

/*
 * Perform the same comparison with hashes already converted to strings.
 */
extern int
sha256_compare_string(const char *expected, const char *actual);

/*
 * Compute a hash over a whole file.
 */
extern int
sha256_hash_file(void *hash, const char *pathname);

/*
 * Compute a hash over the data read from a file descriptor.
 */
extern int
sha256_hash_fd(void *hash, int fd);

/*
 * Helper function to convert a 32-byte binary SHA-256 hash into
 * the 64-byte ASCII representation.  The two pointers given
 * may be the same (that is, the conversion can be done in-
 * place).
 */
extern void
sha256_to_ascii(char *aschash, const void *binhash);

/*
 * Internal state for the SHA-256 transform, for streaming use cases.
 */
struct sha256_state {
	unsigned char buf [64];	/* must be uint32_t aligned */
	uint64_t nhashed;
	uint32_t H0, H1, H2, H3, H4, H5, H6, H7;
	unsigned char nbuffered;
};

/*
 * Initialize the state for streaming.
 */
extern void
sha256_stream_start(struct sha256_state *state);

/*
 * This function will perform better if you always pass in
 * chunks whose sizes are multiples of 4 bytes (this will
 * keep down the number of copies needed to align the data).
 */
extern void
sha256_stream_hash(struct sha256_state *state, const void *src, size_t srcsize);

/*
 * Complete a stream and place the resulting binary hash in 'hash'.
 * The state is reinitialized for a subsequent streaming operation.
 */
extern void
sha256_stream_finish(struct sha256_state *state, void *hash);

static inline void
sha256_to_string(char *strhash, const void *binhash)
{
	sha256_to_ascii(strhash, binhash);
	strhash[SHA256_HEX_BYTES] = '\0';
}

#endif /* LARK_UTIL_SHA_H */
