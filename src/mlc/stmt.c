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
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

#include <util/message.h>

#include "crumble.h"
#include "env.h"
#include "form.h"
#include "heap.h"
#include "interpret.h"
#include "mlc.h"
#include "node.h"
#include "readback.h"
#include "reduce.h"
#include "resolve.h"
#include "stmt.h"
#include "term.h"
#include "uncrumble.h"

static void node_listing(const char *label, struct node *node)
{
	if (quiet_setting)
		return;

	fputs(label, stdout);
	putchar(':');
	if (listing_setting) {
		putchar('\n');
		node_list_rl(node);
	} else {
		putchar(' ');
		node_print_body(node);
		putchar('\n');
	}
}

void stmt_define(symbol_mt name, struct form *form)
{
	/*
	 * Definition currently uses resolve, which lifts the form;
	 * free variables which reference definitions in the global
	 * environment are converted to bound variables via abstraction
	 * while other free variables are left unchanged.  This ensures
	 * all values in the global environment are closed and thus can
	 * be substituted without further closing/expansion.
	 */
	struct term *body = resolve(form);
	if (!body) return;	/* error already printed */

	/*
	 * Note that this doesn't allow for recursive definitions;
	 * if the form references the defined name, it will be added
	 * to the environment as a free variable and this definition
	 * will fail.
	 */
	if (!env_define(name, body).var)
		fprintf(stderr, "Error: Name already exists: %s\n",
			symtab_lookup(name));
}

void stmt_list(struct form *form)
{
	fputs("form: ", stdout);
	form_print(form);
	putchar('\n');

	struct term *term = resolve(form);
	if (!term)
		return;
	if (!quiet_setting) {
		fputs("term: ", stdout);
		term_print(term);
		putchar('\n');
	}

	struct node *node = crumble(term);
	node_listing("crum", node->prev);
	node_free(node);

	fputs("==================================="
	      "===================================\n", stdout);
}

void stmt_reduce(struct form *form)
{
	fputs("form: ", stdout);
	form_print(form);
	putchar('\n');

	struct term *term = resolve(form);
	if (!term)
		return;
	if (!quiet_setting) {
		fputs("term: ", stdout);
		term_print(term);
		putchar('\n');
	}

	struct node *node = crumble(term);
	node_listing("crum", node);

	struct timeval t0, t;
	gettimeofday(&t0, NULL);
	node = reduce(node);
	assert(node);
	gettimeofday(&t, NULL);

	node_listing("eval", node);

	term = uncrumble(node);
	assert(term);
	node_free(node);
	node = NULL;
	if (!quiet_setting) {
		fputs("term: ", stdout);
		term_print(term);
		putchar('\n');
	}

	form = readback(term);
	fputs("norm: ", stdout);
	form_print(form);
	putchar('\n');

	interpret(term);

	long elapsed = (t.tv_sec - t0.tv_sec) * 1000000 +
		       (t.tv_usec - t0.tv_usec);
	if (!quiet_setting) {
		printf("dt: %.6fs\n", elapsed / 1000000.0);
		print_eval_stats();
		print_heap_stats();
	}
	fputs("==================================="
	      "===================================\n", stdout);
}
