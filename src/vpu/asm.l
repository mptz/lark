%{
/*
 * Copyright (c) 2001-2019 Michael P. Touloumtzis.
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

#include <ctype.h>

#include <util/bytebuf.h>
#include <util/message.h>
#include <util/symtab.h>
#include <util/word.h>

#include "asm.h"
#include "heap.h"
#include "bignum.h"
#include "opcode.h"
#include "pool.h"

#include "asm.tab.h"

/*
 * XXX non-reentrant!
 */
static struct bytebuf string_buf;

#define YY_USER_ACTION do {						\
		yylloc->first_line = yylineno;				\
		yylloc->first_column = yycolumn;			\
	} while (0);

%}

%option 8bit batch reentrant bison-bridge
%option bison-locations yylineno
%option warn nodefault noinput nounput noyywrap

%x comment string

/*
 * Numeric formats.  One thing currently missing is the ability to enter
 * +/- NaN or Inf-valued floating-point literals.  We need a strategy
 * which avoids reserved words (it should start with a number or symbol,
 * ideally).
 *
 * XXX should floating point allow leading 0's?
 */
NAT	0|([1-9][0-9]*)
INT	[-+]{NAT}
BASE2	0|(1[01]*)
BASE16	0|([1-9A-Fa-f][0-9A-Fa-f]*)
REAL	[-+]?[0-9]+[.][0-9]+
RINT	[-+]?[0-9]+
SCI	({RINT}|{REAL})[eE]{RINT}
FLOAT	{REAL}|{SCI}

/*
 * Alphanumerics: instructions, labels, registers.  Note that INSN and
 * REG overlap so we'll need to pay attention to order below.
 */
INSN	[A-Z][A-Za-z.]*
LABEL	[a-z][A-Za-z0-9_]*
REG	R[0-9A-F]
REGFD	FD[0-7]
REGW	W[0-7]

%%
	/*
	 * Punctuation.
	 */
:	return ':';
,	return ',';

	/*
	 * Labels. XXX we should have numeric labels and very liberal
	 * textual labels to allow function names without mangling.
	 */
{LABEL}	yylval->sym = symtab_intern(yytext); return LABEL;

	/*
	 * Instructions.  Unlike with C files, flex doesn't seem to have an
	 * include mechanism for rules, so we paste oplex.l here when the
	 * operations change (not too often).
	 */
