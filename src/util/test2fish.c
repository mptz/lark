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

#include <ctype.h>
#include <endian.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "twofish.h"

typedef uint32_t uint32;
typedef uint8_t uint8;

static inline uint32 rol(uint32 x, unsigned n)
	{ return (x << n) | (x >> (32-n)); }
static inline uint32 ror(uint32 x, unsigned n)
	{ return (x >> n) | (x << (32-n)); }

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define	ADDR_XOR	0	/* NOP for little-endian machines */
#else
#define	ADDR_XOR	3	/* convert byte address in dword */
#endif

#define	MCT_OUTER	400	/* MCT outer loop */
#define	MCT_INNER	10000	/* MCT inner loop */

/* API to check table usage, for use in ECB_TBL KAT */
#define		TAB_DISABLE			0
#define		TAB_ENABLE			1
#define		TAB_RESET			2
#define		TAB_QUERY			3
#define		TAB_MIN_QUERY		50
int TableOp(int op);


static inline void block_copy(void *dst, const void *src)
	{ memcpy(dst, src, BLOCK_BYTES); }

#ifdef TEST_2FISH
/*						----- EXAMPLES -----

Unfortunately, the AES API is somewhat clumsy, and it is not entirely
obvious how to use the above functions.  In particular, note that
makeKey() takes an ASCII hex nibble key string (e.g., 32 characters
for a 128-bit key), which is rarely the way that keys are internally
represented.  The reKey() function uses instead the keyInstance.key32
array of key bits and is the preferred method.  In fact, makeKey()
initializes some internal keyInstance state, then parse the ASCII
string into the binary key32, and calls reKey().  To initialize the
keyInstance state, use a 'dummy' call to makeKey(); i.e., set the
keyMaterial parameter to NULL.  Then use reKey() for all key changes.
Similarly, cipherInit takes an IV string in ASCII hex, so a dummy setup
call with a null IV string will skip the ASCII parse.  

Note that CFB mode is not well tested nor defined by AES, so using the
Twofish MODE_CFB it not recommended.  If you wish to implement a CFB mode,
build it external to the Twofish code, using the Twofish functions only
in ECB mode.

Below is a sample piece of code showing how the code is typically used
to set up a key, encrypt, and decrypt.  Error checking is somewhat limited
in this example.  Pseudorandom bytes are used for all key and text.

If you compile TWOFISH2.C or TWOFISH.C as a DOS (or Windows Console) app
with this code enabled, the test will be run.  For example, using
Borland C, you would compile using:
  BCC32 -DTEST_2FISH twofish2.c
to run the test on the optimized code, or
  BCC32 -DTEST_2FISH twofish.c
to run the test on the pedagogical code.

*/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#define MAX_BLK_CNT		4		/* max # blocks per call in TestTwofish */
int TestTwofish(int mode,int keySize) /* keySize must be 128, 192, or 256 */
	{							/* return 0 iff test passes */
	keyInstance    ki;			/* key information, including tables */
	cipherInstance ci;			/* keeps mode (ECB, CBC) and IV */
	uint8  plainText[MAX_BLK_CNT*BLOCK_BYTES];
	uint8 cipherText[MAX_BLK_CNT*BLOCK_BYTES];
	uint8 decryptOut[MAX_BLK_CNT*BLOCK_BYTES];
	uint8 iv[BLOCK_BYTES];
	int  i,byteCnt;

	if (makeKey(&ki,DIR_ENCRYPT,keySize,NULL) != true)
		return 1;				/* 'dummy' setup for a 128-bit key */
	if (cipherInit(&ci,mode,NULL) != true)
		return 1;				/* 'dummy' setup for cipher */
	
	for (i=0;i<keySize/32;i++)	/* select key bits */
		ki.key32[i]=0x10003 * rand();
	twofish_rekey(&ki);		/* run the key schedule */

	if (mode != MODE_ECB)		/* set up random iv (if needed)*/
		{
		for (i=0;i<sizeof(iv);i++)
			iv[i]=(uint8) rand();
		memcpy(ci.iv32,iv,sizeof(ci.iv32));	/* copy the IV to ci */
		}

	/* select number of bytes to encrypt (multiple of block) */
	/* e.g., byteCnt = 16, 32, 48, 64 */
	byteCnt = BLOCK_BYTES * (1 + (rand() % MAX_BLK_CNT));

	for (i=0;i<byteCnt;i++)		/* generate test data */
		plainText[i]=(uint8) rand();
	
	/* encrypt the bytes */
	if (twofish_encrypt(&ci,&ki, plainText,byteCnt/BLOCK_BYTES,cipherText))
		return 1;

	/* decrypt the bytes */
	if (mode != MODE_ECB)		/* first re-init the IV (if needed) */
		memcpy(ci.iv32,iv,sizeof(ci.iv32));

	if (twofish_decrypt(&ci,&ki,cipherText,byteCnt/BLOCK_BYTES,decryptOut))
		return 1;				
	
	/* make sure the decrypt output matches original plaintext */
	if (memcmp(plainText,decryptOut,byteCnt))
		return 1;		

	return 0;					/* tests passed! */
	}

void main(void)
	{
	int testCnt,keySize;

	srand((unsigned) time(NULL));	/* randomize */

	for (keySize=128;keySize<=256;keySize+=64)
		for (testCnt=0;testCnt<10;testCnt++)
			{
			if (TestTwofish(MODE_ECB,keySize))
				{ printf("ECB Failure at keySize=%d",keySize); return; }
			if (TestTwofish(MODE_CBC,keySize))
				{ printf("CBC Failure at keySize=%d",keySize); return; }
			}
	printf("Tests passed");
	}
#endif /* TEST_2FISH */

/*
+*****************************************************************************
*			Constants/Macros/Tables
-****************************************************************************/

typedef struct {
	FILE *f;		/* the file being written/read */
	int  I;			/* test number */
	int	 keySize;	/* key size in bits */
	uint8 pt[BLOCK_BYTES];	/* plaintext */
	uint8 ct[BLOCK_BYTES];	/* ciphertext */

	keyInstance    ki;	/* use ki.keyDwords as key bits */
	cipherInstance ci;	/* use ci.iv as iv bits */
} testData;


static char hexTab[]	=	"0123456789ABCDEF";
char	filePath[80]=	"";

