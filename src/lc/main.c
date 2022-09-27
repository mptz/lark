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
#include <stdlib.h>
#include <sys/time.h>

#include <util/message.h>

#include "alloc.h"
#include "env.h"
#include "heap.h"
#include "include.h"
#include "lc.h"
#include "lc.lex.h"
#include "readback.h"
#include "reduce.h"
#include "term.h"

static bool show_elapsed_time = true,
	    show_prompt = true;

void run_reduce(struct term *term)
{
	fputs("term: ", stdout);
	term_print(term);
	putchar('\n');

	term_index(term);
	if (term->type == ERR) {
		fputs("not evaluating ill-formed term\n", stderr);
		return;
	}

	fputs("dbix: ", stdout);
	term_print_indexed(term);
	putchar('\n');

	struct timeval t0, t;
	gettimeofday(&t0, NULL);
	term = reduce(term);
	gettimeofday(&t, NULL);

	fputs("norm: ", stdout);
	term_print_indexed(term);
	putchar('\n');
	readback(term);

	if (show_elapsed_time) {
		long elapsed = (t.tv_sec - t0.tv_sec) * 1000000 +
			       (t.tv_usec - t0.tv_usec);
		printf("dt: %.6fs\n", elapsed / 1000000.0);
	}
}

static void repl(void)
{
	struct allocator alloc = { .name = "REPL allocator" };
	allocator_init(&alloc, ALLOCATOR_DEFAULT_SLOTS);

	while (1) {
		if (show_prompt) {
			fputs("> ", stdout);
			fflush(stdout);
		}

		char line [4096];
		if (!fgets(line, sizeof line, stdin))
			break;

		lc_yyscan_t scanner;
		lc_yylex_init_extra(&alloc, &scanner);
		lc_lex_string(line, scanner);
		lc_yyparse(&alloc, scanner);
		lc_yylex_destroy(scanner);
		allocator_reset(&alloc);
	}

	allocator_fini(&alloc);
}

int main(int argc, char *argv[])
{
	set_execname(argv[0]);
	heap_init();
	env_init();
	int c;
	while ((c = getopt(argc, argv, "q")) != -1) {
		switch (c) {
		case 'q':
			show_elapsed_time = false;
			show_gc = false;
			show_prompt = false;
			break;
		}
	}
	lc_include("prelude.lc");
	repl();
	return 0;
}
