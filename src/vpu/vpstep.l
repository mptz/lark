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
#include <util/symtab.h>
#include <util/word.h>

#include "bignum.h"
#include "opcode.h"

#include "vpstep.h"
#include "vpstep.tab.h"

static struct bytebuf string_buf;
%}

%option 8bit batch
%option warn noinput nounput noyywrap

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
INTN	\-{NAT}
INTP	\+{NAT}
BASE2	0|(1[01]*)
BASE16	0|([1-9A-Fa-f][0-9A-Fa-f]*)
REAL	[-+]?[0-9]+[.][0-9]+
RINT	[-+]?[0-9]+
SCI	({RINT}|{REAL})[eE]{RINT}
FLOAT	{REAL}|{SCI}

/*
 * Alphanumeric labels and register designations.
 */
REG	R[0-9A-F]

%%
	/*
	 * Punctuation.
	 */
#	return '#';
:	return ':';
,	return ',';

	/*
	 * Instructions.  Unlike with C files, flex doesn't seem to have an
	 * include mechanism for rules, so we paste oplex.l here when the
	 * operations change (not too often).
	 */
ILL  vpstep_yylval.opcode = opILL; return OP0;
NOP  vpstep_yylval.opcode = opNOP; return OP0;
HALT  vpstep_yylval.opcode = opHALT; return OP0;
BREAK  vpstep_yylval.opcode = opBREAK; return OP0;
GC  vpstep_yylval.opcode = opGC; return OP0;
TRACE  vpstep_yylval.opcode = opTRACE; return OP0;
JMP  vpstep_yylval.opcode = opJMP; return OP0O;
JEQ  vpstep_yylval.opcode = opJEQ; return OP0O;
JNE  vpstep_yylval.opcode = opJNE; return OP0O;
JLT  vpstep_yylval.opcode = opJLT; return OP0O;
JGE  vpstep_yylval.opcode = opJGE; return OP0O;
LDLc  vpstep_yylval.opcode = opLDLc; return OP1C;
LDLf  vpstep_yylval.opcode = opLDLf; return OP1W;
LDLn  vpstep_yylval.opcode = opLDLn; return OP1N;
LDLo  vpstep_yylval.opcode = opLDLo; return OP1O;
LDLs  vpstep_yylval.opcode = opLDLs; return OP1S;
LDLw  vpstep_yylval.opcode = opLDLw; return OP1W;
LDLz  vpstep_yylval.opcode = opLDLz; return OP1Z;
ZEROf  vpstep_yylval.opcode = opZEROf; return OP1;
ZEROo  vpstep_yylval.opcode = opZEROo; return OP1;
ZEROw  vpstep_yylval.opcode = opZEROw; return OP1;
JMPI  vpstep_yylval.opcode = opJMPI; return OP1O;
BEQ  vpstep_yylval.opcode = opBEQ; return OP1;
BNE  vpstep_yylval.opcode = opBNE; return OP1;
BLT  vpstep_yylval.opcode = opBLT; return OP1;
BLE  vpstep_yylval.opcode = opBLE; return OP1;
BGT  vpstep_yylval.opcode = opBGT; return OP1;
BGE  vpstep_yylval.opcode = opBGE; return OP1;
ASR  vpstep_yylval.opcode = opASR; return OP1W;
LSL  vpstep_yylval.opcode = opLSL; return OP1W;
LSR  vpstep_yylval.opcode = opLSR; return OP1W;
NOT  vpstep_yylval.opcode = opNOT; return OP1;
INCn  vpstep_yylval.opcode = opINCn; return OP1;
INCo  vpstep_yylval.opcode = opINCo; return OP1;
INCw  vpstep_yylval.opcode = opINCw; return OP1;
INCz  vpstep_yylval.opcode = opINCz; return OP1;
DECn  vpstep_yylval.opcode = opDECn; return OP1;
DECo  vpstep_yylval.opcode = opDECo; return OP1;
DECw  vpstep_yylval.opcode = opDECw; return OP1;
DECz  vpstep_yylval.opcode = opDECz; return OP1;
NEGf  vpstep_yylval.opcode = opNEGf; return OP1;
NEGo  vpstep_yylval.opcode = opNEGo; return OP1;
NEGz  vpstep_yylval.opcode = opNEGz; return OP1;
PRINTc  vpstep_yylval.opcode = opPRINTc; return OP1;
PRINTf  vpstep_yylval.opcode = opPRINTf; return OP1;
PRINTn  vpstep_yylval.opcode = opPRINTn; return OP1;
PRINTo  vpstep_yylval.opcode = opPRINTo; return OP1;
PRINTs  vpstep_yylval.opcode = opPRINTs; return OP1;
PRINTw  vpstep_yylval.opcode = opPRINTw; return OP1;
PRINTz  vpstep_yylval.opcode = opPRINTz; return OP1;
MOV  vpstep_yylval.opcode = opMOV; return OP2;
CMPc  vpstep_yylval.opcode = opCMPc; return OP2;
CMPf  vpstep_yylval.opcode = opCMPf; return OP2;
CMPn  vpstep_yylval.opcode = opCMPn; return OP2;
CMPo  vpstep_yylval.opcode = opCMPo; return OP2;
CMPs  vpstep_yylval.opcode = opCMPs; return OP2;
CMPw  vpstep_yylval.opcode = opCMPw; return OP2;
CMPz  vpstep_yylval.opcode = opCMPz; return OP2;
ANDw  vpstep_yylval.opcode = opANDw; return OP2;
ORw  vpstep_yylval.opcode = opORw; return OP2;
XORw  vpstep_yylval.opcode = opXORw; return OP2;
ADDf  vpstep_yylval.opcode = opADDf; return OP2;
ADDn  vpstep_yylval.opcode = opADDn; return OP2;
ADDo  vpstep_yylval.opcode = opADDo; return OP2;
ADDw  vpstep_yylval.opcode = opADDw; return OP2;
ADDz  vpstep_yylval.opcode = opADDz; return OP2;
SUBf  vpstep_yylval.opcode = opSUBf; return OP2;
SUBn  vpstep_yylval.opcode = opSUBn; return OP2;
SUBo  vpstep_yylval.opcode = opSUBo; return OP2;
SUBw  vpstep_yylval.opcode = opSUBw; return OP2;
SUBz  vpstep_yylval.opcode = opSUBz; return OP2;
MULf  vpstep_yylval.opcode = opMULf; return OP2;
MULn  vpstep_yylval.opcode = opMULn; return OP2;
MULo  vpstep_yylval.opcode = opMULo; return OP2;
MULw  vpstep_yylval.opcode = opMULw; return OP2;
MULz  vpstep_yylval.opcode = opMULz; return OP2;
DIVf  vpstep_yylval.opcode = opDIVf; return OP2;
DIVTn  vpstep_yylval.opcode = opDIVTn; return OP2;
DIVTo  vpstep_yylval.opcode = opDIVTo; return OP2;
DIVTw  vpstep_yylval.opcode = opDIVTw; return OP2;
DIVTz  vpstep_yylval.opcode = opDIVTz; return OP2;
REMTn  vpstep_yylval.opcode = opREMTn; return OP2;
REMTo  vpstep_yylval.opcode = opREMTo; return OP2;
REMTw  vpstep_yylval.opcode = opREMTw; return OP2;
REMTz  vpstep_yylval.opcode = opREMTz; return OP2;

	/*
	 * Arguments.
	 */
{REG}	{ vpstep_yylval.reg = isdigit(yytext[1]) ? yytext[1] - '0' :
		yytext[1] - 'A' + 10; return REG; }

	/*
	 * Character literals.
	 */