"ILL"  yylval->opcode = opILL; return OP0R;
"HALT"  yylval->opcode = opHALT; return OP0R;
"NOP"  yylval->opcode = opNOP; return OP0R;
"JI"  yylval->opcode = opJI; return OP0RO;
"BREAK"  yylval->opcode = opBREAK; return OP0R;
"GC"  yylval->opcode = opGC; return OP0R;
"PRINTrr"  yylval->opcode = opPRINTrr; return OP0R;
"TRACE"  yylval->opcode = opTRACE; return OP0R;
"LDGk"  yylval->opcode = opLDGk; return OP1R;
"LDGh"  yylval->opcode = opLDGh; return OP1R;
"LDLn"  yylval->opcode = opLDLn; return OP1RN;
"LDLs"  yylval->opcode = opLDLs; return OP1RS;
"LDLz"  yylval->opcode = opLDLz; return OP1RZ;
"LDRR"  yylval->opcode = opLDRR; return OP1R;
"INCn"  yylval->opcode = opINCn; return OP1R;
"INCz"  yylval->opcode = opINCz; return OP1R;
"DECn"  yylval->opcode = opDECn; return OP1R;
"DECz"  yylval->opcode = opDECz; return OP1R;
"POSn"  yylval->opcode = opPOSn; return OP1R;
"NEGn"  yylval->opcode = opNEGn; return OP1R;
"NEGz"  yylval->opcode = opNEGz; return OP1R;
"ABSz"  yylval->opcode = opABSz; return OP1R;
"MAGz"  yylval->opcode = opMAGz; return OP1R;
"PRINTn"  yylval->opcode = opPRINTn; return OP1R;
"PRINTs"  yylval->opcode = opPRINTs; return OP1R;
"PRINTz"  yylval->opcode = opPRINTz; return OP1R;
"MOV"  yylval->opcode = opMOV; return OP2R;
"CMPn"  yylval->opcode = opCMPn; return OP2R;
"CMPs"  yylval->opcode = opCMPs; return OP2R;
"CMPz"  yylval->opcode = opCMPz; return OP2R;
"EQn"  yylval->opcode = opEQn; return OP2R;
"EQz"  yylval->opcode = opEQz; return OP2R;
"GTEn"  yylval->opcode = opGTEn; return OP2R;
"GTEz"  yylval->opcode = opGTEz; return OP2R;
"LTn"  yylval->opcode = opLTn; return OP2R;
"LTz"  yylval->opcode = opLTz; return OP2R;
"NEn"  yylval->opcode = opNEn; return OP2R;
"NEz"  yylval->opcode = opNEz; return OP2R;
"ADDn"  yylval->opcode = opADDn; return OP2R;
"ADDz"  yylval->opcode = opADDz; return OP2R;
"SUBn"  yylval->opcode = opSUBn; return OP2R;
"SUBz"  yylval->opcode = opSUBz; return OP2R;
"MULn"  yylval->opcode = opMULn; return OP2R;
"MULz"  yylval->opcode = opMULz; return OP2R;
"DIVTn"  yylval->opcode = opDIVTn; return OP2R;
"DIVTz"  yylval->opcode = opDIVTz; return OP2R;
"REMTn"  yylval->opcode = opREMTn; return OP2R;
"REMTz"  yylval->opcode = opREMTz; return OP2R;
"LDI.fd"  yylval->opcode = opLDI_fd; return OP1FF;
"NEG.fd"  yylval->opcode = opNEG_fd; return OP1F;
"PRN.fd"  yylval->opcode = opPRN_fd; return OP1F;
"ZERO.fd"  yylval->opcode = opZERO_fd; return OP1F;
"ADD.fd"  yylval->opcode = opADD_fd; return OP2F;
"SUB.fd"  yylval->opcode = opSUB_fd; return OP2F;
"MUL.fd"  yylval->opcode = opMUL_fd; return OP2F;
"DIV.fd"  yylval->opcode = opDIV_fd; return OP2F;
"MOV.fd"  yylval->opcode = opMOV_fd; return OP2F;
"ZERO.w"  yylval->opcode = opZERO_w; return OP1W;
"ONE.w"  yylval->opcode = opONE_w; return OP1W;
"TWO.w"  yylval->opcode = opTWO_w; return OP1W;
"EQZ.w"  yylval->opcode = opEQZ_w; return OP1W;
"NEZ.w"  yylval->opcode = opNEZ_w; return OP1W;
"DEC.w"  yylval->opcode = opDEC_w; return OP1W;
"INC.w"  yylval->opcode = opINC_w; return OP1W;
"NEG.o"  yylval->opcode = opNEG_o; return OP1W;
"NOT.w"  yylval->opcode = opNOT_w; return OP1W;
"NOTL.w"  yylval->opcode = opNOTL_w; return OP1W;
"POPCT.w"  yylval->opcode = opPOPCT_w; return OP1W;
"JR.o"  yylval->opcode = opJR_o; return OP1W;
"JRD.o"  yylval->opcode = opJRD_o; return OP1W;
"LDI.w"  yylval->opcode = opLDI_w; return OP1WW;
"LDRR.w"  yylval->opcode = opLDRR_w; return OP1W;
"PRN.c"  yylval->opcode = opPRN_c; return OP1W;
"PRN.o"  yylval->opcode = opPRN_o; return OP1W;
"PRN.w"  yylval->opcode = opPRN_w; return OP1W;
"EQR.w"  yylval->opcode = opEQR_w; return OP2W;
"NER.w"  yylval->opcode = opNER_w; return OP2W;
"LTR.o"  yylval->opcode = opLTR_o; return OP2W;
"LTR.w"  yylval->opcode = opLTR_w; return OP2W;
"GTER.o"  yylval->opcode = opGTER_o; return OP2W;
"GTER.w"  yylval->opcode = opGTER_w; return OP2W;
"ADD.w"  yylval->opcode = opADD_w; return OP2W;
"SUB.w"  yylval->opcode = opSUB_w; return OP2W;
"MUL.w"  yylval->opcode = opMUL_w; return OP2W;
"DIVT.o"  yylval->opcode = opDIVT_o; return OP2W;
"DIVT.w"  yylval->opcode = opDIVT_w; return OP2W;
"REMT.o"  yylval->opcode = opREMT_o; return OP2W;
"REMT.w"  yylval->opcode = opREMT_w; return OP2W;
"ANDL.w"  yylval->opcode = opANDL_w; return OP2W;
"AND.w"  yylval->opcode = opAND_w; return OP2W;
"ASR.w"  yylval->opcode = opASR_w; return OP2W;
"LSL.w"  yylval->opcode = opLSL_w; return OP2W;
"LSR.w"  yylval->opcode = opLSR_w; return OP2W;
"ORL.w"  yylval->opcode = opORL_w; return OP2W;
"OR.w"  yylval->opcode = opOR_w; return OP2W;
"XOR.w"  yylval->opcode = opXOR_w; return OP2W;
"MOV.w"  yylval->opcode = opMOV_w; return OP2W;
"EQZ.wn"  yylval->opcode = opEQZ_wn; return OP2WN;
"NEZ.wn"  yylval->opcode = opNEZ_wn; return OP2WN;

	/*
	 * Arguments.
	 */
{REG}	{ yylval->reg = isdigit(yytext[1]) ? yytext[1] - '0' :
		yytext[1] - 'A' + 10; return REG; }
{REGFD}	{ yylval->reg = yytext[2] - '0'; return REGFD; }
{REGW}	{ yylval->reg = yytext[1] - '0'; return REGW; }

	/*
	 * Must come after register lexing.
	 */
{INSN}	{ fprintf(stderr, "Scan error: unknown instruction '%s'\n", yytext); 
	  return ERROR; }

	/*
	 * Character literals.
	 */
