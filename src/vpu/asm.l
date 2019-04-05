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
INSN	[A-Z][A-Za-z]*
LABEL	[a-z][A-Za-z0-9_]*
REG	R[0-9A-F]

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
ILL  yylval->opcode = opILL; return OP0;
NOP  yylval->opcode = opNOP; return OP0;
HALT  yylval->opcode = opHALT; return OP0;
BREAK  yylval->opcode = opBREAK; return OP0;
GC  yylval->opcode = opGC; return OP0;
TRACE  yylval->opcode = opTRACE; return OP0;
JMP  yylval->opcode = opJMP; return OP0O;
JEQ  yylval->opcode = opJEQ; return OP0O;
JNE  yylval->opcode = opJNE; return OP0O;
JLT  yylval->opcode = opJLT; return OP0O;
JGE  yylval->opcode = opJGE; return OP0O;
LDLc  yylval->opcode = opLDLc; return OP1C;
LDLf  yylval->opcode = opLDLf; return OP1W;
LDLn  yylval->opcode = opLDLn; return OP1N;
LDLo  yylval->opcode = opLDLo; return OP1O;
LDLs  yylval->opcode = opLDLs; return OP1S;
LDLw  yylval->opcode = opLDLw; return OP1W;
LDLz  yylval->opcode = opLDLz; return OP1Z;
ZEROf  yylval->opcode = opZEROf; return OP1;
ZEROo  yylval->opcode = opZEROo; return OP1;
ZEROw  yylval->opcode = opZEROw; return OP1;
JMPI  yylval->opcode = opJMPI; return OP1O;
BEQ  yylval->opcode = opBEQ; return OP1;
BNE  yylval->opcode = opBNE; return OP1;
BLT  yylval->opcode = opBLT; return OP1;
BLE  yylval->opcode = opBLE; return OP1;
BGT  yylval->opcode = opBGT; return OP1;
BGE  yylval->opcode = opBGE; return OP1;
LDRR  yylval->opcode = opLDRR; return OP1;
ASR  yylval->opcode = opASR; return OP1W;
LSL  yylval->opcode = opLSL; return OP1W;
LSR  yylval->opcode = opLSR; return OP1W;
NOT  yylval->opcode = opNOT; return OP1;
INCn  yylval->opcode = opINCn; return OP1;
INCo  yylval->opcode = opINCo; return OP1;
INCw  yylval->opcode = opINCw; return OP1;
INCz  yylval->opcode = opINCz; return OP1;
DECn  yylval->opcode = opDECn; return OP1;
DECo  yylval->opcode = opDECo; return OP1;
DECw  yylval->opcode = opDECw; return OP1;
DECz  yylval->opcode = opDECz; return OP1;
POSn  yylval->opcode = opPOSn; return OP1;
NEGf  yylval->opcode = opNEGf; return OP1;
NEGo  yylval->opcode = opNEGo; return OP1;
NEGn  yylval->opcode = opNEGn; return OP1;
NEGz  yylval->opcode = opNEGz; return OP1;
ABSz  yylval->opcode = opABSz; return OP1;
MAGz  yylval->opcode = opMAGz; return OP1;
PRINTc  yylval->opcode = opPRINTc; return OP1;
PRINTf  yylval->opcode = opPRINTf; return OP1;
PRINTn  yylval->opcode = opPRINTn; return OP1;
PRINTo  yylval->opcode = opPRINTo; return OP1;
PRINTs  yylval->opcode = opPRINTs; return OP1;
PRINTw  yylval->opcode = opPRINTw; return OP1;
PRINTz  yylval->opcode = opPRINTz; return OP1;
MOV  yylval->opcode = opMOV; return OP2;
CMPc  yylval->opcode = opCMPc; return OP2;
CMPf  yylval->opcode = opCMPf; return OP2;
CMPn  yylval->opcode = opCMPn; return OP2;
CMPo  yylval->opcode = opCMPo; return OP2;
CMPs  yylval->opcode = opCMPs; return OP2;
CMPw  yylval->opcode = opCMPw; return OP2;
CMPz  yylval->opcode = opCMPz; return OP2;
ANDw  yylval->opcode = opANDw; return OP2;
ORw  yylval->opcode = opORw; return OP2;
XORw  yylval->opcode = opXORw; return OP2;
ADDf  yylval->opcode = opADDf; return OP2;
ADDn  yylval->opcode = opADDn; return OP2;
ADDo  yylval->opcode = opADDo; return OP2;
ADDw  yylval->opcode = opADDw; return OP2;
ADDz  yylval->opcode = opADDz; return OP2;
SUBf  yylval->opcode = opSUBf; return OP2;
SUBn  yylval->opcode = opSUBn; return OP2;
SUBo  yylval->opcode = opSUBo; return OP2;
SUBw  yylval->opcode = opSUBw; return OP2;
SUBz  yylval->opcode = opSUBz; return OP2;
MULf  yylval->opcode = opMULf; return OP2;
MULn  yylval->opcode = opMULn; return OP2;
MULo  yylval->opcode = opMULo; return OP2;
MULw  yylval->opcode = opMULw; return OP2;
MULz  yylval->opcode = opMULz; return OP2;
DIVf  yylval->opcode = opDIVf; return OP2;
DIVTn  yylval->opcode = opDIVTn; return OP2;
DIVTo  yylval->opcode = opDIVTo; return OP2;
DIVTw  yylval->opcode = opDIVTw; return OP2;
DIVTz  yylval->opcode = opDIVTz; return OP2;
REMTn  yylval->opcode = opREMTn; return OP2;
REMTo  yylval->opcode = opREMTo; return OP2;
REMTw  yylval->opcode = opREMTw; return OP2;
REMTz  yylval->opcode = opREMTz; return OP2;

	/*
	 * Arguments.
	 */
{REG}	{ yylval->reg = isdigit(yytext[1]) ? yytext[1] - '0' :
		yytext[1] - 'A' + 10; return REG; }

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
