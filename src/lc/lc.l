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

#include <assert.h>

#include "alloc.h"
#include "lc.h"
#include "lc.lex.h"
#include "lc.tab.h"
#include "term.h"

#define YY_NO_INPUT

%}

%option bison-bridge
%option extra-type="struct allocator *"
%option nounput noyywrap
%option reentrant

SPACE	([[:space:]]{-}[\n])*

%%
\\		return '\\';
[a-z][a-z0-9-]*	{
			*yylval = allocator_push(yyextra, VarS(yytext));
			return TOKEN_VARIABLE;
		}
[A-Z][A-Z0-9-]*	{
			*yylval = allocator_push(yyextra, SymS(yytext));
			return TOKEN_SYMBOL;
		}
\"[^"]*\"	{
			char *s = strdup(yytext + 1);
			s[strlen(s) - 1] = '\0';	/* trailing quote */
			*yylval = allocator_push(yyextra, SymS(s));
			free(s);
			return TOKEN_SYMBOL;
		}
\(		return '(';
\)		return ')';
\.		return '.';
_		return '_';
\:=		return TOKEN_ASSIGN;
\n		return TOKEN_END_OF_LINE;

#echo		return TOKEN_ECHO;
#env		return TOKEN_ENV_DUMP;
#include	return TOKEN_INCLUDE;

{SPACE}		/* eat whitespace */
\\[ \t]*\n	/* eat escaped end-of-line */
;[^\n]*\n	/* eat comments */

.	{
		fprintf(stderr, "Scan error: '%s'\n", yytext);
		return TOKEN_SCAN_ERROR;
	}

%%

void lc_lex_string(const char *s, lc_yyscan_t scanner)
{
	lc_yy_switch_to_buffer(lc_yy_scan_string(s, scanner), scanner);
}