int	useAsm		=	0;	/* use assembly language */
int	mctInner	=	MCT_INNER;
int	mctOuter	=	MCT_OUTER;
int	verify		=	0;	/* set to nonzero to read&verify files */
int	verbose		=	0;	/* verbose output */
int	quietVerify	=	0;	/* quiet during verify */
int	timeIterCnt	=	0;	/* how many times to iterate for timing */
uint32	randBits[64]= {1};	/* use Knuth's additive generator */
int	randPtr;
int	CLKS_BYTE	=	0;	/* use clks/byte? (vs. clks/block) */
int	FMT_LOG		=	0;	/* format for log file */

#define		KEY_BITS_0			128			/* first key bit setting to test */
#define		STEP_KEY_BITS		((MAX_KEY_BITS-KEY_BITS_0)/2)

static char  hexString[]=
		"0123456789ABCDEFFEDCBA987654321000112233445566778899AABBCCDDEEFF";


/*
+*****************************************************************************
*
* Function Name:	Rand
*
* Function:			Generate random number
*
* Arguments:		None.
*
* Return:			New random number.
*
* Notes:			Uses Knuth's additive generator, other magic
*
-****************************************************************************/
static uint32 Rand(void)
{
	if (randPtr >= 57)
		randPtr = 0;			/* handle the ptr wrap */

	randBits[randPtr] += randBits[(randPtr < 7) ? randPtr-7+57 : randPtr-7];

	randBits[62]+= randBits[61];
	randBits[63] = rol(randBits[63],9) + 0x6F4ED7D0;	/* very long period! */
	
	return (randBits[randPtr++] ^ randBits[63]) + randBits[62];
}


/*
+*****************************************************************************
*
* Function Name:	SetRand
*
* Function:			Initialize random number seed
*
* Arguments:		seed	=	new seed value
*
* Return:			None.
*
* Notes:			
*
-****************************************************************************/
static void SetRand(uint32 seed)
	{
	int i;
	uint32 x;

	randPtr=0;
	for (i=x=0;i<64;i++)
		{
		randBits[i]=seed;
		x |= seed;		/* keep track of lsb of all entries */
		seed = rol(seed,11) + 0x12345678;
		}

	if ((x & 1) == 0)	/* insure maximal period by having at least one odd value */
		randBits[0]++;

	for (i=0;i<1000;i++)
		Rand();			/* run it for a while */

	randBits[63] = Rand();
	randBits[62] = Rand();
	randBits[61] = Rand() | 1;	/* make it odd */
	}


/*
+*****************************************************************************
*
* Function Name:	ClearTestData
*
* Function:			Initialize test data to all zeroes
*
* Arguments:		t		=	pointer to testData structure
*
* Return:			None.
*
* Notes:			
*
-****************************************************************************/
static void ClearTestData(testData *t)
{
	memset(t->pt,0,BLOCK_BYTES);
	memset(t->ct,0,BLOCK_BYTES);
	memset(t->ci.iv32,0,BLOCK_BYTES);
	memset(t->ki.key32,0,MAX_KEY_BITS/8);
	memset(t->ki.keyMaterial,'0',sizeof(t->ki.keyMaterial));
}

/*
+*****************************************************************************
*
* Function Name:	FatalError
*
* Function:			Output a fatal error message and exit
*
* Arguments:		msg		=	fatal error description (printf string)
*					msg2	=	2nd parameter to printf msg
*
* Return:			None.
*
* Notes:
*
-****************************************************************************/
static void FatalError(const char *msg,const char *msg2)
	{
	printf("\nFATAL ERROR: ");
	printf(msg,msg2);
	abort();
	}


/*
+*****************************************************************************
*
* Function Name:	TimeOps
*
* Function:			Time encryption/decryption and print results
*
* Arguments:		iterCnt	= how many calls to make
*
* Return:			None.
*
* Notes:			None.
*
-****************************************************************************/
static void TimeOps(int iterCnt)
	{
	enum { TEST_CNT	= 3, BLOCK_CNT=64 };
	int   i,j,k,n,q;
	uint32 t0,t1,dt[8],minT;
	uint32 testTime[3][TEST_CNT];
	testData t;
	uint8 text[BLOCK_CNT*BLOCK_BYTES];
	static char *testName[TEST_CNT]={"BlockEncrypt:","BlockDecrypt:","reKeyEncrypt:"};
	static char *atomName[TEST_CNT]={"block","block","call "};
	static char *format  [TEST_CNT]={"%10.1f/%s ","%10.1f/%s ","%10.1f/%s "};
	static int	 denom   [TEST_CNT]={BLOCK_CNT,BLOCK_CNT,1};
	static int	 needSet [TEST_CNT]={1,1,0};

	ClearTestData(&t);
	for (i=0;i<TEST_CNT;i++)
		{
		if (needSet[i] & 1)
			{
			denom[i]=sizeof(text)/((CLKS_BYTE) ? 1 : BLOCK_BYTES );
			atomName[i] = (CLKS_BYTE) ? "byte "      : "block";
			}
		format  [i] = (CLKS_BYTE) ? "%10.1f/%s " : "%10.0f/%s ";
		}

	for (i=0;i<MAX_KEY_SIZE;i++)		/* generate random key material */
		t.ki.keyMaterial[i]=hexTab[Rand() & 0xF];
	for (j=0;j<sizeof(text);j++)
		text[j]=(uint8) Rand();
	memset(dt,0,sizeof(dt));
	dt[0]++;							/* make sure it's in the cache */

	/* calibrate our timing code */
	t0=clock();	t1=clock();	t0++; t1++;	/* force cache line fill */
	for (i=0;i<sizeof(dt)/sizeof(dt[0]);i++)
		{
		t0=clock();
		t1=clock();
		dt[i]=t1-t0;
		}

	for (n=0;n<TEST_CNT;n++)			/* gather all data into testTime[][] */
		{
		for (t.keySize=KEY_BITS_0,q=0;t.keySize<=MAX_KEY_BITS;t.keySize+=STEP_KEY_BITS,q++)
			{
			cipherInit(&t.ci,MODE_ECB,NULL);
			makeKey(&t.ki,DIR_ENCRYPT,t.keySize,t.ki.keyMaterial);

#if defined(HI_RES_CLK)
#define		CALL_N						/* just call once */
#define		ICNT	1.0
#define		TSCALE	1000.0
			for (k=0,minT=~0u;k<iterCnt;k++)
#else
#define		CALL_N	for (j=0;j<iterCnt;j++)
#define		ICNT	((double)iterCnt)
#define		TSCALE	1000.0
			for (k=0,minT=~0u;k<4;k++)
#endif
				{	/* run a few times to get "best" time */
				switch (n)
					{
					case 0:
						twofish_encrypt(&t.ci,&t.ki,text,sizeof(text)/BLOCK_BYTES,text);
						t0=clock();
						CALL_N twofish_encrypt(&t.ci,&t.ki,text,sizeof(text)/BLOCK_BYTES,text);
						t1=clock();
						break;
					case 1:
						twofish_decrypt(&t.ci,&t.ki,text,sizeof(text)/BLOCK_BYTES,text);
						t0=clock();
						CALL_N twofish_decrypt(&t.ci,&t.ki,text,sizeof(text)/BLOCK_BYTES,text);
						t1=clock();
						break;
					case 2:
						twofish_rekey(&t.ki);
						t0=clock();
						CALL_N { t.ki.key32[0]+=0x87654321;	/* change key bytes to force cache misses */
								 t.ki.key32[1]+=0x9ABCDEF3;
								 twofish_rekey(&t.ki); }
						t1=clock();
						break;
					default:
						FatalError("Unknown test","");
						break;
					}
				if (minT > t1-t0)
					minT = t1-t0;
				}
			testTime[q][n]=minT;
			}
		}
	/* now print all the results */
#ifdef HI_RES_CLK
	if (!FMT_LOG)
		{
		printf("\nCalibrate clock(): ",t1-t0);
		for (i=0;i<sizeof(dt)/sizeof(dt[0]);i++)
			printf("%6ld",dt[i]);
		printf("\n\n");
		}
#else
	printf("All times in clocks * 1000\n");
	printf("CLOCKS_PER_SEC = %8.1f\n",(double)CLOCKS_PER_SEC);
#endif

	printf("%-13s","keySize=");
	for (t.keySize=KEY_BITS_0;t.keySize<=MAX_KEY_BITS;t.keySize+=STEP_KEY_BITS)
		printf("%10d bits  ",t.keySize);
	printf("\n");
		
	for (n=0;n<TEST_CNT;n++)
		{
		printf("%-13s",testName[n]);
		for (q=0;q<3;q++)
			{
			printf(format[n],TSCALE*testTime[q][n]/(ICNT*(double)denom[n]),atomName[n]);
			}
		printf("\n");
		}
	if (FMT_LOG)
		printf(";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n");
	}


