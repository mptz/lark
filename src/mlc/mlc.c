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
#include <sys/random.h>
#include <unistd.h>

#include <util/base64.h>
#include <util/huidrand.h>
#include <util/memutil.h>
#include <util/message.h>

#include "env.h"
#include "form.h"
#include "heap.h"
#include "mlc.h"
#include "mlc.lex.h"
#include "parse.h"
#include "term.h"

int listing_setting = 0;
int quiet_setting = 0;

static void parse_line(const char *line)
{
	/*
	 * XXX we should avoid creating and destroying a scanner for
	 * each line... it's probably fast enough to not worry about,
	 * but makes multiline tokens impossible.
	 */
	struct scanner_state scanner;
	mlc_scan_init(&scanner);
	mlc_scan_string(line, &scanner);
	mlc_yyparse(scanner.flexstate);
	mlc_scan_fini(&scanner);
}

static void usage(void) __attribute__ ((noreturn));

#define NONCE_BYTES 36

static void init(void)
{
	char random_bytes [NONCE_BYTES], text_bytes [NONCE_BYTES*2];
	getrandom(random_bytes, sizeof random_bytes, 0);
	size_t n = base64_encode(text_bytes, sizeof text_bytes,
				 random_bytes, sizeof random_bytes);
	text_bytes[n] = '\0';
	const char *attestor = "mlc interpreter " __FILE__ " " __DATE__;
	huid_init(text_bytes, attestor);

	the_placeholder_symbol = symtab_intern("_");

	node_heap_init();
	env_init();
}

static void usage(void)
{
	fprintf(stderr,
	"Usage: mlc <options> [<pathname>]\n"
	"When invoked without an input filename:\n"
	"        => start an interactive REPL, if on a terminal;\n"
	"        => read from standard input, otherwise.\n"
	"Options:\n"
	"	 -d		 Debug parser\n"
	"        -e              Empty environment (don't load prelude)\n"
	"        -l <pathname>   Load the given file before entering REPL\n"
	"        -L              Verbose multi-line listings\n"
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
	while ((c = getopt(argc, argv, "del:Lq")) != -1) {
		switch (c) {
		case 'd': mlc_yydebug = 1; break;
		case 'e': use_prelude = false; break;
		case 'l': load_file = optarg; break;
		case 'L': listing_setting = 1; break;
		case 'q': quiet_setting = 1; break;
		default: usage();
		}
	}
	if (optind + 1 < argc)
		usage();

	if (use_prelude)
		parse_include("prelude.mlc");

	int result = 0;
	if (optind < argc) {
		result = parse_file(argv[optind]);

	} else if (isatty(fileno(stdin))) {
		/*
		 * Load readline history from a dotfile.
		 */
		char *histfile = NULL;
		glob_t globbuf;
		if (glob("~/.mlc_history", GLOB_TILDE, NULL, &globbuf) == 0 &&
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
