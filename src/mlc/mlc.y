%{
/*
 * Copyright (c) 2009-2025 Michael P. Touloumtzis.
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

#include <util/message.h>
#include <util/symtab.h>

#include "binder.h"
#include "env.h"
#include "form.h"
#include "mlc.h"
#include "prim.h"
#include "sourcefile.h"
#include "stmt.h"

#ifndef YYERROR_VERBOSE
#define YYERROR_VERBOSE
#endif
%}

%union {
	struct form *form;
	const struct prim *prim;
	symbol_mt sym;
	unsigned u;
}

%{
static int mlc_yyerror(YYLTYPE *locp, struct sourcefile *sf, const char *s);
static inline struct stmt *locs(struct stmt *stmt, struct YYLTYPE *locp)
	{ stmt->line0 = locp->first_line; stmt->line1 = locp->last_line;
	  return stmt; }
%}

%define api.pure full
%define api.push-pull push
%define parse.error verbose
%locations
%parse-param {struct sourcefile *sourcefile}

%token DEF
%token ENV_DUMP SYMTAB_DUMP

%nonassoc OP2C			/* binary comparison operators */
%left OP2A			/* binary additive operators */
%left OP2M			/* binary multiplicative operators */
%precedence OP1			/* unary operators */
%type <prim> OP2C OP2A OP2M OP1

/*
 * Tokens for namespace management.  These don't need to be treated
 * as keywords which can be used as variable names, since the lexer
 * only recognizes them using more complex regular expressions.
 */
%token <sym> HUID DISCARD INSPECT PUBLISH REQUIRE RETRACT SECTION

%token <prim> PRIM
%token <form> NUM STRING SYMBOL VARIABLE

%token <form> KW_DEEP KW_DEF KW_ECHO KW_LET KW_LIFTING KW_LITERAL
%token <form> KW_OPAQUE KW_SURFACE KW_VAL

%type <u> bflag bflags vflag
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
	| marker
	| stmt '.'
	| stmts marker
	| stmts stmt '.'
	;

stmt	: term 
		{ sourcefile_add(sourcefile,
			locs(StmtVal($1, BINDING_DEEP), &@$)); }
	| KW_DEF '{' bflags '}' var DEF term
		{ sourcefile_add(sourcefile, locs(StmtDef($5, $7, $3), &@$)); }
	| KW_DEF '{' bflags ',' '}' var DEF term
		{ sourcefile_add(sourcefile, locs(StmtDef($6, $8, $3), &@$)); }
	| KW_ECHO STRING
		{ sourcefile_add(sourcefile, locs(StmtEcho($2), &@$)); }
	| ENV_DUMP		{ env_dump(NULL); }
	| ENV_DUMP STRING	{ env_dump($2->str); }
	| SYMTAB_DUMP		{ symtab_dump(); }
	| var DEF term
		{ sourcefile_add(sourcefile, locs(StmtDef($1, $3, 0), &@$)); }
	| KW_VAL '{' vflag '}' term
		{ sourcefile_add(sourcefile, locs(StmtVal($5, $3), &@$)); }
	| KW_VAL '{' vflag ',' '}' term
		{ sourcefile_add(sourcefile, locs(StmtVal($6, $3), &@$)); }
	;

bflags	: %empty		{ $$ = 0; }
	| bflag			{ $$ = $1; }
	| bflags ',' bflag	{ $$ = $1 | $3; }
	;

bflag	: KW_DEEP		{ $$ = BINDING_DEEP; }
	| KW_LIFTING		{ $$ = BINDING_LIFTING; }
	| KW_LITERAL		{ $$ = BINDING_LITERAL; }
	| KW_SURFACE		{ $$ = 0; }
	| KW_OPAQUE		{ $$ = BINDING_OPAQUE; }
	;

vflag	: %empty		{ $$ = BINDING_DEEP; }
	| KW_DEEP		{ $$ = BINDING_DEEP; }
	| KW_LITERAL		{ $$ = BINDING_LITERAL; }
	| KW_SURFACE		{ $$ = 0; }
	;

marker	: DISCARD { sourcefile_add(sourcefile, locs(StmtDiscard($1), &@$)); }
	| INSPECT { sourcefile_add(sourcefile, locs(StmtInspect($1), &@$)); }
	| PUBLISH { sourcefile_add(sourcefile, locs(StmtPublish($1), &@$)); }
	| REQUIRE { sourcefile_add(sourcefile, locs(StmtRequire($1), &@$)); }
	| RETRACT { sourcefile_add(sourcefile, locs(StmtRetract($1), &@$)); }
	| SECTION { sourcefile_add(sourcefile, locs(StmtSection($1), &@$)); }
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
let	: KW_LET '{' assigns '}' expr		{ $$ = FormLet($3, $5); }
	| KW_LET '{' assigns '.' '}' expr	{ $$ = FormLet($3, $6); };
assigns	: assign | assigns '.' assign		{ $3->prev = $1; $$ = $3; };
assign	: var DEF term				{ $$ = FormDefLocal($1, $3); };

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

abs	: '[' params '.' term ']'		{ $$ = FormAbs($2, $4); };
fix	: '[' var '!' params '.' term ']'	{ $$ = FormFix($2, $4, $6); };
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

base	: NUM | SYMBOL | string | prim | var | pack;

string	: STRING | string STRING	{ $$ = FormStringConcat($1, $2); };
prim	: PRIM			{ $$ = FormPrim($1, &@1); };
var	: VARIABLE | keyword | blank;
keyword	: KW_DEEP | KW_DEF | KW_ECHO | KW_LET | KW_LIFTING | KW_LITERAL
	| KW_OPAQUE | KW_SURFACE | KW_VAL;
blank	: '_'			{ $$ = FormVar(the_placeholder_symbol); };

%%
/* Additional C code section */

static int mlc_yyerror(YYLTYPE *locp, struct sourcefile *sf, const char *s)
{
	if (locp->first_line == locp->last_line)
		errf("Parse error: %d: %s\n", locp->first_line, s);
	else
		errf("Parse error: %d-%d: %s\n",
		     locp->first_line, locp->last_line, s);
	return 0;
}
