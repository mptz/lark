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
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

#include <util/memutil.h>
#include <util/message.h>

#include "binder.h"
#include "env.h"
#include "form.h"
#include "heap.h"
#include "flatten.h"
#include "interpret.h"
#include "library.h"
#include "mlc.h"
#include "node.h"
#include "readback.h"
#include "reduce.h"
#include "resolve.h"
#include "sourcefile.h"
#include "stmt.h"
#include "term.h"
#include "unflatten.h"

static struct stmt *stmt_alloc(enum stmt_variety variety)
{
	struct stmt *stmt = xmalloc(sizeof *stmt);
	stmt->variety = variety;
	stmt->line0 = stmt->line1 = -1;
	return stmt;
}

struct stmt *StmtDef(struct form *var, struct form *val, unsigned flags)
{
	struct stmt *stmt = stmt_alloc(STMT_DEF);
	stmt->def.var = var;
	stmt->def.val = val;
	stmt->def.flags = flags;
	return stmt;
}

struct stmt *StmtEcho(struct form *str)
{
	struct stmt *stmt = stmt_alloc(STMT_ECHO);
	assert(str->variety == FORM_STRING);
	stmt->form = str;
	return stmt;
}

struct stmt *StmtMarker(enum marker_variety variety, symbol_mt huid)
{
	assert(huid != the_empty_symbol);
	struct stmt *stmt = stmt_alloc(STMT_MARKER);
	stmt->marker.variety = variety;
	stmt->marker.huid = huid;
	return stmt;
}

struct stmt *StmtVal(struct form *val, unsigned flags)
{
	struct stmt *stmt = stmt_alloc(STMT_VAL);
	stmt->val.val = val;
	stmt->val.flags = flags;
	return stmt;
}

void stmt_free(struct stmt *stmt)
{
	switch (stmt->variety) {
	case STMT_DEF:	form_free(stmt->def.var);
			form_free(stmt->def.val);
			break;
	case STMT_ECHO: form_free(stmt->form); break;
	case STMT_VAL:	form_free(stmt->val.val); break;
	case STMT_MARKER:
			/* nada */ break;
	default: panicf("Unhandled stmt variety %d\n", stmt->variety);
	}
	stmt->variety = STMT_INVALID;	/* in case of accidental reuse */
	xfree(stmt);
}

static void form_labeled_print(const char *label, const struct form *form)
{
	printf("%s: ", label);
	form_print(form);
	putchar('\n');
}

static void node_labeled_print(const char *label, const struct node *node)
{
	printf("%s:", label);
	if (listing_setting) {
		putchar('\n');
		node_list_body(node);
	} else {
		putchar(' ');
		node_print_body(node);
		putchar('\n');
	}
}

static void term_labeled_print(const char *label, const struct term *term)
{
	printf("%s: ", label);
	term_print(term);
	putchar('\n');
}

static void sanity_check_flatten(const struct node *node)
{
	assert(node);
	assert(node->variety == NODE_SENTINEL);
}

static struct node *do_flatten(const struct term *term)
{
	struct node *node = flatten(term);
	sanity_check_flatten(node);
	if (!quiet_setting) node_labeled_print("flat", node);
	return node;
}

static void sanity_check_reduction(const struct node *node)
{
	assert(node);
	assert(node->variety == NODE_SENTINEL);
	assert(node->nslots == 1);
	assert(node->slots[0].variety == SLOT_SUBST);
	assert(node->slots[0].subst->nref > 0);

	/*
	 * If the sentinel's substitution doesn't point to the leftmost
	 * node in the sentinel's list, the entire reduction must have
	 * yielded a reference to a previously defined global environment
	 * variable.  In this case this node's linked list should have
	 * been fully garbage collected.
	 */
	if (node->next != node->slots[0].subst) {
		assert(node->next == node->prev);
		assert(node->next == node);
	}
}

static struct node *timed_reduction(struct node *node, enum reduction reduction)
{
	struct timeval t0, t1;
	gettimeofday(&t0, NULL);
	node = reduce(node, reduction);
	gettimeofday(&t1, NULL);
	sanity_check_reduction(node);

