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

#include "form.h"
#include "mlc.h"
#include "mlc.lex.h"
#include "mlc.tab.h"
#include "prim.h"

/*
 * This is a terrible hack required (AFACT) to compile on older versions
 * of GNU Bison (confirmed with 3.5.1) which don't include the standard
 * internal tokens like YYEOF, YYerror, and YYUNDEF in the .tab.h header
 * file.  As far as I know these tokens always have the same values.
 */
#define YYerror 256
#define YYUNDEF 257
%}

%option 8bit reentrant bison-bridge
%option warn nodefault noinput nounput noyywrap

%x comment

INT	[-+]?(0|([1-9][0-9]*))
REAL	[-+]?[0-9]+[.][0-9]+
RINT	[-+]?[0-9]+
SCI	({RINT}|{REAL})[eE]{RINT}
FLOAT	{REAL}|{SCI}
NUM	{INT}|{FLOAT}

VARIABLE	[a-zA-Z][a-zA-Z0-9-]*
SPACE		([[:space:]]{-}[\n])*

%%
\(		return '(';
\)		return ')';
\[		return '[';
\]		return ']';
\.		return '.';
\,		return ',';
\?		return '?';
\!		return '!';
\;		return ';';
_		return '_';
\|		return '|';
\+		return '+';
\-		return '-';
\*		return '*';
\/		return '/';
==		return OPEQ;
!=		return OPNE;
\<		return OPLT;
\<=		return OPLTE;
\>		return OPGT;
\>=		return OPGTE;
\:=		return ASSIGN;
\n		return END_OF_LINE;

#0		return OPCAR;
#1		return OPCDR;
#is-nil		yylval->prim = PRIM_ISNIL;	return PRIM;
#is-pair	yylval->prim = PRIM_ISPAIR;	return PRIM;

#echo		return CMD_ECHO;
#env		return ENV_DUMP;
#include	return INCLUDE;
#list		return LIST;

{NUM}		yylval->form = FormNum(atof(yytext)); return NUM;

{VARIABLE}	{ yylval->form = FormVarS(yytext); return VARIABLE; }
\"[^"]*\"	{
			char *s = strdup(yytext + 1);
			s[strlen(s) - 1] = '\0';	/* trailing quote */
			yylval->str = s;
			return STRING;
		}

"|*"		BEGIN(comment);
<comment>{
		\n|"*|"		BEGIN(INITIAL);
		"*"[^\|]	/* still in comment, no bar after asterisk */
		"*"		/* asterisk followed by end-of-file: OK */
		[^\n\*]+	/* eat comment */
}

{SPACE}		/* eat whitespace */
\\[ \t]*\n	/* eat escaped end-of-line */

.		fprintf(stderr, "Scan error: '%s'\n", yytext); return YYUNDEF;

%%

void mlc_lex_string(const char *s, mlc_yyscan_t scanner)
{
	mlc_yy_switch_to_buffer(mlc_yy_scan_string(s, scanner), scanner);
}