/*
+*****************************************************************************
*
* Function Name:	AES_Sanity_Check
*
* Function:			Make sure things work to the interface spec and
*					that encryption and decryption are inverse functions
*
* Arguments:		None.
*
* Return:			None.
*
* Notes:			Will FatalError if any problems found
*
-****************************************************************************/
static void AES_Sanity_Check(int testCnt)
	{
	static uint32 hexVal[] =
			{0x67452301,0xEFCDAB89,0x98BADCFE,0x10325476,
			 0x33221100,0x77665544,0xBBAA9988,0xFFEEDDCC};
	static char *modeNames[]={"(null)","MODE_ECB","MODE_CBC",};

	int i,j,q,n,testNum,lim;
	testData t;
	keyInstance k2;
	uint8 pt[128];
	uint8 ct[128];
	char ivString[BLOCK_BITS/4];
	char *mName;
	unsigned mode;

	if (!quietVerify) printf("\nTwofish code sanity check...");
#if (MODE_CBC != MODE_ECB + 1)
#error Need to change mode loop constants
#endif
	if (testCnt)
	for (mode=MODE_ECB;mode<=MODE_CBC;mode++)
		{
		mName=modeNames[mode];
		if (cipherInit(&t.ci,mode,hexString) != true)
			FatalError("cipherInit error during sanity check %s",mName);
		if (t.ci.mode != mode)
			FatalError("Cipher mode not set properly during sanity check %s",mName);
		if (mode != MODE_ECB)
			for (i=0;i<BLOCK_BITS/32;i++)
				if (t.ci.iv32[i] != hexVal[i])
					FatalError("Invalid IV parse during sanity check %s",mName);
		lim = testCnt;
		for (t.keySize=KEY_BITS_0;t.keySize <= MAX_KEY_BITS;t.keySize+=STEP_KEY_BITS)
			{
			/* printf("Running %-9s sanity check on keySize = %3d.\n",mName,t.keySize); */
			if (!quietVerify) printf(".");	/* show some progress */
			ClearTestData(&t);
			if (makeKey(&t.ki,DIR_ENCRYPT,t.keySize,hexString) != true)
				FatalError("Error parsing key during sanity check %s",mName);
			for (i=0;i<t.keySize/32;i++)
				if (t.ki.key32[i]!=hexVal[i])
					FatalError("Invalid key parse during sanity check %s",mName);
			for (testNum=0;testNum<lim;testNum++)
				{						/* run a bunch of encode/decode tests */
				if ((testNum&0x1F)==0)	/* periodic re-key time? */
					{
					for (j=0;j<t.keySize/4;j++)
						t.ki.keyMaterial[j]=hexTab[Rand() & 0xF];
					if (testNum==0)
						ClearTestData(&t);	/* give "easy" test data the first time */
					if (makeKey(&t.ki,DIR_ENCRYPT,t.keySize,t.ki.keyMaterial) != true)
						FatalError("Encrypt makeKey during sanity check %s",mName);
					if (makeKey(&k2  ,DIR_DECRYPT,t.keySize,t.ki.keyMaterial) != true)
						FatalError("Decrypt makeKey during sanity check %s",mName);
					}
				if (mode != MODE_ECB)				/* set IV  if needed*/
					for (j=0;j<BLOCK_BITS/4;j++)
						ivString[j]=hexTab[(testNum)? Rand() & 0xF : 0];
				if (testNum == 0)
					n = BLOCK_BYTES;
				else
					n = BLOCK_BYTES*(1 + (Rand() % (sizeof(pt)/BLOCK_BYTES)));

				for (j=0;j<n;j++)					/* set random plaintext */
					pt[j]=(testNum) ? (uint8) Rand() : 0;
				if (mode == MODE_CBC)
					{	/* check that CBC works as advertised */
					cipherInit(&t.ci,mode,ivString);
					t.ci.mode=MODE_ECB;
					for (q=0;q<BLOCK_BYTES;q++)	/* copy over the iv */
						t.pt[q] = (uint8) (t.ci.iv32[q/4] >> (8*(q&3)));	/* auto-Bswap! */
					for (j=0;j<n;j+=BLOCK_BYTES)
						{
						for (q=0;q<BLOCK_BYTES;q++)	/* xor in next block */
							t.pt[q] ^= pt[j+q];
						if (twofish_encrypt(&t.ci,&t.ki,t.pt,1,t.pt))
							FatalError("twofish_encrypt return value during sanity check %s",mName);
						}
					t.ci.mode=MODE_CBC;			/* restore mode */
					}
				/* encrypt */
				cipherInit(&t.ci,mode,ivString);
				if ((testNum < 4) || (Rand() & 1))
					{
					if (twofish_encrypt(&t.ci,&t.ki,pt,n/BLOCK_BYTES,ct))
						FatalError("twofish_encrypt return value during sanity check %s",mName);
					}
				else	/* do it in pieces */
					for (j=0;j<n;j+=BLOCK_BYTES)
						if (twofish_encrypt(&t.ci,&t.ki,pt+j,1,ct+j))
						FatalError("twofish_encrypt return value during sanity check %s",mName);
							
				if (mode == MODE_CBC)			/* validate CBC "hash" */
					for (q=0;q<BLOCK_BYTES;q++)
						if (t.pt[q] != ct[n-BLOCK_BYTES+q])
							FatalError("CBC doesn't work during sanity check %s",mName);
				/* decrypt */
				cipherInit(&t.ci,mode,ivString);
				if ((testNum < 4) || (Rand() & 1))
					{
					if (twofish_decrypt(&t.ci,&t.ki,ct,n/BLOCK_BYTES,ct))
						FatalError("twofish_decrypt return value during sanity check %s",mName);
					}
				else	/* do it in pieces */
					for (j=0;j<n;j+=BLOCK_BYTES)
						if (twofish_decrypt(&t.ci,&t.ki,ct+j,1,ct+j))
						FatalError("twofish_encrypt return value during sanity check %s",mName);

				/* compare */
				for (j=0;j<n;j++)
					if (pt[j] != ct[j])
						{
						char s[128];
						sprintf(s,"Sanity check: encrypt/decrypt miscompare (mode=%s,keySize=%d)",
								mName,t.keySize);
						FatalError(s,"");
						}
				}
			}
		}
	if (!quietVerify) printf("  OK\n");
	}


