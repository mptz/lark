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

#include <util/bytebuf.h>
#include <util/memutil.h>

#include "form.h"
#include "mlc.h"
#include "mlc.lex.h"
#include "mlc.tab.h"
#include "prim.h"

/*
 * Flex lexers don't update locations by default, so we add a user action
 * which updates the location for every rule matched.  We set the current
 * token's first line based on the last line of the prior token which is
 * still in yylloc.
 */
#define YY_USER_ACTION do {					\
		yylloc->first_line = yylloc->last_line;		\
		yylloc->last_line = yylineno;			\
	} while (0);

/*
 * This is a terrible hack required (AFACT) to compile on older versions
 * of GNU Bison (confirmed with 3.5.1) which don't include the standard
 * internal tokens like YYEOF, YYerror, and YYUNDEF in the .tab.h header
 * file.  As far as I know these tokens always have the same values.
 */
#define YYerror 256
#define YYUNDEF 257

static const char *lexstr(struct bytebuf *buf);
static int scanerr(int lineno, const char *msg, const char *text);
%}

%option 8bit reentrant bison-bridge
%option bison-locations yylineno
%option warn nodefault noinput nounput noyywrap
%option extra-type="struct scanner_state *"

%x comment
%x string

/*
 * HUIDs. We rely on the longest-match principle to resolve lexical ambiguity;
 * the substring of a HUID up to the second '.' could be a floating-point
 * number, and the substring up to the first '.' could be an identifier
 * (or operator if we consider the '#' prefix which precedes HUIDs in the
 * lexer rules).
 */
BASE64CHAR	[A-Za-z0-9_\-]
HUID		{BASE64CHAR}{8}"."{BASE64CHAR}{8}"."{BASE64CHAR}{8}

INT	[-+]?(0|([1-9][0-9]*))
REAL	[-+]?[0-9]+[.][0-9]+
RINT	[-+]?[0-9]+
SCI	({RINT}|{REAL})[eE]{RINT}
FLOAT	{REAL}|{SCI}
NUM	{INT}|{FLOAT}

VARIABLE	[a-zA-Z][a-zA-Z0-9-]*
SPACE		[[:space:]]*

%%

		/* let no delimiter go unused */
\(		return '(';
\)		return ')';
\[		return '[';
\]		return ']';
\{		return '{';
\}		return '}';

		/* core syntactic punctuation */
\!		return '!';
\,		return ',';
\.		return '.';
\;		return ';';
\?		return '?';
\|		return '|';

		/* language features */
_		return '_';
\:=		return DEF;

		/* arithmetic operators */
\+		yylval->prim = &prim_add;	return OP2A;
\-		yylval->prim = &prim_sub;	return OP2A;
\*		yylval->prim = &prim_mult;	return OP2M;
\/		yylval->prim = &prim_div;	return OP2M;
==		yylval->prim = &prim_eq;	return OP2C;
\<\>		yylval->prim = &prim_ne;	return OP2C;
\<		yylval->prim = &prim_lt;	return OP2C;
\<=		yylval->prim = &prim_lte;	return OP2C;
\>		yylval->prim = &prim_gt;	return OP2C;
\>=		yylval->prim = &prim_gte;	return OP2C;

$and		yylval->prim = &prim_and;	return PRIM;
$or		yylval->prim = &prim_or;	return PRIM;
$xor		yylval->prim = &prim_xor;	return PRIM;
$not		yylval->prim = &prim_not;	return PRIM;

$is-integral	yylval->prim = &prim_is_integral; return PRIM;

\+\+		yylval->prim = &prim_concat;	return OP2A;

$at		yylval->prim = &prim_at;	return PRIM;
$cell		yylval->prim = &prim_cell;	return PRIM;
$fill		yylval->prim = &prim_fill;	return PRIM;
$find		yylval->prim = &prim_find;	return PRIM;
$fuse		yylval->prim = &prim_fuse;	return PRIM;
$is-cell	yylval->prim = &prim_is_cell;	return PRIM;
#		yylval->prim = &prim_nelems;	return OP1;