'.'	yylval->word = yytext[1]; return CHAR;
'\\0'	yylval->word = '\0'; return CHAR;
'\\n'	yylval->word = '\n'; return CHAR;
'\\t'	yylval->word = '\t'; return CHAR;
'\\r'	yylval->word = '\r'; return CHAR;
'\\b'	yylval->word = '\b'; return CHAR;
'\\f'	yylval->word = '\f'; return CHAR;
'\\v'	yylval->word = '\v'; return CHAR;
'\\.'	yylval->word = yytext[1]; return CHAR;

	/*
	 * Numeric literals.  Unlike basically every other language, we
	 * distinguish between natural numbers and integers in literal
	 * syntax.  It really does matter in lots of programs...
	 */
{FLOAT}		yylval->numf = atof(yytext); return FLOAT;
{INT}		yylval->intx = pool_int(str2int(yytext)); return INT;
{NAT}		yylval->nat = pool_nat(str2nat(yytext)); return NAT;
#[+-]{NAT}	yylval->offset = strtol(yytext + 1, NULL, 10); return OFFSET;
#0b[+-]{BASE2}	yylval->offset = strtol(yytext + 3, NULL,  2); return OFFSET;
#0x[+-]{BASE16}	yylval->offset = strtol(yytext + 3, NULL, 16); return OFFSET;
#{NAT}		yylval->word = strtoul(yytext + 1, NULL, 10); return WORD;
#0b{BASE2}	yylval->word = strtoul(yytext + 3, NULL,  2); return WORD;
#0x{BASE16}	yylval->word = strtoul(yytext + 3, NULL, 16); return WORD;

	/*
	 * Erroneous forms of numeric literals, e.g. '+000'.  We don't
	 * want to scan these as consecutive numbers, which would be
	 * confusing... just indicate an error.
	 */
[+-]?0[0-9]*	{ fprintf(stderr, "Scan error: improper numeric '%s'\n",
			  yytext); return ERROR; }
#[+-]?0[0-9]*	{ fprintf(stderr, "Scan error: improper numeric '%s'\n",
			  yytext); return ERROR; }

	/*
	 * One-line comments.
	 */
"|*"	BEGIN(comment);
<comment>{
	\n	BEGIN(INITIAL); return EOL;
	"*|"	BEGIN(INITIAL);
	"*"[^\|] /* asterisk followed by non-bar: still in comment */
	"*"	 /* asterisk followed by end-of-file: OK */
	[^\n\*]+ /* eat comment */
}

	/*
	 * String literals, with backslash escaping.
	 */
\"	BEGIN(string);
<string>{
	\"	{ BEGIN(INITIAL);
		  bytebuf_append_char(&string_buf, '\0');	/* XXX? */
		  yylval->str = pool_str(
			stralloc(string_buf.data,
				 bytebuf_complete(&string_buf)));
		  return STRING; }
	\n	bytebuf_append_char(&string_buf, '\n');
	\\0	bytebuf_append_char(&string_buf, '\0');
	\\n	bytebuf_append_char(&string_buf, '\n');
	\\t	bytebuf_append_char(&string_buf, '\t');
	\\r	bytebuf_append_char(&string_buf, '\r');
	\\b	bytebuf_append_char(&string_buf, '\b');
	\\f	bytebuf_append_char(&string_buf, '\f');
	\\v	bytebuf_append_char(&string_buf, '\v');
	\\.	bytebuf_append_char(&string_buf, yytext[1]);
	\\\n	bytebuf_append_char(&string_buf, yytext[1]);
	[^\\\n\"]+	bytebuf_append_string(&string_buf, yytext, yyleng);
	\\	{ fprintf(stderr, "Scan error: EOF within quoted string\n");
		  return ERROR; }
	<<EOF>>	{ fprintf(stderr, "Scan error: EOF within quoted string\n");
		  return ERROR; }
}

\n	return EOL;
[ \t]+	/* eat whitespace, XXX add others but not EOL */
\\[ \t]*\n	/* eat escaped end-of-line */

.	fprintf(stderr, "Scan error: '%s'\n", yytext); return ERROR;

%%

void
asm_init_cstr_lexer(struct lexer *lexer, const char *cstr)
{
	yyscan_t scanner;
	yylex_init(&scanner);
	YY_BUFFER_STATE buffer = yy_scan_string(cstr, scanner);
	lexer->scanner = scanner;
	lexer->buffer = buffer;
}

void
asm_init_file_lexer(struct lexer *lexer, FILE *file)
{
	yyscan_t scanner;
	yylex_init(&scanner);
	YY_BUFFER_STATE buffer = yy_create_buffer(file, YY_BUF_SIZE, scanner);
	yy_switch_to_buffer(buffer, scanner);
	lexer->scanner = scanner;
	lexer->buffer = buffer;
}

void
asm_fini_lexer(struct lexer *lexer)
{
	yy_delete_buffer(lexer->buffer, lexer->scanner);
	yylex_destroy(lexer->scanner);
}