/*
+*****************************************************************************
*
* Function Name:	AES_FileIO
*
* Function:			Output to file or verify file contents vs. string
*
* Arguments:		f		=	opened file
*					s		=	string to output/compare (NULL-->reset, return)
*					errOK	=	do not fatalError on miscompare
*
* Return:			Zero --> compare ok
*
* Notes:			On miscompare, FatalError (unless errOK)
*
-****************************************************************************/
static int AES_FileIO(FILE *f,const char *s,int errOK)
	{
	int  i;
	static int  lineNum=0;
	static int  j=0;
	static char line[516]="";

	if (s == NULL)	/* starting new file */
		{
		line[0]=j=lineNum=0;
		return 0;
		}

	if (!verify)
		{
		fputs(s, f);
		return 0;
		}
				
	/* here to verify the file against the string */
	for (i=0;s[i];i++)
		{
		while (line[j] == 0)
			{
			lineNum++;
			if (fgets(line,sizeof(line)-4,f) == NULL)
				{
				if ((s[i]=='\n') && (s[i+1]==0))
					{
					line[0]=j=0;	/* missing final eol is ok */
					return 0;
					}
				FatalError("Unexpected EOF looking for %s",s);
				}
			if (verbose) fputs(line, stdout);
			j=0;
			}
		if (s[i] != line[j])
			{
			if ((s[i] == '\n') && ((i==0) || (s[i-1] == '\n'))) continue; /* blank line skip */
			if (line[j] == '\n') {j++; continue; }
			if (!errOK)
				{
				char tmp[1024];
				sprintf(tmp,"Miscompare at line #%d:\n%s\nlooking for\n\n%%s",lineNum,line);
				FatalError(tmp,s);
				}
			line[0]=j=0;	/* let caller re-synch if desired */
			return 1;		/* return error flag */
			}
		j++;
		}

	return 0;
	}



/*
+*****************************************************************************
*
* Function Name:	AES_PutFileHeader
*
* Function:			Output a text header for AES test file
*
* Arguments:		fileName	=	name of file to create
*					testName	=	name of the specific test
*
* Return:			Open FILE pointer
*
* Notes:			If unable to create, gives FatalError
*
-****************************************************************************/
static FILE *AES_PutFileHeader(const char *fileName,const char *testName)
	{
	char s[512];
	FILE *f;

	sprintf(s,"%s%s",filePath,fileName);
	if (verify)
		{
		if (!quietVerify) printf("Verifying file %s",s);
		f=fopen(s,"rt");
		AES_FileIO(NULL,NULL,0);		/* reset file read state */
		}
	else
		{
		printf("Creating file %s.\n",s);
		f=fopen(s,"wt");
		}
	if (f == NULL) FatalError("Unable to open file '%s'",s);

	sprintf(s,
			"\n=========================\n"
			"\n"
			"FILENAME:  \"%s\"\n"
			"\n"
			"%s\n"
			"\n"
			"Algorithm Name:       TWOFISH\n"
			"Principal Submitter:  Bruce Schneier, Counterpane Systems\n"
			"\n"
			"==========\n"
			"\n",
			fileName,testName);

	if (AES_FileIO(f,s,1))		
		{						/* header mismatch */
		if (!verify)
			FatalError("Miscompare while not verifying??","");
		printf("  \tWARNING:  header mismatch!");
		(void) fgets(s,sizeof(s)-4,f);
		do	{					/* skip rest of "bad" header */
			if (fgets(s,sizeof(s)-4,f) == NULL)
				break;			/* end of file? */
			}
		while ((s[0] != '=') || (s[1] != '='));
		(void) fgets(s,sizeof(s)-4,f);	/* skip trailing blank line */
		}

	if (verify)
		if (!quietVerify) printf("\n");

	return f;
	}

/*
+*****************************************************************************
*
* Function Name:	AES_PutTestResult
*
* Function:			Output a test result
*
* Arguments:		f		=	output file
*					name	=	name of field
*					p		=	pointer to bytes/dwords
*					cnt		=	# bytes to output
*					fmt32	=	nonzero --> p points to dwords, else bytes
* Return:			None.
*
* Notes:
*
-****************************************************************************/
static void AES_PutBytes(FILE *f,const char *name,const void *p,int cnt,int fmt32)
	{
	char s[128];
	int i,j,a;
	if (p == NULL) return;

	a = (fmt32) ? ADDR_XOR : 0;	/* handle big/little endian on dword i/o */

	sprintf(s,"%s=",name);
	for (j=0;s[j];j++) ;
	for (i=0;i<cnt;i++)
		{
		s[j++]=hexTab[((uint8 *)p)[i ^ a] >> 4 ];
		s[j++]=hexTab[((uint8 *)p)[i ^ a] & 0xF];
		}
	s[j++]='\n';
	s[j  ]=0;	/* terminate the string */

	AES_FileIO(f,s,0);
	}


