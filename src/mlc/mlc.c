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

#include <assert.h>
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
#include <util/wordbuf.h>

#include "env.h"
#include "heap.h"
#include "libload.h"
#include "mlc.h"
#include "repl.h"

symbol_mt the_placeholder_symbol;
symbol_mt the_undefined_symbol;

int listing_setting = 0;
int quiet_setting = 0;
int trace_unflatten = 0;

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
	the_undefined_symbol = symtab_intern("$undefined");

	node_heap_init();
	env_init();
}

static void set_trace(const char *flag)
{
	     if (!strcmp(flag, "parser"))	mlc_yydebug = 1;
	else if (!strcmp(flag, "unflatten"))	trace_unflatten = 1;
	else warnf("Ignoring invalid trace flag '%s'\n", flag);
}

static void usage(void)
{
	fprintf(stderr,
	"Usage: mlc <options> [<pathname> ...]\n"
	"When invoked without any input filenames:\n"
	"        => start an interactive REPL, if on a terminal;\n"
	"        => read from standard input, otherwise.\n"
	"Options:\n"
	"        -f              Formatted, multi-line listings\n"
	"        -l <pathname>   Load the given library "
					"(may be given more than once)\n"
	"        -q              Quieter output\n"
	"        -r <HUID>       Require section in REPL\n"
	"        -T <flag>       Trace behavior associated with flag\n"
	"                        Flags: 'parser', 'unflatten'\n"
	);
	exit(EXIT_FAILURE);
}

int main(int argc, char *const argv[])
{
	set_execname(argv[0]);
	global_message_threshold = MSGLEVEL_INFO;
	init();

	int c;
	struct wordbuf loadlibs;
	wordbuf_init(&loadlibs);
	symbol_mt require_section = 0;

	while ((c = getopt(argc, argv, "fl:qr:T:")) != -1) {
		switch (c) {
		case 'f': listing_setting = 1; break;
		case 'l': wordbuf_push(&loadlibs, (word) optarg); break;
		case 'q': quiet_setting = 1; break;
		case 'r': require_section = symtab_intern(optarg); break;
		case 'T': set_trace(optarg); break;
		default: usage();
		}
	}

	int result = 1;

	/*
	 * Load libraries specified on the command line, in the order
	 * given--any number may be loaded.
	 */
	for (size_t i = 0; i < wordbuf_used(&loadlibs); ++i) {
		if (library_load((const char *) wordbuf_at(&loadlibs, i)))
			goto done;
	}
	wordbuf_fini(&loadlibs);

	if (optind < argc) {
		/*
		 * Any files specified on the command line form a single
		 * library with random per-invocation ID.
		 */
		result = library_load_files(argc - optind, argv + optind);

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

		char prompt [30] = "1> ";
		int lineno = 1;

		repl_init(require_section);
		char *input;
		while ((input = readline(prompt))) {
			if (*input)
				add_history(input);
			repl_line(input, lineno);
			xfree(input);
			snprintf(prompt, sizeof prompt, "%d> ", ++lineno);
		}
		repl_fini();
		if (histfile) {
			write_history(histfile);
			globfree(&globbuf);
		}

	} else {
		/*
		 * Read from standard input, again as a single library
		 * with random per-invocation ID.
		 */
		char *argv [] = { "-" };
		result = library_load_files(1, argv);
	}

done:
	return result;
}
