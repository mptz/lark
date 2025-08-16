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

#include <util/huid.h>
#include <util/huidrand.h>
#include <util/memutil.h>

#include "env.h"
#include "library.h"
#include "sourcefile.h"
#include "repl.h"
#include "stmt.h"

#include "mlc.tab.h"
#include "mlc.lex.h"

symbol_mt repl_lib_id;
static symbol_mt repl_file_id;

void repl_init(symbol_mt section)
{
	/* random ID for REPL library */
	char huid [HUID_STR];
	huid_fresh_str(huid, sizeof huid);
	repl_lib_id = symtab_intern(huid);
	repl_file_id = symtab_intern("<REPL>");

	struct sourcefile *sf = xmalloc(sizeof *sf);
	sourcefile_init(sf, repl_lib_id, repl_file_id);
	sourcefile_begin(sf, repl_lib_id);
	if (section) sourcefile_add(sf, StmtRequire(section));

	library_init();
	library_queue(sf);
	library_resolve();
}

void repl_fini(void)
{
	library_fini();
}

int repl_line(const char *line, int lineno)
{
	struct sourcefile *sf = library_recycle();
	assert(sf);

	struct scanner_state scanner;
	mlc_yypstate *parser;

	mlc_scan_init_raw(&scanner);
	scanner.repl = 1;		/* XXX convoluted */
	mlc_scan_string(line, &scanner, lineno);
	parser = mlc_yypstate_new();
	assert(parser);

	int status;
	do {
		YYSTYPE val;
		YYLTYPE loc;
		int token = mlc_yylex(&val, &loc, scanner.flexstate);
		status = mlc_yypush_parse(parser, token, &val, &loc, sf);
	} while (status == YYPUSH_MORE);
	sf->bound = wordbuf_used(&sf->contents);

	mlc_yypstate_delete(parser);
	mlc_scan_fini(&scanner);

	library_queue(sf);
	library_resolve();
	return status;
}
