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

#include <getopt.h>
#include <glob.h>
#include <stdio.h>
#include <readline/history.h>
#include <readline/readline.h>	/* must follow stdio.h */
#include <stdlib.h>
#include <unistd.h>

#include <util/memutil.h>
#include <util/message.h>

#include "env.h"
#include "form.h"
#include "heap.h"
#include "parse.h"
#include "slc.h"
#include "slc.lex.h"
#include "term.h"

int quiet_setting = 0;

static void parse_line(const char *line)
{
	slc_yyscan_t scanner;
	slc_yylex_init(&scanner);
	slc_lex_string(line, scanner);
	slc_yyparse(scanner);
	slc_yylex_destroy(scanner);
}

static void usage(void) __attribute__ ((noreturn));

static void init(void)
{
	node_heap_init();
	env_init();
}

static void usage(void)
{
	fprintf(stderr,
	"Usage: slc <options> [<pathname>]\n"
	"When invoked without an input filename:\n"
	"        => start an interactive REPL, if on a terminal;\n"
	"        => read from standard input, otherwise.\n"
	"Options:\n"
	"        -e              Empty environment (don't load prelude)\n"
	"        -l <pathname>   Load the given file before entering REPL\n"
	"        -q              Quieter output\n"
	);
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	set_execname(argv[0]);
	init();

	int c;
	bool use_prelude = true;
	const char *load_file = NULL;
	while ((c = getopt(argc, argv, "el:q")) != -1) {
		switch (c) {
		case 'e': use_prelude = false; break;
		case 'l': load_file = optarg; break;
		case 'q': quiet_setting = 1; break;
		default: usage();
		}
	}
	if (optind + 1 < argc)
		usage();

	if (use_prelude)
		parse_include("prelude.slc");

	int result = 0;
	if (optind < argc) {
		result = parse_file(argv[optind]);

	} else if (isatty(fileno(stdin))) {
		/*
		 * Load readline history from a dotfile.
		 */
		char *histfile = NULL;
		glob_t globbuf;
		if (glob("~/.slc_history", GLOB_TILDE, NULL, &globbuf) == 0 &&
		    globbuf.gl_pathc == 1) {
			histfile = globbuf.gl_pathv[0];
			read_history(histfile);
		}

		if (load_file)
			if (parse_file(load_file))
				goto done;

		char *input;
		while ((input = readline("> "))) {
			if (*input)
				add_history(input);
			parse_line(input);
			xfree(input);
		}
		if (histfile) {
			write_history(histfile);
			globfree(&globbuf);
		}

	} else {
		result = parse_stdin();
	}

done:
	return result;
}
