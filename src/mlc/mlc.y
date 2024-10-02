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
	const char *str;
	int prim;
}

%{
extern int mlc_yylex(YYSTYPE *valp, void *scanner);
static int mlc_yyerror(mlc_yyscan_t scanner, const char *s);
%}

%define api.pure
%define parse.error verbose
%lex-param {void *scanner}
%parse-param {mlc_yyscan_t scanner}

%token ASSIGN
%token CMD_ECHO
%token END_OF_LINE
%token ENV_DUMP
%token INCLUDE
%token LIST

%nonassoc OPEQ OPNE OPLT OPLTE OPGT OPGTE
%left '+' '-'
%left '*' '/'
%precedence OPCAR OPCDR

%token <prim> PRIM
%token <str> STRING
%token <form> NUM VARIABLE

%type <form> term terms seq expr factor arith
%type <form> app app1
%type <form> base pack abs fix nil pair params prim test var

%%

input	: %empty
	| stmt			/* for REPL, one statement w/o newline */
	| stmts;		/* for noninteractive, parse multiple stmts
				   which must each be newline-terminated */

stmts	: END_OF_LINE
	| stmt END_OF_LINE
	| stmts END_OF_LINE
	| stmts stmt END_OF_LINE
	;

stmt	: term			{ stmt_reduce($1); form_free($1); }
	| CMD_ECHO		{ putchar('\n'); }
	| CMD_ECHO STRING	{ fputs($2, stdout); putchar('\n'); }
	| ENV_DUMP		{ env_dump(NULL); }
	| ENV_DUMP STRING	{ env_dump($2); }
	| INCLUDE STRING	{ if (parse_include($2)) YYERROR; }
	| LIST term		{ stmt_list($2); }
	| VARIABLE ASSIGN term	{ stmt_define($1->var.name, $3); }
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

expr	: arith;

arith	: arith OPEQ arith	{ $$ = FormOp2(PRIM_EQ, $1, $3); }
	| arith OPNE arith	{ $$ = FormOp2(PRIM_NE, $1, $3); }
	| arith OPLT arith	{ $$ = FormOp2(PRIM_LT, $1, $3); }
	| arith OPLTE arith	{ $$ = FormOp2(PRIM_LTE, $1, $3); }
	| arith OPGT arith	{ $$ = FormOp2(PRIM_GT, $1, $3); }
	| arith OPGTE arith	{ $$ = FormOp2(PRIM_GTE, $1, $3); }
	| arith '+' arith	{ $$ = FormOp2(PRIM_ADD, $1, $3); }
	| arith '-' arith	{ $$ = FormOp2(PRIM_SUB, $1, $3); }
	| arith '*' arith	{ $$ = FormOp2(PRIM_MULT, $1, $3); }
	| arith '/' arith	{ $$ = FormOp2(PRIM_DIV, $1, $3); }
	| OPCAR arith		{ $$ = FormOp1(PRIM_CAR, $2); }
	| OPCDR arith		{ $$ = FormOp1(PRIM_CDR, $2); }
	| factor;

factor	: abs | fix | nil | pair | test | app | base;

abs	: '[' params '.' terms ']'		{ $$ = FormAbs($2, $4); };
fix	: '[' var '!' params '.' terms ']'	{ $$ = FormFix($2, $4, $6); };
params	: var | params ',' var			{ $3->prev = $1; $$ = $3; };
nil	: '[' ']'				{ $$ = FormNil(); };
pair	: '[' term '|' term ']'			{ $$ = FormPair($2, $4); };
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

base	: NUM | prim | var | pack;

prim	: PRIM			{ $$ = FormPrim($1); };
var	: VARIABLE | '_'	{ $$ = FormVar(the_placeholder_symbol); };

%%
/* Additional C code section */

static int mlc_yyerror(void *scanner, const char *s)
{
	fprintf(stderr, "Parse error: %s\n", s);
	return 0;
}