/*
+*****************************************************************************
*
* Function Name:	AES_printf
*
* Function:			Output a test result
*
* Arguments:		t		=	testData (includes output file)
*					fmt		=	format list (string of chars, see notes)
*
* Return:			None.
*
* Notes:
*	The fmt string specifies what is output. The following characters are
*	treated specially (S,K,P,C,v,V,I).  See the code in the switch statement
*	to see how they are handled.  All other characters (e.g., '\n') are
*	simply output to the file.
*
-****************************************************************************/
static void AES_printf(testData *t,const char *fmt)
	{
	char s[40];

	for (s[1]=0;*fmt;fmt++)
		switch (*fmt)
			{
			case 'I': sprintf(s,"I=%d\n",t->I);				AES_FileIO(t->f,s,0);	break;
			case 'S': sprintf(s,"KEYSIZE=%d\n",t->keySize); AES_FileIO(t->f,s,0);	break;
			case 'P': AES_PutBytes(t->f,"PT" ,t->pt		 ,BLOCK_BYTES,0);	break;
			case 'C': AES_PutBytes(t->f,"CT" ,t->ct	     ,BLOCK_BYTES,0);	break;
			case 'v': AES_PutBytes(t->f,"IV" ,t->ci.IV   ,BLOCK_BYTES,0);	break;
			case 'V': AES_PutBytes(t->f,"IV" ,t->ci.iv32 ,BLOCK_BYTES,1);	break;
			case 'K': AES_PutBytes(t->f,"KEY",t->ki.key32,t->keySize/8,1);	break;
			default:  s[0]=*fmt; s[1]=0; AES_FileIO(t->f,s,0);				break;
			}
	}

static void AES_EndSection(testData *t)
{
	AES_FileIO(t->f,"==========\n\n",0);
}

static void AES_Close(testData *t)
{
	fclose(t->f);
}

/*
+*****************************************************************************
*
* Function Name:	AES_Test_VK
*
* Function:			Run variable key test
*
* Arguments:		fname	=	file name to produce
*
* Return:			None.
*
* Notes:
*
-****************************************************************************/
static void AES_Test_VK(const char *fname)
	{
	testData t;

	memset(t.ki.keyMaterial,'0',MAX_KEY_SIZE);

	t.f=AES_PutFileHeader(fname,
			  "Electronic Codebook (ECB) Mode\nVariable Key Known Answer Tests");

	if (cipherInit(&t.ci,MODE_ECB,NULL) != true)
		FatalError("cipherInit error during %s test",fname);

	for (t.keySize=KEY_BITS_0;t.keySize<=MAX_KEY_BITS;t.keySize+=STEP_KEY_BITS)
		{
		ClearTestData(&t);
		AES_printf(&t,"S\nP\n");	/* output key size, plaintext */
		for (t.I=1;t.I<=t.keySize;t.I++)
			{
			t.ki.keyMaterial[(t.I-1)/4]='0' + (8 >> ((t.I-1) & 3));
			if (makeKey(&t.ki,DIR_ENCRYPT,t.keySize,t.ki.keyMaterial) != true)
				FatalError("Error parsing key during %s test",fname);
			if (twofish_encrypt(&t.ci,&t.ki,t.pt,1,t.ct))
				FatalError("twofish_encrypt return during %s test",fname);
			AES_printf(&t,"IKC\n");	/* output I, KEY, CT, newline */

			t.ki.keyMaterial[(t.I-1)/4]='0';	/* rezero the key bit */
			}
		AES_EndSection(&t);
		}

	AES_Close(&t);
	}

/*
+*****************************************************************************
*
* Function Name:	AES_Test_VT
*
* Function:			Run variable text test
*
* Arguments:		fname	=	file name to produce
*
* Return:			None.
*
* Notes:
*
-****************************************************************************/
static void AES_Test_VT(const char *fname)
	{
	testData t;

	memset(	t.ki.keyMaterial,'0',MAX_KEY_SIZE);

	t.f=AES_PutFileHeader(fname,
			  "Electronic Codebook (ECB) Mode\nVariable Text Known Answer Tests");

	if (cipherInit(&t.ci,MODE_ECB,NULL) != true)
		FatalError("cipherInit error during %s test",fname);

	for (t.keySize=KEY_BITS_0;t.keySize<=MAX_KEY_BITS;t.keySize+=STEP_KEY_BITS)
		{
		ClearTestData(&t);
		if (makeKey(&t.ki,DIR_ENCRYPT,t.keySize,t.ki.keyMaterial) != true)
			FatalError("Error parsing key during %s test",fname);

		AES_printf(&t,"S\nK\n");	/* output key size, key */
		for (t.I=1;t.I<=BLOCK_BITS;t.I++)
			{
			t.pt[(t.I-1)/8] = 0x80 >> ((t.I-1) & 7);
			if (twofish_encrypt(&t.ci,&t.ki,t.pt,1,t.ct))
				FatalError("twofish_encrypt return during %s test",fname);
			AES_printf(&t,"IPC\n");	/* output I, PT, CT, newline */
			t.pt[(t.I-1)/8] = 0;
			}
		AES_EndSection(&t);
		}
	AES_Close(&t);
	}

/*
+*****************************************************************************
*
* Function Name:	AES_Test_TBL
*
* Function:			Run tabl test
*
* Arguments:		fname	=	file name to produce
*
* Return:			None.
*
* Notes:
*
-****************************************************************************/
static void AES_Test_TBL(const char *fname)
	{
	int i;
	testData t;

	t.f=AES_PutFileHeader(fname,
			"Electronic Codebook (ECB) Mode\nTables Known Answer Test\n"
			"Tests permutation tables and MDS matrix multiply tables.");

	for (t.keySize=KEY_BITS_0;t.keySize <= MAX_KEY_BITS;t.keySize+=STEP_KEY_BITS)
		{
		AES_printf(&t,"S\n");	/* output key size */
		TableOp(TAB_ENABLE);
		TableOp(TAB_RESET);

		ClearTestData(&t);
		if (cipherInit(&t.ci,MODE_ECB,NULL) != true)
			FatalError("Error cipherInit() during %s test",fname);

		for (t.I=1;TableOp(TAB_QUERY) == false;t.I++)
			{
			if (makeKey(&t.ki,DIR_ENCRYPT,t.keySize,t.ki.keyMaterial) != true)
				FatalError("Error parsing key during %s test",fname);
			if (twofish_encrypt(&t.ci,&t.ki,t.pt,1,t.ct))
				FatalError("twofish_encrypt during %s test",fname);
			AES_printf(&t,"IKPC\n");		/* show the 'vector' */
			memcpy(t.ki.keyMaterial+MAX_KEY_SIZE/2,t.ki.keyMaterial,MAX_KEY_SIZE/2);
			for (i=0;i<MAX_KEY_SIZE/2;i+=2)	/* make new key from old paintext */
				{
				t.ki.keyMaterial[i  ]=hexTab[t.pt[i/2] >> 4];
				t.ki.keyMaterial[i+1]=hexTab[t.pt[i/2] &0xF];
				}
			memcpy(t.pt,t.ct,BLOCK_BYTES);	/* use ciphertext as new plaintext */
			}
		TableOp(TAB_DISABLE);
		AES_EndSection(&t);		/* output separator */
		if (!quietVerify) printf("  [%d,%3d]",t.keySize,t.I);
		}
	if (!quietVerify) printf("\n");
	AES_Close(&t);
	}