	if (!quiet_setting) {
		long elapsed = (t1.tv_sec - t0.tv_sec) * 1000000 +
			       (t1.tv_usec - t0.tv_usec);
		printf("-dt-: %.6fs\n", elapsed / 1000000.0);
		node_labeled_print("eval", node);
	}

	return node;
}

/*
 * Define a value in the global environment.  First resolve the form
 * to a term, then flatten to a node, then perform a shallow (abstract)
 * reduction.  Install the resulting term in the global environment.
 */
static int stmt_define(symbol_mt name, struct form *form, unsigned flags)
{
	if (!quiet_setting) form_labeled_print("form", form);

	struct term *body = resolve(form);
	if (!body) return -1;	/* error already printed */
	if (!quiet_setting) term_labeled_print("body", body);

	assert(the_current_sourcefile);
	symbol_mt space = the_current_sourcefile->namespace;
	if (space == the_empty_symbol) {
		errf("No namespace yet, can't define '%s'\n",
		     symtab_lookup(name));
		return -1;
	}

	struct binder *binder;
	if (flags & BINDING_LIFTING) {
		/*
		 * Don't flatten and convert to a node, instead install
		 * in the environment as a term constant.  Referencing this
		 * constant will trigger lifting.
		 */
		binder = env_install(name, space, body);
		if (!binder) {
			errf("Failed to define '%s'\n", symtab_lookup(name));
			return -1;
		}
		goto apply_flags;
	}

	struct node *node = do_flatten(body);

	/* XXX should have option for this? */
	reset_eval_stats();
	reset_heap_stats();

	if (!(flags & BINDING_LITERAL)) {
		node = timed_reduction(node, (flags & BINDING_DEEP) ?
			REDUCTION_DEEP : REDUCTION_SURFACE);
	}

	if (!quiet_setting) {
		print_eval_stats();
		print_heap_stats();
	}

	binder = env_define(name, space, node);
	node_heap_baseline();
	if (!binder) {
		errf("Failed to define '%s'\n", symtab_lookup(name));
		return -1;
	}

apply_flags:
	assert(binder);
	if (flags & BINDING_OPAQUE)
		binder->flags |= BINDING_OPAQUE;
	return 0;
}

static void stmt_reduce(struct form *form, unsigned flags)
{
	form_labeled_print("form", form);

	struct term *term = resolve(form);
	if (!term) return;	/* error already printed */
	if (!quiet_setting) term_labeled_print("term", term);

	struct node *node = do_flatten(term);

	/* XXX should have option for this? */
	reset_eval_stats();
	reset_heap_stats();

	/*
	 * Map flags to a reduction strategy (messy, since bindings
	 * and values take different sets of flags and have different
	 * defaults).
	 */
	if (!(flags & BINDING_LITERAL)) {
		node = timed_reduction(node, (flags & BINDING_DEEP) ?
			REDUCTION_DEEP : REDUCTION_SURFACE);
	}

	term = unflatten(node);
	assert(term);
	node_free(node), node = NULL;
	if (!quiet_setting) term_labeled_print("term", term);

	form = readback(term);
	form_labeled_print("norm", form);
	interpret(term);

	if (!quiet_setting && !(flags & BINDING_LITERAL)) {
		print_eval_stats();
		print_heap_stats();
	}
	fputs("==================================="
	      "===================================\n", stdout);
}

int stmt_eval(const struct stmt *stmt)
{
	switch (stmt->variety) {
	case STMT_DEF:
		return stmt_define(stmt->def.var->var.name, stmt->def.val,
				   stmt->def.flags);
	case STMT_ECHO:
		fputs(stmt->form->str, stdout);
		putchar('\n');
		return 0;
	case STMT_VAL:
		stmt_reduce(stmt->val.val, stmt->val.flags);
		return 0;
	default: panicf("Unhandled stmt variety %d\n", stmt->variety);
	}
}
