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

#include <assert.h>
#include <stdio.h>

#include <util/message.h>
#include <util/wordtab.h>

#include "bignum.h"
#include "vpstep.h"

#ifndef YYERROR_VERBOSE
#define YYERROR_VERBOSE
#endif

%}

%union {
	uint32_t insn;
	int_mt intx;
	nat_mt nat;
	fpw numf;
	offset offset;
	opcode opcode;
	unsigned reg;
	const char *str;	/* XXX temporary? */
	word word;
}

%{
extern int vpstep_yylex(void);
static int vpstep_yyerror(const char *s);

static void assemble(opcode opcode, unsigned reg1, unsigned reg2);
%}

/*
 * Other terminals.
 */
%token <nat> NAT
%token <intx> INT
%token <numf> FLOAT
%token <offset> OFFSET
%token <opcode> OP0 OP0O OP1 OP1C OP1N OP1O OP1S OP1W OP1Z OP2
%token <reg> REG
%token <str> STRING
%token <word> CHAR WORD

/*
 * Nonterminal type declarations.
 */

%%

insn	: OP0 { assemble($1, 0, 0); }
	| OP0O OFFSET { assemble($1, 0, 0); vpstep_arg = $2; }
	| OP1 REG { assemble($1, $2, 0); }
	| OP1C REG ',' CHAR { assemble($1, $2, 0); vpstep_arg = $4; }
	| OP1N REG ',' NAT
	| OP1O REG ',' OFFSET { assemble($1, $2, 0); vpstep_arg = $4; }
	| OP1W REG ',' WORD { assemble($1, $2, 0); vpstep_arg = $4; }
	| OP1Z REG ',' INT
	| OP2 REG ',' REG { assemble($1, $2, $4); }
	;

%%
/* Additional C code section */

insncode vpstep_insn;
word vpstep_arg;

static int
vpstep_yyerror(const char *s)
{
	fprintf(stderr, "Parse error: %s\n", s);
	return 0;
}

static void
assemble(opcode opcode, unsigned reg1, unsigned reg2)
{
	vpstep_insn = opcode | (reg1 << 16) | (reg2 << 24);
}
