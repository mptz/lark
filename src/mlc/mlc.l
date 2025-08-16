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

#include <assert.h>
#include <ctype.h>

#include <util/bytebuf.h>
#include <util/huid.h>
#include <util/memutil.h>
#include <util/message.h>

#include "form.h"
#include "mlc.h"
#include "prim.h"
#include "sourcefile.h"

#include "mlc.tab.h"
#include "mlc.lex.h"

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
 * Set up a start condition once we determine whether we're in listing
 * or notebook mode--we need to preserve the right mode on exiting from
 * nested start conditions like comments and quoted strings.  This isn't
 * a start condition stack, just additional state for our FSM.
 */
#define YY_USER_INIT do {					\
		yylloc->first_line = yylloc->last_line = 1;	\
		BEGIN(yyextra->repl ? sc_listing : sc_header );	\
	} while (0);

/*
 * This is a terrible hack required (AFACT) to compile on older versions
 * of GNU Bison (confirmed with 3.5.1) which don't include the standard
 * internal tokens like YYEOF, YYerror, and YYUNDEF in the .tab.h header
 * file.  As far as I know these tokens always have the same values.
 */
#define YYerror 256
#define YYUNDEF 257

static symbol_mt lexhuid(char *text);
static const char *lexstr(struct bytebuf *buf);
static int scanerr(int lineno, const char *msg, const char *text);
%}

%option 8bit reentrant bison-bridge
%option bison-locations yylineno
%option warn nodefault noinput nounput noyywrap
%option extra-type="struct scanner_state *"

/*
 * We default to listing mode (i.e. expecting lines of code) unless the
 * input begins with the notebook header sequence.  We enter greenbar
 * mode on encountering a tractor feed marker in notebook mode; greenbar
 * mode automatically reverts to notebook mode on end-of-line.  Subsidiary
 * start conditions (e.g. comments and quoted strings) transition back to
 * the start condition stored in the current mode.
 */
%s sc_listing
%x sc_notebook
%x sc_comment
%s sc_greenbar
%x sc_header
%x sc_string

/*
 * HUIDs. We rely on the longest-match principle to resolve lexical ambiguity;
 * the substring of a HUID up to the second '.' could be a floating-point
 * number, and the substring up to the first '.' could be an identifier
 * (or operator if we consider the '#' prefix which precedes HUIDs in the
 * lexer rules).
 */
BASE64CHAR	[A-Za-z0-9_\-]
HUID		{BASE64CHAR}{8}"."{BASE64CHAR}{8}"."{BASE64CHAR}{8}

INT		[-+]?(0|([1-9][0-9]*))
REAL		[-+]?[0-9]+[.][0-9]+
RINT		[-+]?[0-9]+
SCI		({RINT}|{REAL})[eE]{RINT}
FLOAT		{REAL}|{SCI}
NUM		{INT}|{FLOAT}

VARIABLE	[a-zA-Z][a-zA-Z0-9-]*
BLANK		[[:space:]]{-}[\n]
GAP		{BLANK}+
OGAP		{BLANK}*
WRAP		{BLANK}*\n[[:space:]]*
DOTS		\.{7,}
BAR		={7,}

/*
 * Markers.  These can appear within notebook mode as well as in listings.
 * To avoid accidental matches, they must appear at the beginning of the
 * line and are always a vertical bar followed by at least 7 punctuation
 * marks (i.e. the full marker beginning is 8 characters or one tab stop).
 */
SECTION		^\|{BAR}\ #{HUID}{OGAP}$
PERFORATION	^\|{DOTS}{OGAP}$
MARKER_ERROR	^\|[.=]+.*$

/*
 * Section attributes.  These can only appear in a section header.
 */
DISCARD		^\|{GAP}Discard{GAP}#{HUID}{OGAP}(\(.*\))?{OGAP}$
INSPECT		^\|{GAP}Inspect{GAP}#{HUID}{OGAP}(\(.*\))?{OGAP}$
PUBLISH		^\|{GAP}Publish{GAP}#{HUID}{OGAP}(\(.*\))?{OGAP}$
REQUIRE		^\|{GAP}Require{GAP}#{HUID}{OGAP}(\(.*\))?{OGAP}$
RETRACT		^\|{GAP}Retract{GAP}#{HUID}{OGAP}(\(.*\))?{OGAP}$

%%

<sc_listing>{
	{PERFORATION}	yyextra->mode = sc_notebook; BEGIN(sc_notebook);
}

