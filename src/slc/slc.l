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

#include "form.h"
#include "slc.h"
#include "slc.lex.h"
#include "slc.tab.h"

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

VARIABLE	[a-zA-Z][a-zA-Z0-9-]*
SPACE		([[:space:]]{-}[\n])*

%%
\\		return '\\';
\(		return '(';
\)		return ')';
\.		return '.';
_		return '_';
\:=		return ASSIGN;
\n		return END_OF_LINE;

#echo		return CMD_ECHO;
#env		return ENV_DUMP;
#include	return INCLUDE;

{VARIABLE}	{ yylval->form = FormVarS(yytext); return VARIABLE; }
\"[^"]*\"	{
			char *s = strdup(yytext + 1);
			s[strlen(s) - 1] = '\0';	/* trailing quote */
			yylval->str = s;
			return STRING;
		}

{SPACE}		/* eat whitespace */
\\[ \t]*\n	/* eat escaped end-of-line */
;[^\n]*\n	/* eat comments */

.		fprintf(stderr, "Scan error: '%s'\n", yytext); return YYUNDEF;

%%

void slc_lex_string(const char *s, slc_yyscan_t scanner)
{
	slc_yy_switch_to_buffer(slc_yy_scan_string(s, scanner), scanner);
}