'.'	vpstep_yylval.word = yytext[1]; return CHAR;

	/*
	 * Numeric literals.  Unlike basically every other language, we
	 * distinguish between natural numbers and integers in literal
	 * syntax.  It really does matter in lots of programs...
	 */
{FLOAT}		vpstep_yylval.numf = atof(yytext); return FLOAT;
	/* {INTP}	vpstep_yylval.tm = lexint(+atol(yytext + 1)); return INT; */
	/* {INTN}	vpstep_yylval.tm = lexint(-atol(yytext + 1)); return INT; */
{NAT}	vpstep_yylval.nat = str2nat(yytext); return NAT;
#[+-]{NAT}	vpstep_yylval.offset = strtol(yytext + 1, NULL, 10); return OFFSET;
#0b[+-]{BASE2}	vpstep_yylval.offset = strtol(yytext + 3, NULL,  2); return OFFSET;
#0x[+-]{BASE16}	vpstep_yylval.offset = strtol(yytext + 3, NULL, 16); return OFFSET;
#{NAT}		vpstep_yylval.word = strtoul(yytext + 1, NULL, 10); return WORD;
#0b{BASE2}	vpstep_yylval.word = strtoul(yytext + 3, NULL,  2); return WORD;
#0x{BASE16}	vpstep_yylval.word = strtoul(yytext + 3, NULL, 16); return WORD;

	/*
	 * Erroneous forms of numeric literals, e.g. '+000'.  We don't
	 * want to scan these as consecutive numbers, which would be
	 * confusing... just indicate an error.
	 */
[+-]?0[0-9]*	fprintf(stderr, "Scan error: '%s'\n", yytext); yyterminate();
#[+-]?0[0-9]*	fprintf(stderr, "Scan error: '%s'\n", yytext); yyterminate();

	/*
	 * String literals, with backslash escaping.
	 */
\"	BEGIN(string);
<string>{
	\"	{ BEGIN(INITIAL); bytebuf_append_char(&string_buf, '\0');
		  vpstep_yylval.str = strdup((const char *) string_buf.data);
		  bytebuf_complete(&string_buf);
		  return STRING; }	/* XXX can't handle embedded NUL */
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
		  yyterminate(); }
	<<EOF>>	{ fprintf(stderr, "Scan error: EOF within quoted string\n");
		  yyterminate(); }
}

[[:space:]]*	/* ignore whitespace */
.	fprintf(stderr, "Scan error: '%s'\n", yytext); yyterminate();

%%

static YY_BUFFER_STATE vpstep_buffer;

void vpstep_init_lexer(const char *str)
{
	vpstep_buffer = yy_scan_string(str);
}

void vpstep_fini_lexer(void)
{
	yy_delete_buffer(vpstep_buffer);
}
