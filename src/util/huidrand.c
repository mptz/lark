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

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#ifdef __linux__
#include <linux/if_link.h>
#include <linux/if_packet.h>
#endif
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "base64.h"
#include "fdutil.h"
#include "huid.h"
#include "message.h"
#include "sha2.h"
#include "twister.h"
#include "twofish.h"

/*
 * CLOCK_BOOTTIME is not POSIX; prepare a fallback.
 */
#ifdef CLOCK_BOOTTIME
#define CLOCK_MONOTONIC_ALTERNATE CLOCK_BOOTTIME
#else
#define CLOCK_MONOTONIC_ALTERNATE CLOCK_MONOTONIC
#endif

extern char **environ;

typedef uint8_t bits256 [32];
struct uint128 { uint64_t lo, hi; };

static void uint128inc(struct uint128 *u128)
{
	uint64_t t = u128->lo++;
	if (t > u128->lo) {
		t = u128->hi++;
		if (t > u128->hi)
			u128->lo = 0;
	}
}

static void env_entropy(struct sha256_state *hash)
{
	for (char **envp = environ; *envp; ++envp)
		sha256_stream_hash(hash, *envp, strlen(*envp));
}

static void file_entropy(const char *pathname, struct sha256_state *hash)
{
	int fd = open(pathname, O_RDONLY);
	if (fd < 0)
		ppanic(pathname);
	char buf [8192];
	ssize_t n = r_read(fd, buf, sizeof buf);
	if (n > 0)
		sha256_stream_hash(hash, buf, n);
	p_close(fd);
}

static void sockaddr_entropy(const struct sockaddr *sa,
			     struct sha256_state *hash)
{
	char hbuf [NI_MAXHOST], sbuf [NI_MAXSERV];

	int family = sa->sa_family;
	if (family == AF_INET || family == AF_INET6) {
		int e = getnameinfo(sa,
			(family == AF_INET) ? sizeof (struct sockaddr_in) :
					      sizeof (struct sockaddr_in6),
			hbuf, sizeof hbuf, sbuf, sizeof sbuf,
			NI_NUMERICHOST | NI_NUMERICSERV);
		if (e) panicf("getnameinfo: %s\n", gai_strerror(e));

		/* IPv4/IPv6 numeric address and service */
		sha256_stream_hash(hash, hbuf, strlen(hbuf));
		sha256_stream_hash(hash, sbuf, strlen(sbuf));
	}
#ifdef __linux__
	if (family == AF_PACKET) {
		/* raw HW address (MAC address) of packet I/F */
		struct sockaddr_ll *ll = (struct sockaddr_ll*) sa;
		sha256_stream_hash(hash, ll->sll_addr, ll->sll_halen);
	}
#endif
}

static void ifdata_entropy(const struct ifaddrs *ifaddr,
			   struct sha256_state *hash)
{
#ifdef __linux__
	if (ifaddr->ifa_addr->sa_family == AF_PACKET && ifaddr->ifa_data) {
		/* raw packet I/F link stats including TX/RX counts, errors,
		   drops, collisions, etc. */
		sha256_stream_hash(hash, ifaddr->ifa_data,
				   sizeof (struct rtnl_link_stats));
	}
#endif
}

static void ifaddr_entropy(struct sha256_state *hash)
{
	struct ifaddrs *ifaddrs, *p;

	if (getifaddrs(&ifaddrs) < 0)
		ppanic("getifaddrs");
	for (p = ifaddrs; p; p = p->ifa_next) {
		if (p->ifa_addr == NULL) continue;
		sha256_stream_hash(hash, p->ifa_name, strlen(p->ifa_name));
		sha256_stream_hash(hash, &p->ifa_flags, sizeof p->ifa_flags);
		sockaddr_entropy(p->ifa_addr, hash);
		ifdata_entropy(p, hash);
	}
	freeifaddrs(ifaddrs);
}

static inline void
xor256bit(void *d, const void *sa, const void *sb)
{
	uint32_t *d32 = d;
	const uint32_t *sa32 = sa, *sb32 = sb;
	for (size_t i = 0; i < 8; ++i)
		d32[i] = sa32[i] ^ sb32[i];
}

/*
 * The feedback stream is basically an ANSI X9.17 PRNG except for:
 *	1) We use SHA-256 as the one-way function instead of 3DES E-D-E.
 *	2) We use a combination of high-resolution time and a conventional
 *	   long-period PRNG, rather than time alone, for intermediates.
 *	3) We combine intermediate + seed with concatenation, not XOR.
 * Since this application is not performance-critical we regenerate the
 * intermediate values for each output, as though always requesting a
 * single value from an ANSI X9.17 PRNG.
 */
static uint32_t feedback_state [16],	/* 512 bits, 1st half I, 2nd half S */
		*feedback_seed = feedback_state + 8;