<sc_notebook>{
	^\|:{GAP}	yyextra->mode = sc_greenbar; BEGIN(sc_greenbar);
	{PERFORATION}	yyextra->mode = sc_listing; BEGIN(sc_listing);
	[^|]*		/* ignore runs of insignificant characters */
	.|\n		/* ignore anything else */
}

<sc_listing,sc_notebook,sc_header>{
	{SECTION} {
		BEGIN(sc_header);
		yylval->sym = lexhuid(yytext);
		return SECTION;
	}
	{MARKER_ERROR} {
		errf("Scan error: Invalid marker '%s'\n", yytext);
		return YYUNDEF;
	}
}

<sc_header>{
	{DISCARD}	yylval->sym = lexhuid(yytext); return DISCARD;
	{INSPECT}	yylval->sym = lexhuid(yytext); return INSPECT;
	{PUBLISH}	yylval->sym = lexhuid(yytext); return PUBLISH;
	{REQUIRE}	yylval->sym = lexhuid(yytext); return REQUIRE;
	{RETRACT}	yylval->sym = lexhuid(yytext); return RETRACT;
	^\|\*.*$	/* ignore comment in section header */
	^{OGAP}$	BEGIN(yyextra->mode);
	\n		/* ignore other end-of-line */
	^.*$	{
		errf("Scan error: Invalid header line '%s'\n", yytext);
		return YYUNDEF;
	}
}

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

$env		return ENV_DUMP;
$symtab		return SYMTAB_DUMP;

		/* keywords which can decay into variables */
Deep		yylval->form = FormVarS(yytext); return KW_DEEP;
Def		yylval->form = FormVarS(yytext); return KW_DEF;
Echo		yylval->form = FormVarS(yytext); return KW_ECHO;
let		yylval->form = FormVarS(yytext); return KW_LET;
Lifting		yylval->form = FormVarS(yytext); return KW_LIFTING;
Literal		yylval->form = FormVarS(yytext); return KW_LITERAL;
Opaque		yylval->form = FormVarS(yytext); return KW_OPAQUE;
Surface		yylval->form = FormVarS(yytext); return KW_SURFACE;
Val		yylval->form = FormVarS(yytext); return KW_VAL;

		/* atomic value literals including HUIDs */
{NUM}		yylval->form = FormNum(atof(yytext)); return NUM;
#[[:alpha:]][[:alnum:]]*	{
	yylval->form = FormSymbol(symtab_intern(yytext + 1));
	return SYMBOL;
}
#{HUID}		{
	yylval->sym = symtab_intern(yytext + 1);
	return HUID;
}

		/* string literals use a scanner state to convert escapes */
\"		BEGIN(sc_string);
<sc_string>{
	\" {
		BEGIN(yyextra->mode);
		yylval->form = FormString(lexstr(&yyextra->strbuf));
		return STRING;
	}
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
}

{VARIABLE}	{ yylval->form = FormVarS(yytext); return VARIABLE; }

"|*"		BEGIN(sc_comment);
<sc_comment>{
	\n|"*|"		BEGIN(yyextra->mode);
	"*"[^\|]	/* still in comment, no bar after asterisk */
	"*"		/* asterisk followed by end-of-file: OK */
	[^\n\*]+	/* eat comment */
}

\\[ \t]*\n	/* eat escaped end-of-line */

<sc_greenbar>{WRAP}	BEGIN(sc_notebook);
{WRAP}			/* don't change start condition */
{OGAP}			/* eat whitespace within a line */

<*>.		return scanerr(yylineno, "unrecognized input", yytext);

<<EOF>>	{
	yypop_buffer_state(yyscanner);
	if (!YY_CURRENT_BUFFER) {
		/*
		 * This is the true EOF state in which there's no other
		 * buffer in the input buffer stack to fall back on.
		 */
		if (YY_START == sc_string) {
			const char *s = lexstr(&yyextra->strbuf);
			errf("Scan error: EOF within quoted string "
			     "beginning '%s', returning $undefined\n", s);
			xfree(s);
			return YYUNDEF;
		} else
			yyterminate();
	}
}

%%

void mlc_scan_init_raw(struct scanner_state *scanner)
{
	mlc_yylex_init(&scanner->flexstate);
	yyset_extra(scanner, scanner->flexstate);
	bytebuf_init(&scanner->strbuf);
	scanner->mode = sc_listing;
	scanner->repl = 0;
}

