%{
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

#include <stdio.h>
#include <unistd.h>

#include <vpu/bignum.h>
#include <vpu/heap.h>

/*
 * Improved error reporting
 */
#ifndef YYERROR_VERBOSE
#define YYERROR_VERBOSE
#endif

extern int calc_yylex(void);
static int calc_yyerror(const char *s);
%}

%union {
	nat_mt n;
	int_mt z;
	int r;
}

/*
 * Token declarations
 */
%token <n> NAT
%token <z> INT
%token ERROR

%left '+' '-'
%left '*' '/' '%'
%precedence '!'
%token CMP

%type <n> nexpr
%type <z> zexpr
%type <r> cmp

%%

input	: %empty	{ fputs(  "> ", stdout); fflush(stdout); }
	| input line	{ fputs("\n> ", stdout); fflush(stdout); }
	;

line	: '\n'
	| nexpr	'\n'	{ fputs(nat2str($1), stdout); }
	| zexpr	'\n'	{ fputs(int2str($1), stdout); }
	| cmp '\n'	{ printf("%d", $1); }
	| error '\n'	{ fputs("Recovered.", stdout); }
	;

nexpr	: NAT
	| '(' nexpr ')'		{ $$ = $2; }
	| '+' zexpr		{ $$ = int_mag($2); }
	| nexpr '*' nexpr	{ $$ = nat_mul($1, $3); }
	| nexpr '/' nexpr	{ $$ = nat_divt($1, $3); }
	| nexpr '%' nexpr	{ $$ = nat_remt($1, $3); }
	| nexpr '+' nexpr	{ $$ = nat_add($1, $3); }
	| nexpr '-' nexpr	{ $$ = nat_sub($1, $3); }
	;

zexpr	: INT
	| '(' zexpr ')'		{ $$ = $2; }
	| '+' nexpr		{ $$ = nat_pos($2); }
	| '-' nexpr		{ $$ = nat_neg($2); }
	| '*' zexpr		{ $$ = int_abs($2); }
	| '!' zexpr		{ $$ = int_neg($2); }
	| zexpr '*' zexpr	{ $$ = int_mul($1, $3); }
	| zexpr '/' zexpr	{ $$ = int_divt($1, $3); }
	| zexpr '%' zexpr	{ $$ = int_remt($1, $3); }
	| zexpr '+' zexpr	{ $$ = int_add($1, $3); }
	| zexpr '-' zexpr	{ $$ = int_sub($1, $3); }
	;

cmp	: nexpr CMP nexpr	{ $$ = nat_cmp($1, $3); }
	| zexpr CMP zexpr	{ $$ = int_cmp($1, $3); }
	;

%%
/* Additional C code section */

static int calc_yyerror(const char *s)
{
	fprintf(stderr, "Parse error: %s\n", s);
	return 0;
}

int main(int argc, char *argv[])
{
	/*
	 * XXX need a way to disable GC... we don't protect numbers.
	 */
	heap_init();

	int c;
	while ((c = getopt(argc, argv, "dh")) != -1) {
		switch (c) {
		case 'd':
			calc_yydebug = 1;	/* verbose debugging */
			break;
		case 'h':
			printf("usage: calc [-dh]\n");
			return 0;
		}
	}

	if (calc_yyparse()) {
		fputs("Parse failed.\n", stderr);
		exit(EXIT_FAILURE);
	}
	return 0;
}
