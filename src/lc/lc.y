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
#include <sys/time.h>

#include "alloc.h"
#include "env.h"
#include "include.h"
#include "lc.h"
#include "lc.lex.h"
#include "main.h"
#include "term.h"

static int lc_yyerror(struct allocator *alloc, lc_yyscan_t scanner,
		      const char *s);
%}

%define api.pure
%define parse.error verbose
%lex-param {void *scanner}
%parse-param {struct allocator *alloc}
%parse-param {lc_yyscan_t scanner}

/*
 * Token names are verbose since they appear in parser-generated error
 * messages so are end-user-visible.
 */
%token TOKEN_ASSIGN
%token TOKEN_ECHO
%token TOKEN_END_OF_LINE
%token TOKEN_ENV_DUMP
%token TOKEN_INCLUDE
%token TOKEN_SCAN_ERROR		/* not referenced in grammar */
%token TOKEN_SYMBOL
%token TOKEN_VARIABLE

%%

input	: %empty
	| stmt input
	;

stmt	: TOKEN_END_OF_LINE
	| error TOKEN_END_OF_LINE { YYABORT; }
	| term TOKEN_END_OF_LINE { run_reduce($1); }
	| TOKEN_ECHO TOKEN_END_OF_LINE { putchar('\n'); }
	| TOKEN_ECHO TOKEN_SYMBOL TOKEN_END_OF_LINE
	{ printf("%s\n", symtab_lookup($2->sym.name)); }
	| TOKEN_ENV_DUMP TOKEN_END_OF_LINE { env_dump(); }
	| TOKEN_INCLUDE TOKEN_SYMBOL TOKEN_END_OF_LINE
	{ if (lc_include(symtab_lookup($2->sym.name))) YYERROR; }
	| TOKEN_SYMBOL TOKEN_ASSIGN term TOKEN_END_OF_LINE
	{ $1->sym.body = $3; env_install($1); }
	;

/*
 * Notes on the grammar:
 *
 * Abstraction is right-recursive since by convention lambda abstractions
 * extend as far to the right as possible.  Also, we allow the lambda to
 * be omitted on nested abstractions: \x y. x y == \x. \y. x y.
 *
 * Application is left-recursive since we want to allow 'x y z' to be
 * written as an abbreviation for '(x y) z'.
 */
term	: abs ;
abs	: app | '\\' cabs { $$ = $2; };
cabs	: '_' '.' abs
		{ $$ = allocator_push(alloc, Abs(symtab_intern("_"), $3)); }
	| TOKEN_VARIABLE '.' abs
		{ $$ = allocator_push(alloc, Abs($1->var.name, $3)); }
	| '_' cabs { $$ = allocator_push(alloc, Abs(symtab_intern("_"), $2)); };
	| TOKEN_VARIABLE cabs
		{ $$ = allocator_push(alloc, Abs($1->var.name, $2)); };
app	: base | app base { $$ = allocator_push(alloc, App($1, $2)); };
base	: '(' term ')' { $$ = $2; }
	| TOKEN_SYMBOL { $$ = env_lookup($1->sym.name); }
	| TOKEN_VARIABLE;

%%
/* Additional C code section */

static int lc_yyerror(struct allocator *alloc, void *scanner, const char *s)
{
	fprintf(stderr, "Parse error: %s\n", s);
	return 0;
}
