#ifndef LARK_UTIL_TWOFISH_H
#define LARK_UTIL_TWOFISH_H
/*
 * Copyright (c) 2009-2015 Michael P. Touloumtzis.
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
/***************************************************************************
			
	Code Author:		Doug Whiting,	Hi/fn
	Version  1.00		April 1998
	Copyright 1998, Hi/fn and Counterpane Systems.  All rights reserved.

	"Twofish is unpatented, and the source code is uncopyrighted and
	 license-free; it is free for all uses."

***************************************************************************/

#include <stdint.h>

typedef uint32_t fullSbox [4][256];

#define DIR_ENCRYPT 	0 	/* Are we encrpyting? */
#define DIR_DECRYPT 	1 	/* Are we decrpyting? */
#define MODE_ECB 	1 	/* Are we ciphering in ECB mode? */
#define MODE_CBC 	2 	/* Are we ciphering in CBC mode? */
#define MAX_KEY_SIZE	64	/* in ASCII chars, not raw binary */
#define	MAX_KEY_BITS	256	/* max number of bits of key */
#define	MIN_KEY_BITS	128	/* min number of bits of key (zero pad) */
#define	MAX_IV_SIZE	16	/* # of bytes needed to represent an IV */
#define	BLOCK_BITS	128	/* number of bits per block */
#define	BLOCK_BYTES	16	/* number of bytes per block */
#define NUM_ROUNDS	16	/* uniform for all key lengths */
#define	INPUT_WHITEN	0	/* subkey array indices */
#define	OUTPUT_WHITEN	(INPUT_WHITEN + BLOCK_BITS/32)
#define	ROUND_SUBKEYS	(OUTPUT_WHITEN + BLOCK_BITS/32)	/* use 2 * (# rounds) */
#define	TOTAL_SUBKEYS	(ROUND_SUBKEYS + 2*NUM_ROUNDS)

/* The structure for key information */
typedef struct {
	uint8_t direction;	/* encrypting or decrypting? */
	int keyLen;		/* Length of the key */
	char keyMaterial [MAX_KEY_SIZE+4];	/* Raw key data in ASCII */

	/* Twofish-specific parameters: */
	uint32_t keySig;		/* set to VALID_SIG by makeKey() */
	uint32_t key32 [MAX_KEY_BITS/32]; 	/* actual key bits */
	uint32_t sboxKeys [MAX_KEY_BITS/64];	/* key bits used for S-boxes */
	uint32_t subKeys [TOTAL_SUBKEYS];
				/* round subkeys, input/output whitening bits */
	fullSbox sBox8x32;			/* fully expanded S-box */
} keyInstance;

/* The structure for cipher information */
typedef struct {
	uint8_t mode;			/* MODE_ECB, MODE_CBC, or MODE_CFB1 */
	uint8_t IV[MAX_IV_SIZE];	/* was used for CFB1, now test only */

	/* Twofish-specific parameters: */
	uint32_t cipherSig;		/* set to VALID_SIG by cipherInit() */
	uint32_t iv32[BLOCK_BITS/32];	/* CBC IV bytes arranged as dwords */
} cipherInstance;

/* Function protoypes */
extern int makeKey(keyInstance *key, uint8_t direction, int keyLen,
		   char *keyMaterial);
extern int cipherInit(cipherInstance *cipher, uint8_t mode, char *IV);
extern int twofish_encrypt(cipherInstance *cipher, keyInstance *key,
			   const void *input, unsigned nblocks, void *output);
extern int twofish_decrypt(cipherInstance *cipher, keyInstance *key,
			   const void *input, unsigned nblocks, void *output);
extern void twofish_rekey(keyInstance *key);	/* recalc key schedule */

#endif /* LARK_UTIL_TWOFISH_H */
