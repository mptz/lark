%{
/*
 * Copyright (c) 2009-2022 Michael P. Touloumtzis.
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
#include "parse.h"
#include "slc.lex.h"
#include "stmt.h"

#ifndef YYERROR_VERBOSE
#define YYERROR_VERBOSE
#endif
%}

%union {
	struct form *form;
	const char *str;
}

%{
extern int slc_yylex(YYSTYPE *valp, void *scanner);
static int slc_yyerror(slc_yyscan_t scanner, const char *s);
%}

%define api.pure
%define parse.error verbose
%lex-param {void *scanner}
%parse-param {slc_yyscan_t scanner}

%token ASSIGN
%token CMD_ECHO
%token END_OF_LINE
%token ENV_DUMP
%token INCLUDE

%token <str> STRING
%token <form> VARIABLE

%type <form> term body app abs base

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
	| ENV_DUMP		{ env_dump(); }
	| INCLUDE STRING	{ if (parse_include($2)) YYERROR; }
	| VARIABLE ASSIGN term	{ stmt_define($1->var.name, $3); }
	;

/*
 * Notes on lambda calculus grammars:
 *
 * Abstraction is right-recursive since by convention lambda abstractions
 * extend as far to the right as possible.  Also, we allow the lambda to
 * be omitted on nested abstractions: \x y. x y == \x. \y. x y.
 *
 * Application is left-recursive since we want to allow 'x y z' to be
 * written as an abbreviation for '(x y) z'.
 */
term	: body;
body	: app | '\\' abs	{ $$ = $2; };
abs	: '_' '.' body		{ $$ = FormAbs(symtab_intern("_"), $3); }
	| VARIABLE '.' body	{ $$ = FormAbs($1->var.name, $3); }
	| '_' abs		{ $$ = FormAbs(symtab_intern("_"), $2); };
	| VARIABLE abs		{ $$ = FormAbs($1->var.name, $2); };
app	: base
	| app base		{ $$ = FormApp($1, $2); };
base	: '(' term ')'		{ $$ = $2; }
	| VARIABLE;

%%
/* Additional C code section */

static int slc_yyerror(void *scanner, const char *s)
{
	fprintf(stderr, "Parse error: %s\n", s);
	return 0;
}