#0		yylval->prim = &prim_car;	return OP1;
#1		yylval->prim = &prim_cdr;	return OP1;
$is-nil		yylval->prim = &prim_is_nil;	return PRIM;
$is-pair	yylval->prim = &prim_is_pair;	return PRIM;

$undefined	yylval->prim = &prim_undefined;	return PRIM;

#echo		return CMD_ECHO;
#env		return ENV_DUMP;
#include	return INCLUDE;
#list		return LIST;

		/* keywords which can decay into variables */
let		yylval->form = FormVarS(yytext); return LET;
section		yylval->form = FormVarS(yytext); return SECTION;

		/* atomic value literals including HUIDs */
{NUM}		yylval->form = FormNum(atof(yytext)); return NUM;
#{HUID}		yylval->huid = xstrdup(yytext + 1); return HUID;

		/* string literals use a scanner state to convert escapes */
\"		BEGIN(string);
<string>{
	\"		BEGIN(INITIAL); yylval->form = FormString(lexstr(&yyextra->strbuf)); return STRING;
	\n		bytebuf_append_char(&yyextra->strbuf, '\n');
	\\0		bytebuf_append_char(&yyextra->strbuf, '\0');
	\\n		bytebuf_append_char(&yyextra->strbuf, '\n');
	\\t		bytebuf_append_char(&yyextra->strbuf, '\t');
	\\r		bytebuf_append_char(&yyextra->strbuf, '\r');
	\\b		bytebuf_append_char(&yyextra->strbuf, '\b');
	\\f		bytebuf_append_char(&yyextra->strbuf, '\f');
	\\v		bytebuf_append_char(&yyextra->strbuf, '\v');
	\\.		bytebuf_append_char(&yyextra->strbuf, yytext[1]);
	\\\n		bytebuf_append_char(&yyextra->strbuf, yytext[1]);
	[^\\\n\"]+	bytebuf_append_string(&yyextra->strbuf, yytext, yyleng);
	\\	return scanerr(yylineno, "EOF within quoted string", yytext);
	<<EOF>>	return scanerr(yylineno, "EOF within quoted string", yytext);
}

{VARIABLE}	{ yylval->form = FormVarS(yytext); return VARIABLE; }

"|*"		BEGIN(comment);
<comment>{
	\n|"*|"		BEGIN(INITIAL);
	"*"[^\|]	/* still in comment, no bar after asterisk */
	"*"		/* asterisk followed by end-of-file: OK */
	[^\n\*]+	/* eat comment */
}

{SPACE}		/* eat whitespace */
\\[ \t]*\n	/* eat escaped end-of-line */

.		return scanerr(yylineno, "unrecognized input", yytext);

%%

void mlc_scan_init(struct scanner_state *scanner)
{
	mlc_yylex_init(&scanner->flexstate);
	yyset_extra(scanner, scanner->flexstate);
	bytebuf_init(&scanner->strbuf);
}

void mlc_scan_fini(struct scanner_state *scanner)
{
	mlc_yylex_destroy(scanner->flexstate);
	bytebuf_fini(&scanner->strbuf);
}

void mlc_scan_string(const char *s, struct scanner_state *scanner)
{
	mlc_yy_switch_to_buffer(mlc_yy_scan_string(s, scanner->flexstate),
				scanner->flexstate);
}

static const char *lexstr(struct bytebuf *buf)
{
	size_t len = bytebuf_used(buf);
	char *p = xmalloc(len + 1);
	memcpy(p, buf->data, len), p[len] = '\0';
	bytebuf_complete(buf);
	return p;
}

static int scanerr(int lineno, const char *msg, const char *text)
{
	fprintf(stderr, "Scan error: %d: %s '%s', returning $undefined\n",
		lineno, msg, text);
	return YYUNDEF;
}
