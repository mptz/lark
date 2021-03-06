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

#include <vpu/bignum.h>

#include "calc.tab.h"
%}

%option noinput nounput noyywrap

NAT	[[:digit:]]+
INT	(\+|\-)[[:digit:]]+

%%

\!	return '!';
\*	return '*';
\/	return '/';
\%	return '%';
\+	return '+';
\-	return '-';
\(	return '(';
\)	return ')';
\<=\>	return CMP;

{NAT}	calc_yylval.n = str2nat(yytext); return NAT;
{INT}	calc_yylval.z = str2int(yytext); return INT;

\n		return '\n';
[ \f\r\t\v]+	/* eat other whitespace */

.	{
		fprintf(stderr, "Scan error: '%s'\n", yytext);
		return ERROR;
	}

%%