static void feedback_stream_init(int fd)
{
	if (r_readall(fd, feedback_state, sizeof feedback_state))
		ppanic("read from random source");

	/* this half of feedback_state is not otherwise used */
	unsigned long randseed [8] = {
		feedback_state[0], feedback_state [1],
		feedback_state[2], feedback_state [3],
		feedback_state[4], feedback_state [5],
		feedback_state[6], feedback_state [7],
	};
	init_by_array(randseed, sizeof randseed / sizeof randseed[0]);

	/* inject some additional entropy */
	struct sha256_state hash;
	sha256_stream_start(&hash);
	pid_t pid = getpid();
	sha256_stream_hash(&hash, &pid, sizeof pid);
	env_entropy(&hash);
	ifaddr_entropy(&hash);
	file_entropy("/etc/fstab", &hash);
	file_entropy("/proc/stat", &hash);
	file_entropy("/proc/uptime", &hash);
	file_entropy("/proc/version", &hash);
	bits256 entropy;
	sha256_stream_finish(&hash, entropy);
	xor256bit(feedback_seed, feedback_seed, entropy);
}

static void feedback_stream_next(void *out256)
{
	/*
	 * Interleave various flavors of current-time-nanoseconds with the
	 * Mersenne Twister, which is not cryptographically strong (it
	 * doesn't need to be for this application) but which has a very
	 * long period.
	 */
	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts))
		ppanic("clock_gettime");
	feedback_state[0] = ts.tv_nsec;
	if (clock_gettime(CLOCK_MONOTONIC, &ts))
		ppanic("clock_gettime");
	feedback_state[1] = ts.tv_nsec;
	feedback_state[2] = genrand();
	feedback_state[3] = genrand();
	if (clock_gettime(CLOCK_MONOTONIC_ALTERNATE, &ts))
		ppanic("clock_gettime");
	feedback_state[4] = ts.tv_nsec;
	feedback_state[5] = genrand();
	feedback_state[6] = genrand();
	if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts))
		ppanic("clock_gettime");
	feedback_state[7] = ts.tv_nsec;

	/*
	 * Now calculate the next value and updated state.
	 */
	sha256_hash(out256, feedback_state, sizeof feedback_state);
	memcpy(feedback_state + 8, out256, 256 / 8);
	sha256_hash(feedback_state + 8, feedback_state, sizeof feedback_state);
}

/*
 * The nonce stream runs independently of the feedback stream and is XOR'd
 * with the feedback stream on the way out.  It's a cryptographic PRNG using
 * the Twofish block cipher in counter mode with a random 128-bit starting
 * point and a key derived from the nonce and attestor strings via SHA-256.
 */
static keyInstance nonce_key;
static cipherInstance nonce_cipher;
static struct uint128 nonce_counter;

static void nonce_stream_init(int fd, const char *nonce, const char *attestor)
{
	/* initial state derived from the provided strings */
	struct sha256_state hash;
	sha256_stream_start(&hash);
	sha256_stream_hash(&hash, nonce, strlen(nonce));
	sha256_stream_hash(&hash, attestor, strlen(attestor));

	/* mingle with some additional entropy */
	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts))
		ppanic("clock_gettime");
	sha256_stream_hash(&hash, &ts.tv_sec, sizeof ts.tv_sec);
	sha256_stream_hash(&hash, &ts.tv_nsec, sizeof ts.tv_nsec);
	file_entropy("/etc/passwd", &hash);
	file_entropy("/proc/meminfo", &hash);
	file_entropy("/proc/partitions", &hash);
	file_entropy("/proc/version", &hash);
	bits256 keymat;
	sha256_stream_finish(&hash, keymat);

	makeKey(&nonce_key, DIR_ENCRYPT, 256, NULL);
	memcpy(nonce_key.key32, keymat, sizeof keymat);
	twofish_rekey(&nonce_key);
	cipherInit(&nonce_cipher, MODE_ECB, NULL);	/* no IV for ECB */

	if (r_readall(fd, &nonce_counter, sizeof nonce_counter))
		ppanic("read from random source");
}

static void nonce_stream_next(void *out256)
{
	void *out1h = out256, *out2h = ((uint8_t*) out256) + 16;
	twofish_encrypt(&nonce_cipher, &nonce_key, &nonce_counter, 1, out1h);
	uint128inc(&nonce_counter);
	twofish_encrypt(&nonce_cipher, &nonce_key, &nonce_counter, 1, out2h);
	uint128inc(&nonce_counter);
}

static int huid_initialized;

void huid_init(const char *nonce, const char *attestor)
{
	if (huid_initialized) return;
	if (!nonce) panic("Missing nonce!\n");
	if (!attestor) panic("Missing attestor!\n");

	int fd;
	if ((fd = open("/dev/urandom", O_RDONLY)) < 0)
		ppanic("open /dev/urandom");
	feedback_stream_init(fd);
	nonce_stream_init(fd, nonce, attestor);
	p_close(fd);

	huid_initialized = 1;
}

void huid_fresh(void *buf, unsigned bufsize)
{
	if (bufsize < HUID_BYTES) panic("Buffer too small!\n");
	if (!huid_initialized) panic("HUID generation not initialized!\n");

	bits256 feedbackbuf, noncebuf, mixbuf;
	feedback_stream_next(feedbackbuf);
	nonce_stream_next(noncebuf);
	xor256bit(mixbuf, feedbackbuf, noncebuf);
	memcpy(buf, mixbuf, HUID_BYTES);
}