/*
 * Configure scanner input from a file.  Vastly complicated by the
 * need to detect notebook mode based on a token which may or may
 * not appear at the beginning of the file.
 */
void mlc_scan_init_input(struct scanner_state *scanner, FILE *input)
{
	mlc_scan_init_raw(scanner);

	/*
	 * Look at the head of the input to see if it's a notebook.
	 * If the stream could be opened but not read, we just panic.
	 * Not ideal--should either add an error return or put the
	 * scanner in a state which will cause it to error.
	 *
	 * Even though we only need to look at fewer than 20 chars
	 * for the header, we read the full first line into a buffer.
	 * Flex can't match tokens which span buffer boundaries, so
	 * this buffer must hold full-line tokens in their entirety.
	 * If the file contains only a single partial line, this is
	 * moot; it can't be a notebook since the notebook header
	 * contains a trailing '\n', and it can't match full-line
	 * tokens, again because those match through end-of-line.
	 */
	struct bytebuf line1;
	bytebuf_init(&line1);
	int c;
	while ((c = fgetc(input)) != '\n') {
		if (c == EOF) {
			assert(feof(input) || ferror(input));
			if (ferror(input)) ppanic("Scanner input");
			break;		/* incomplete first line */
		}
		bytebuf_append_char(&line1, c);
	}
	if (c == '\n') bytebuf_append_char(&line1, c);

	/*
	 * Start the scanner with the given input... this will be
	 * our main input source whether or not we push a buffer
	 * for the first line to be consumed ahead of it.
	 */
	mlc_yyrestart(input, scanner->flexstate);

	/*
	 * If we find the notebook mode header, start the scanner
	 * with just the given input file, setting its internal line
	 * counter to reflect the fact that the first line it'll see
	 * is line 2 of the file.  Otherwise we push a buffer (set to
	 * line 1; with the current options, flex doesn't appear to
	 * generate any code path to intialize the per-buffer yylineno
	 * so we have to set it up carefully here).
	 *
	 * If we don't find the notebook header, push a string buffer
	 * containing the line we already read onto the scanner.  The
	 * EOF rule will pop back to the input file at end-of-string.
	 *
	 * n.b. Flex's buffer stack API is bonkers.  Buffers can only be
	 * created explicitly using file pointers; the functions to
	 * create string buffers also have the side effect of swapping
	 * them to the current buffer.  Thus to push a string buffer
	 * we need to create a dummy file buffer, push it, start the
	 * string scan (which creates the string buffer and swaps it
	 * for the current buffer without deleting the prior value of
	 * the current buffer, i.e. the dummy), and finally delete the
	 * dummy to avoid a memory leak.
	 */
	static const char header[] = "|MLC Notebook v1|\n";
	static const size_t headlen = sizeof header - 1;
	mlc_yyset_lineno(2, scanner->flexstate);
	if (bytebuf_used(&line1) == headlen &&
	    !memcmp(header, line1.data, headlen)) {
		scanner->mode = sc_notebook;
	} else {
		YY_BUFFER_STATE dummy = mlc_yy_create_buffer(NULL,
			YY_BUF_SIZE, scanner->flexstate);
		mlc_yypush_buffer_state(dummy, scanner->flexstate);
		mlc_yy_scan_bytes((const char *) line1.data,
				  bytebuf_used(&line1),
				  scanner->flexstate);
		mlc_yy_delete_buffer(dummy, scanner->flexstate);
		mlc_yyset_lineno(1, scanner->flexstate);
	}
	bytebuf_fini(&line1);
}

void mlc_scan_fini(struct scanner_state *scanner)
{
	mlc_yylex_destroy(scanner->flexstate);
	bytebuf_fini(&scanner->strbuf);
}

void mlc_scan_string(const char *s, struct scanner_state *scanner,
		     int lineno)
{
	mlc_yy_switch_to_buffer(mlc_yy_scan_string(s, scanner->flexstate),
				scanner->flexstate);
	mlc_yyset_lineno(lineno, scanner->flexstate);
}

static symbol_mt lexhuid(char *text)
{
	char *huid = rawmemchr(text, '#');
	(++huid)[HUID_CHARS] = '\0';
	return symtab_intern(huid);
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
	/*
	 * XXX here and elsewhere we should look into the ramifications
	 * of $undefined/YYUNDEF vs YYerror.  Perhaps once error handling
	 * in the parser is further along.
	 */
	errf("Scan error: %d: %s '%s', returning $undefined\n",
		lineno, msg, text);
	return YYUNDEF;
}
