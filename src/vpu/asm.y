%{
/*
 * Copyright (c) 2001-2021 Michael P. Touloumtzis.
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

#include <util/memutil.h>
#include <util/message.h>
#include <util/symtab.h>
#include <util/wordbuf.h>
#include <util/wordtab.h>

#include "asm.h"
#include "bignum.h"
#include "fixup.h"
#include "opcode.h"
#include "pstr.h"

#ifndef YYERROR_VERBOSE
#define YYERROR_VERBOSE
#endif

%}

%union {
	uint32_t insn;
	int num;
	word intx;	/* offset of int in literal pool */
	word nat;	/* offset of nat in literal pool */
	fpw numf;
	offset offset;
	opcode opcode;
	unsigned reg;
	word str;	/* offset of str in literal pool */
	symbol_mt sym;
	word word;
}

%{
extern int asm_yylex(YYSTYPE *valp, YYLTYPE *locp, void *scanner);
static int asm_yyerror(YYLTYPE *locp, void *scanner, const char *s);

static void assemble(opcode opcode, unsigned arg1, unsigned arg2);
static int backpatch(void);
static void fixout(word word);
static void floatout(fpw f);
static void label(symbol_mt sym);
static void labelref(symbol_mt sym);
static void wordout(word word);

/*
 * The assembler is currently non-reentrant but could easily be made so
 * by incorporating these into lexer parameter(s).  For library use we
 * should also add a finalizer to free code & label memory.
 */
static struct wordbuf codewords;
static struct wordbuf fixups;
static struct wordtab labels;
static struct backpatch {
	struct backpatch *next;
	word pos;
	symbol_mt sym;
} *backpatches;

%}

%define api.pure
%locations
%lex-param {void *scanner}
%parse-param {void *scanner}

/*
 * Other terminals.
 */
%token <nat> NAT
%token <intx> INT
%token <numf> FLOAT
%token <offset> OFFSET
%token <opcode> OP0R OP0RC OP0RN OP0RO OP0RS OP0RW OP0RZ
		OP1R OP1RC OP1RN OP1RO OP1RS OP1RW OP1RZ
		OP2R
		OP1F OP1FF
		OP2F
		OP1W OP1WW
		OP2W
		OP2WN
%token <reg> REG REGFD REGW
%token <str> STRING
%token <sym> LABEL
%token <word> CHAR WORD
%token EOL ERROR

/*
 * Nonterminal type declarations.
 */

%%

input	: lines { if (backpatch()) YYERROR; }
	;

lines	: %empty
	| lines line
	;

line	: labels EOL
	| labels insn EOL
	;

labels	: %empty
	| labels LABEL ':'	{ label($2); }
	;

insn	: OP0R			{ assemble($1,  0,  0); }
	| OP0RC CHAR		{ assemble($1,  0,  0); wordout($2); }
	| OP0RN NAT		{ assemble($1,  0,  0); fixout($2); }
	| OP0RO LABEL		{ assemble($1,  0,  0); labelref($2); }
	| OP0RO OFFSET		{ assemble($1,  0,  0); wordout($2); }
	| OP0RS STRING		{ assemble($1,  0,  0); fixout($2); }
	| OP0RW WORD		{ assemble($1,  0,  0); wordout($2); }
	| OP0RZ INT		{ assemble($1,  0,  0); fixout($2); }

	| OP1R REG		{ assemble($1, $2,  0); }
	| OP1RC REG ',' CHAR	{ assemble($1, $2,  0); wordout($4); }
	| OP1RN REG ',' NAT	{ assemble($1, $2,  0); fixout($4); }
	| OP1RO REG ',' LABEL	{ assemble($1, $2,  0); labelref($4); }
	| OP1RO REG ',' OFFSET	{ assemble($1, $2,  0); wordout($4); }
	| OP1RS REG ',' STRING	{ assemble($1, $2,  0); fixout($4); }
	| OP1RW REG ',' WORD	{ assemble($1, $2,  0); wordout($4); }
	| OP1RZ REG ',' INT	{ assemble($1, $2,  0); fixout($4); }
	| OP2R REG ',' REG	{ assemble($1, $2, $4); }

	| OP1F REGFD		{ assemble($1, $2,  0); }
	| OP1FF REGFD ',' FLOAT	{ assemble($1, $2,  0); floatout($4); }
	| OP2F REGFD ',' REGFD	{ assemble($1, $2, $4); }

	| OP1W REGW		{ assemble($1, $2,  0); }
	| OP1WW REGW ',' LABEL	{ assemble($1, $2,  0); labelref($4); }
	| OP1WW REGW ',' CHAR	{ assemble($1, $2,  0); wordout($4); }
	| OP1WW REGW ',' WORD	{ assemble($1, $2,  0); wordout($4); }
	| OP2W REGW ',' REGW	{ assemble($1, $2, $4); }

	| OP2WN REGW ',' REG	{ assemble($1, $2, $4); }
	;