/*
+*****************************************************************************
*
* Function Name:	AES_Test_ECB_E_MCT
*
* Function:			Run ECB Monte Carlo test for ECB encryption
*
* Arguments:		fname	=	file name to produce
*
* Return:			None.
*
* Notes:
*
-****************************************************************************/
static void AES_Test_ECB_E_MCT(const char *fname)
	{
	int i,j,q;
	testData t;

	t.f=AES_PutFileHeader(fname,
				"Electronic Codebook (ECB) Mode - ENCRYPTION\nMonte Carlo Test");

	if (cipherInit(&t.ci,MODE_ECB,NULL) != true)
		FatalError("cipherInit error during %s test",fname);

	for (t.keySize=KEY_BITS_0,q=0;t.keySize<=MAX_KEY_BITS;t.keySize+=STEP_KEY_BITS,q+=2)
		{
		AES_printf(&t,"S\n");			/* output key size */
		if (!quietVerify) printf("  keyLen = %3d. ",t.keySize);

		ClearTestData(&t);				/* start with all zeroes */
		if (makeKey(&t.ki,DIR_ENCRYPT,t.keySize,t.ki.keyMaterial) != true)
			FatalError("Error parsing key during %s test",fname);

		for (t.I=0;t.I<mctOuter;t.I++)
			{
			AES_printf(&t,"IKP");
			if (!quietVerify) printf("%3d\b\b\b",t.I);
			for (j=0;j<mctInner;j++)
				{
				if (twofish_encrypt(&t.ci,&t.ki,t.pt,1,t.ct))
					FatalError("twofish_encrypt return during %s test",fname);
				if (j == mctInner-1)	/* xor the key for next outer loop */
					for (i=0;i<t.keySize/32;i++)
						t.ki.key32[i] ^=
							htole32((i>=q) ? ((uint32 *)t.ct)[i-q] :
										   ((uint32 *)t.pt)[BLOCK_BITS/32-q+i]);
				block_copy(t.pt,t.ct);
				}
			AES_printf(&t,"C\n");
			twofish_rekey(&t.ki);
			}
		AES_EndSection(&t);
		}
	if (!quietVerify) printf("   \n");
	AES_Close(&t);
	}

/*
+*****************************************************************************
*
* Function Name:	AES_Test_ECB_D_MCT
*
* Function:			Run ECB Monte Carlo test for ECB decryption
*
* Arguments:		fname	=	file name to produce
*
* Return:			None.
*
* Notes:
*
-****************************************************************************/
static void AES_Test_ECB_D_MCT(const char *fname)
	{
	int i,j,q;
	testData t;

	t.f=AES_PutFileHeader(fname,
				"Electronic Codebook (ECB) Mode - DECRYPTION\nMonte Carlo Test");

	if (cipherInit(&t.ci,MODE_ECB,NULL) != true)
		FatalError("cipherInit error during %s test",fname);

	for (t.keySize=KEY_BITS_0,q=0;t.keySize <= MAX_KEY_BITS;t.keySize+=STEP_KEY_BITS,q+=2)
		{
		AES_printf(&t,"S\n");			/* output key size */
		if (!quietVerify) printf("  keyLen = %3d. ",t.keySize);

		ClearTestData(&t);				/* start with all zeroes */
		if (makeKey(&t.ki,DIR_DECRYPT,t.keySize,t.ki.keyMaterial) != true)
			FatalError("Error parsing key during %s test",fname);

		for (t.I=0;t.I<mctOuter;t.I++)
			{
			AES_printf(&t,"IKC");
			if (!quietVerify) printf("%3d\b\b\b",t.I);
			for (j=0;j<mctInner;j++)
				{
				if (twofish_decrypt(&t.ci,&t.ki,t.ct,1,t.pt))
					FatalError("twofish_decrypt return during %s test",fname);
				if (j == mctInner-1)	/* xor the key for next outer loop */
					for (i=0;i<t.keySize/32;i++)
						t.ki.key32[i] ^=
							htole32((i>=q) ? ((uint32 *)t.pt)[i-q] :
									       ((uint32 *)t.ct)[BLOCK_BITS/32-q+i]);
				block_copy(t.ct,t.pt);
				}
			AES_printf(&t,"P\n");
			twofish_rekey(&t.ki);
			}
		AES_EndSection(&t);
		}
	if (!quietVerify) printf("   \n");
	AES_Close(&t);
	}

