%{
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

#include <stdlib.h>

#include "env.h"
#include "form.h"
#include "mlc.lex.h"
#include "parse.h"
#include "prim.h"
#include "stmt.h"

#ifndef YYERROR_VERBOSE
#define YYERROR_VERBOSE
#endif
%}

%union {
	struct form *form;
	const struct prim *prim;
	const char *huid;
}

%{
extern int mlc_yylex(YYSTYPE *valp, YYLTYPE *locp, void *scanner);
static int mlc_yyerror(YYLTYPE *locp, mlc_yyscan_t scanner, const char *s);
%}

%define api.pure
%define parse.error verbose
%locations
%lex-param {void *scanner}
%parse-param {mlc_yyscan_t scanner}

%token CMD_ECHO
%token DEF
%token ENV_DUMP
%token INCLUDE
%token LIST

%nonassoc OP2C			/* binary comparison operators */
%left OP2A			/* binary additive operators */
%left OP2M			/* binary multiplicative operators */
%precedence OP1			/* unary operators */
%type <prim> OP2C OP2A OP2M OP1

%token <huid> HUID
%token <prim> PRIM
%token <form> NUM STRING VARIABLE
%token <form> LET SECTION

%type <form> term terms seq expr factor arith
%type <form> let assigns assign
%type <form> app app1
%type <form> base pack abs fix params
%type <form> cell elems prim string test var keyword blank

%%

input	: %empty
	| stmt			/* for REPL, one statement w/o terminator */
	| stmts;		/* for noninteractive, parse multiple stmts
				   which must each be terminated */

stmts	: '.'
	| stmt '.'
	| stmts stmt '.'
	;

stmt	: term 			{ stmt_reduce($1); form_free($1); }
	| CMD_ECHO		{ putchar('\n'); }
	| CMD_ECHO STRING	{ fputs($2->str, stdout); putchar('\n'); }
	| ENV_DUMP		{ env_dump(NULL); }
	| ENV_DUMP STRING	{ env_dump($2->str); }
	| INCLUDE STRING	{ if (parse_include($2->str)) YYERROR; }
	| LIST term 		{ stmt_list($2); }
	| SECTION HUID		{ printf("section: #%s.\n", $2); }
	| var DEF term 		{ stmt_define($1->var.name, $3); }
	;

/*
 * Any term can be the argument of an application by writing:
 *
 * args-term; fn-expr			OR
 * args-term literal-abstraction
 *
 * Square brackets [ ] serve unambiguously to demarcate abstractions.
 * Parentheses ( ) serve two roles:
 *	1) Grouping;
 *	2) Non-unary arguments, comma separated.
 * I could choose another type of punctuation like { } for one of these,
 * but since grouping is for one term, which is exactly the scenario in
 * which we don't need parentheses for arguments, I'm hoping I can make
 * it work.
 *
 * <args> [...in-place-abstraction...]
 * <args>; exp	<- exp evaluates to an abstraction
 *
 * <args>; exp1; exp2; exp3
 *    \_App_/     /     /
 *         \_App_/     /
 *              \_App_/
 */
term	: seq;
terms	: term
	| terms ',' term	{ $$ = form_splice($1, $3); };

seq	: expr
	| seq ';' expr		{ $$ = FormApp($3, $1, FORM_SYNTAX_POSTFIX); };

expr	: arith | let;
let	: LET '{' assigns '}' expr		{ $$ = FormLet($3, $5); }
	| LET '{' assigns '.' '}' expr		{ $$ = FormLet($3, $6); };
assigns	: assign | assigns '.' assign		{ $3->prev = $1; $$ = $3; };
assign	: var DEF term				{ $$ = FormDef($1, $3); };

arith	: arith OP2C arith	{ $$ = FormOp2($2, $1, $3); }
	| arith OP2A arith	{ $$ = FormOp2($2, $1, $3); }
	| arith OP2M arith	{ $$ = FormOp2($2, $1, $3); }
	| OP1 arith		{ $$ = FormOp1($1, $2); }
	| '(' OP2C ')'		{ $$ = FormPrim($2, &@2); }
	| '(' OP2A ')'		{ $$ = FormPrim($2, &@2); }
	| '(' OP2M ')'		{ $$ = FormPrim($2, &@2); }
	| '(' OP1 ')'		{ $$ = FormPrim($2, &@2); }
	| factor;

factor	: abs | fix | cell | test | app | base;

abs	: '[' params '.' terms ']'		{ $$ = FormAbs($2, $4); };
fix	: '[' var '!' params '.' terms ']'	{ $$ = FormFix($2, $4, $6); };
params	: var | params ',' var			{ $3->prev = $1; $$ = $3; };
cell	: '[' ']'				{ $$ = FormCell(NULL); }
	| '[' elems ']'				{ $$ = FormCell($2); };
elems	: term | elems '|' term			{ $3->prev = $1; $$ = $3; };

test	: '[' term '?' terms '|' terms ']'	{ $$ = FormTest($2, $4, $6); };

app	: app1 abs		{ $$ = FormApp($2, $1, FORM_SYNTAX_POSTFIX); }
	| app1 fix		{ $$ = FormApp($2, $1, FORM_SYNTAX_POSTFIX); }
	| app1 pack		{ $$ = FormApp($1, $2, FORM_SYNTAX_PREFIX); }
	| app abs		{ $$ = FormApp($2, $1, FORM_SYNTAX_POSTFIX); }
	| app fix		{ $$ = FormApp($2, $1, FORM_SYNTAX_POSTFIX); }
	| app pack		{ $$ = FormApp($1, $2, FORM_SYNTAX_PREFIX); };
app1	: abs | fix | test | base;

/*
 * Open question as to whether it makes sense to support 0-ary abstractions,
 * but for now we do, so we support empty argument lists.
 */
pack	: '(' ')'		{ $$ = NULL; }
	| '(' ',' ')'		{ $$ = NULL; }
	| '(' terms ')'		{ $$ = $2; }
	| '(' terms ',' ')'	{ $$ = $2; }

base	: NUM | string | prim | var | pack;

string	: STRING | string STRING	{ $$ = FormStringConcat($1, $2); };
prim	: PRIM			{ $$ = FormPrim($1, &@1); };
var	: VARIABLE | keyword | blank;
keyword	: LET | SECTION;
blank	: '_'			{ $$ = FormVar(the_placeholder_symbol); };

%%
/* Additional C code section */

static int mlc_yyerror(YYLTYPE *locp, void *scanner, const char *s)
{
	if (locp->first_line == locp->last_line)
		fprintf(stderr, "Parse error: %d: %s\n", locp->first_line, s);
	else
		fprintf(stderr, "Parse error: %d-%d: %s\n",
			locp->first_line, locp->last_line, s);
	return 0;
}