%%
/* Additional C code section */

void
asm_init(void)
{
	wordbuf_init(&codewords);
	wordbuf_init(&fixups);
	wordtab_init(&labels, 100 /* arbitrary hint */);
	wordtab_set_oob(&labels, (void*) OFFSET_MAX);
}

const word *asm_fixupwords(void)	{ return fixups.data; }
const word *asm_insnwords(void)		{ return codewords.data; }
word asm_num_fixups(void)	{ return wordbuf_used(&fixups); }
word asm_num_insnwords(void)	{ return wordbuf_used(&codewords); }

static int
asm_yyerror(YYLTYPE *locp, void *scanner, const char *s)
{
	fprintf(stderr, "Parse error: %d:%d-%d:%d: %s\n",
		locp->first_line, locp->first_column,
		locp->last_line, locp->last_column,
		s);
	return 0;
}

static void
assemble(opcode opcode, unsigned arg1, unsigned arg2)
{
	word i = insn_code2index(opcode | (arg1 << 16) | (arg2 << 24));
	wordbuf_push(&codewords, i);
}

static int
backpatch(void)
{
	/*
	 * Note: these aren't freed at the moment.
	 */
	struct backpatch *bp;
	for (bp = backpatches; bp; bp = bp->next) {
		offset ref = (offset) wordtab_get(&labels, bp->sym);
		if (ref == OFFSET_MAX) {
			errf("no such label: %s\n", symtab_lookup(bp->sym));
			return -1;
		}
		offset off = ref - bp->pos;
		codewords.data[bp->pos] = off;
	}
	return 0;
}

static void
fixout(word word)
{
	wordbuf_push(&fixups, wordbuf_used(&codewords));
	wordbuf_push(&codewords, word);
}

static void
floatout(fpw f)
{
	union fpw_word_pun pun;
	pun.f = f;
	/* XXX this requires 64-bit words! */
	wordbuf_push(&codewords, pun.w);
}

static void
label(symbol_mt sym)
{
	word pos = wordbuf_used(&codewords);
	wordtab_put(&labels, sym, (void*) pos);
}

static void
labelref(symbol_mt sym)
{
	offset ref = (offset) wordtab_get(&labels, sym);
	if (ref == OFFSET_MAX) {
		/*
		 * Unknown label... will need to backpatch.
		 */
		struct backpatch *bp = xmalloc(sizeof *bp);
		bp->next = backpatches;
		bp->pos = wordbuf_used(&codewords);
		bp->sym = sym;
		backpatches = bp;
		wordbuf_push(&codewords, 0);
	} else {
		offset pos = wordbuf_used(&codewords),
		       off = ((word) ref) - pos;
		wordbuf_push(&codewords, off);
	}
}

static void
wordout(word word)
{
	wordbuf_push(&codewords, word);
}