/*
+*****************************************************************************
*
* Function Name:	AES_Test_CBC_E_MCT
*
* Function:			Run ECB Monte Carlo test for CBC encryption
*
* Arguments:		fname	=	file name to produce
*
* Return:			None.
*
* Notes:
*
-****************************************************************************/
static void AES_Test_CBC_E_MCT(const char *fname)
	{
	int i,j,q;
	testData t;
	uint8 ctPrev[BLOCK_BYTES];
	uint8 IV[BLOCK_BYTES];
#define	CV	t.ci.IV						/* use t.ci.IV as CV */

	t.f=AES_PutFileHeader(fname,
			"Cipher Block Chaining (CBC) Mode - ENCRYPTION\nMonte Carlo Test");

	if (cipherInit(&t.ci,MODE_ECB,NULL) != true)
		FatalError("cipherInit error during %s test",fname);

	for (t.keySize=KEY_BITS_0,q=0;t.keySize<=MAX_KEY_BITS;t.keySize+=STEP_KEY_BITS,q+=2)
		{
		AES_printf(&t,"S\n");			/* output key size */
		if (!quietVerify) printf("  keyLen = %3d. ",t.keySize);

		ClearTestData(&t);				/* start with all zeroes */
		memset(IV,0,sizeof(IV));
		if (makeKey(&t.ki,DIR_ENCRYPT,t.keySize,t.ki.keyMaterial)	 != true)
			FatalError("Error parsing key during %s test",fname);

		block_copy(CV,IV);				
		for (t.I=0;t.I<mctOuter;t.I++)
			{
			AES_printf(&t,"IKvP");
			if (!quietVerify) printf("%3d\b\b\b",t.I);
			for (j=0;j<mctInner;j++)
				{
				for (i=0;i<BLOCK_BYTES;i++)
					t.pt[i]  ^= CV[i];		/* IB = PT ^ CV */
				block_copy(ctPrev,t.ct);		/* save previous ct */

				if (twofish_encrypt(&t.ci,&t.ki,t.pt,1,t.ct))
					FatalError("twofish_encrypt return during %s test",fname);
				block_copy(t.pt,(j)? ctPrev : CV);
				block_copy(CV,t.ct);
				}
			AES_printf(&t,"C\n");

			for (i=0;i<t.keySize/32;i++)
				t.ki.key32[i] ^=
					htole32((i>=q) ? ((uint32 *)t.ct  )[i-q] :
								   ((uint32 *)ctPrev)[BLOCK_BITS/32-q+i]);
			block_copy(t.pt,ctPrev);
			block_copy(CV,t.ct);

			twofish_rekey(&t.ki);
			}
		AES_EndSection(&t);
		}
	if (!quietVerify) printf("   \n");
	AES_Close(&t);
	}

/*
+*****************************************************************************
*
* Function Name:	AES_Test_CBC_D_MCT
*
* Function:			Run ECB Monte Carlo test for CBC decryption
*
* Arguments:		fname	=	file name to produce
*
* Return:			None.
*
* Notes:
*
-****************************************************************************/
static void AES_Test_CBC_D_MCT(const char *fname)
	{
	int i,j,q;
	testData t;
	uint8 ptPrev[BLOCK_BYTES];
	uint8 IV[BLOCK_BYTES];
#define	CV	t.ci.IV						/* use t.ci.IV as CV */

	t.f=AES_PutFileHeader(fname,
			"Cipher Block Chaining (CBC) Mode - DECRYPTION\nMonte Carlo Test");

	if (cipherInit(&t.ci,MODE_ECB,NULL) != true)
		FatalError("cipherInit error during %s test",fname);

	for (t.keySize=KEY_BITS_0,q=0;t.keySize <= MAX_KEY_BITS;t.keySize+=STEP_KEY_BITS,q+=2)
		{
		AES_printf(&t,"S\n");			/* output key size */
		if (!quietVerify) printf("  keyLen = %3d. ",t.keySize);

		ClearTestData(&t);				/* start with all zeroes */
		memset(IV,0,sizeof(IV));
		if (makeKey(&t.ki,DIR_DECRYPT,t.keySize,t.ki.keyMaterial)	 != true)
			FatalError("Error parsing key during %s test",fname);

		block_copy(CV,IV);				
		for (t.I=0;t.I<mctOuter;t.I++)
			{
			AES_printf(&t,"IKvC");
			if (!quietVerify) printf("%3d\b\b\b",t.I);
			for (j=0;j<mctInner;j++)
				{
				block_copy(ptPrev,t.pt);
				if (twofish_decrypt(&t.ci,&t.ki,t.ct,1,t.pt))
					FatalError("twofish_decrypt return during %s test",fname);
				for (i=0;i<BLOCK_BYTES;i++)
					t.pt[i]  ^= CV[i];		/* PT = OB ^ CV */
				block_copy(CV,t.ct);			/* CV = CT */
				block_copy(t.ct,t.pt);		/* CT = PT */
				}
			AES_printf(&t,"P\n");

			for (i=0;i<t.keySize/32;i++)
				t.ki.key32[i] ^=
					htole32((i>=q) ? ((uint32 *)t.pt  )[i-q] :
								   ((uint32 *)ptPrev)[BLOCK_BITS/32-q+i]);
			twofish_rekey(&t.ki);
			}
		AES_EndSection(&t);
		}
	if (!quietVerify) printf("   \n");
	AES_Close(&t);
	}

/*
+*****************************************************************************
*
* Function Name:	ParseArgFile
*
* Function:			Parse commands from argument file
*
* Arguments:		fName		=	name of file to read
*					argList		=	list of ptrs to fill in
*					maxArgCnt	=	size of argList
*
* Return:			None.
*
* Notes:	'/' and ';' are comment to end of line characters in the file
*			This function is used to allow a "custom" set of switches to
*			be automatically read from a file at startup.
*
-****************************************************************************/
static int ParseArgFile(const char *fName,char *argList[],int maxArgCnt)
	{
	static char buf[1024];
	static int  bufCnt=0;		/* current # chars in buf */

	int i,j,k,argCnt;
	char line[256];
	FILE *f=fopen(fName,"rt");

	if (f == NULL) return 0;

	for (argCnt=0;argCnt<maxArgCnt;)
		{	/* read in arg file one line at a time */
		memset(line,0,sizeof(line));
		if (fgets(line,sizeof(line)-4,f) == NULL)
			break;
		for (i=0;line[i];i++)	/* ignore comments to end of line */
			if ((line[i]=='/') || (line[i]==';'))
				{ line[i]=line[i+1]=0; break; }
		for (i=0;line[i];)		/* parse out tokens */
			{
			for (j=i;line[j];j++)	/* skip leading whitespace */
				if (line[j] > ' ') break;
			if (line[j]==0) break;
			for (k=j;line[k];k++)
				if (line[k] <= ' ') break;
			/* now j..k-1 defines a token */
			if (k-j+1 > (int)(sizeof(buf) - bufCnt))
				FatalError("Arg file too large: %s",line);
			if (argCnt >= maxArgCnt)
				break;
			memcpy(buf+bufCnt,line+j,k-j);
			buf[bufCnt+k-j]=0;	/* terminate the token */
			argList[argCnt++]=buf+bufCnt;
			bufCnt+=k-j+1;
			i=k;				/* skip to next token */
			}
		}
	fclose(f);
	return argCnt;
	}

/*
+*****************************************************************************
*
* Function Name:	GiveHelp
*
* Function:			Print out list of command line switches
*
* Arguments:		None.
*
* Return:			None.
*
* Notes:
*
-****************************************************************************/
static void GiveHelp(void)
	{
	printf("Syntax:   TST2FISH [options]\n"
		   "Purpose:  Generate/validate AES Twofish code and files\n"
		   "Options:  -lNN    ==> set sanity check loop to NN\n"
		   "          -pPath  ==> set file path\n"
		   "          -s      ==> set initial random seed based on time\n"
		   "          -sNN    ==> set initial random seed to NN\n"
		   "          -tNN    ==> time performance using NN iterations\n"
		   "          -v      ==> validate files, don't generate them\n"
		  );
	exit(1);
	}

#ifdef TEST_EXTERN
void	Test_Extern(void);
#endif

static void ShowHex(FILE *f,const void *p,int bCnt,const char *name)
	{
	int i;

	fprintf(f,"    ;%s:",name);
	for (i=0;i<bCnt;i++)
		{
		if ((i % 8) == 0)
			fprintf(f,"\n\t.byte\t");
		else
			fprintf(f,",");
		fprintf(f,"0%02Xh",((uint8 *)p)[i]);
		}
	fprintf(f,"\n");
	}

/* output a formatted 6805 test vector include file */
static void Debug6805(void)
	{
	int i,j;
	testData t;
	FILE *f;

	ClearTestData(&t);
	t.keySize=128;
	
	f=stdout;
	cipherInit(&t.ci,MODE_ECB,NULL);
	makeKey(&t.ki,DIR_ENCRYPT,t.keySize,t.ki.keyMaterial);

	for (i=0;i<4;i++)	/* make sure it all fits in 256 bytes */
		{
		twofish_rekey(&t.ki);
		twofish_encrypt(&t.ci,&t.ki,t.pt,1,t.ct);
		fprintf(f,"; Twofish vector #%d\n",i+1);
		ShowHex(f,&t.keySize,1,"Key Size");
		ShowHex(f,t.ki.key32,16,"Key");
		ShowHex(f,t.pt,BLOCK_BYTES,"Plaintext");
		ShowHex(f,t.ct,BLOCK_BYTES,"Ciphertext");
		for (j=0;j<16;j++)
			((uint8 *)t.ki.key32)[j] = t.pt[j] ^ t.ct[j];
		memcpy(t.pt,t.ct,sizeof(t.pt));
		fprintf(f,";-------------------------------------------------------\n");
		}
	fprintf(f,"\n\t.byte 0\t;end of list\n");
	fclose(f);
	}

/*
+*****************************************************************************
*	Main AES test function
-****************************************************************************/
int main(int argc,char *argv[])
	{
#define	MAX_ARGS	40
	int i,j,argCnt,testCnt=32;
	int doTableTest=1;
	uint32 randSeed=0x12345678;
	char *argList[MAX_ARGS];

#if ((MCT_INNER != 10000) || (MCT_OUTER != 400))
#error  MCT loop counts incorrect!
#endif

	argCnt=ParseArgFile("TST2FISH.CFG",argList,MAX_ARGS);	/* read parameter file */
	for (i=1;(i<argc) && (argCnt<MAX_ARGS);i++)				/* append command line */
		argList[argCnt++]=argv[i];

	for (i=0;i<argCnt;i++)	/* parse command line arguments */
		{
		if (argList[i][0] == '-')
			switch (toupper((uint8) argList[i][1]))
				{
				case 'F':
					switch (toupper((uint8) argList[i][2]))
						{
						case 'L':	FMT_LOG		=	1;
									testCnt		=	0;	break;
						case 'B':	CLKS_BYTE	=	~CLKS_BYTE;	break;
						}
					break;
				case '?':
				case 'H':
					GiveHelp();
					break;	
				case 'A':
					if (argList[i][2])
						useAsm=atoi(argList[i]+2);
					else
						useAsm=7;		/* enable everything in ASM */
					break;
				case 'L':
					testCnt = atoi(argList[i]+2);
					break;
				case 'P':
					for (j=0;j<sizeof(filePath)-4;j++)
						if ((filePath[j]=argList[i][j+2]) == 0)
							break;
					filePath[j]=filePath[j+1]=0;
#ifdef _M_IX86	/* DOS/Win specific filePath stuff */
					if ((j) && (filePath[j-1] != ':') && (filePath[j-1] != '\\'))
						filePath[j]='\\';	/* append backslash to filePath */
#endif
					break;
				case 'S':
					if (argList[i][2])
						randSeed = atol(argList[i]+2);
					else
						randSeed=(uint32) time(NULL);	/* randomize */
					break;
				case 'T':
					if (argList[i][2])
						timeIterCnt = atoi(argList[i]+2);
					else
						timeIterCnt = 32;
					break;
				case 'V':
					verify=1;	/* don't generate files.  Read&verify them */
					if (argList[i][2]=='+')
						verbose=1;
					if (argList[i][2]=='-')
						doTableTest=0;
					if (toupper((uint8) argList[i][2])=='Q')
						quietVerify=1;
					break;
				case '6':
					Debug6805();
					exit(1);
					break;
				}
		else
			GiveHelp();
		}


	printf("%zu.%s, %s.\n", 8*sizeof(long),
			(sizeof(int) == 2) ? "x86" : "CPU???",
#if __BYTE_ORDER == __LITTLE_ENDIAN
		   "little-endian"
#else
		   "big-endian"
#endif
		  );

	SetRand(randSeed);					/* init pseudorandom generator for testing */

	if (testCnt)
		AES_Sanity_Check(testCnt);		/* test API compliance, self-consistency */

	if ((timeIterCnt) && (!verify))
		{
		TimeOps(timeIterCnt);
		exit(0);
		}

#ifdef TEST_EXTERN
	Test_Extern();
	exit(0);
#endif

	AES_Test_VK("ecb_vk.txt");			/* Variable key  KAT */
	AES_Test_VT("ecb_vt.txt");			/* Variable text KAT */

	if (!quietVerify)
	printf("%s MCT Generation : %d,%d.\n",
		   ((MCT_INNER == mctInner) && (MCT_OUTER == mctOuter)) ? "Full" : " *** Partial",
		   mctOuter,mctInner);
	AES_Test_CBC_E_MCT("cbc_e_m.txt");	/* Monte Carlo test for CBC encryption */
	AES_Test_CBC_D_MCT("cbc_d_m.txt");	/* Monte Carlo test for CBC decryption */
	AES_Test_ECB_E_MCT("ecb_e_m.txt");	/* Monte Carlo test for ECB encryption */
	AES_Test_ECB_D_MCT("ecb_d_m.txt");	/* Monte Carlo test for ECB decryption */

	if (doTableTest)
		AES_Test_TBL("ecb_tbl.txt");		/* Table test */
	else
		if (!quietVerify) printf("WARNING: Skipping ecb_tbl.txt verification\n");

	if (verify)
		printf("*** All files verified OK ***\n");
	
	if (timeIterCnt)
		TimeOps(timeIterCnt);

	return 0;
	}
