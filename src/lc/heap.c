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

#include <util/message.h>

#include "alloc.h"
#include "heap.h"
#include "term.h"

#define MAXTERM 100000

bool show_gc = true;
static struct term terms [MAXTERM];
static struct term *termfree;	/* free list */

static struct circlist the_allocators_sentinel;

static void
gc(struct term *root1, struct term *root2);

static void
heap_mark_allocators(void);

static void
term_mark(struct term *term);

struct term *
term_alloc(struct term *root1, struct term *root2)
{
	if (!termfree)
		gc(root1, root2);
	struct term *tmp = termfree;
	termfree = termfree->gbg.nextfree;
	return tmp;
}

static void
gc(struct term *root1, struct term *root2)
{
	const ssize_t nterms = sizeof terms / sizeof terms[0];
	ssize_t i, nc = 0, nf = 0, nu = 0;

	if (show_gc) {
		fprintf(stderr, "gc: ");
		fflush(stderr);
	}

	/* clear marks */
	for (i = nterms; --i >= 0; /* nada */)
		terms[i].mark = 0;

	/* mark roots registered via allocators */
	heap_mark_allocators();

	/* mark ad hoc roots */
	term_mark(root1);
	term_mark(root2);

	/* sweep */
	for (i = nterms; --i >= 0; /* nada */) {
		if (terms[i].mark != 0 && terms[i].mark != 1)
			panicf("Invalid term mark %u, type %d\n",
			       terms[i].mark, terms[i].type);
		if (terms[i].type == GBG)
			++nf;
		else if (terms[i].mark)
			++nu;
		else {
			terms[i].type = GBG;
			terms[i].gbg.nextfree = termfree;
			termfree = terms + i;
			++nc;
		}
	}

	if (show_gc)
		fprintf(stderr, "%zu used + %zu collected + %zu free = %zu\n",
				nu, nc, nf, nu + nc + nf);
	if (nc + nf == 0)
		panic("Exhausted term heap\n");
}

void
heap_init(void)
{
	ssize_t i = sizeof terms / sizeof terms[0];
	while (--i >= 0) {
		terms[i].type = GBG;
		terms[i].mark = 0;
		terms[i].gbg.nextfree = termfree;
		termfree = terms + i;
	}
	circlist_init(&the_allocators_sentinel);
}

static void
heap_mark_allocators(void)
{
	struct circlist_iter iter;
	circlist_iter_init(&the_allocators_sentinel, &iter);
	const struct allocator *entry;
	while ((entry = (const struct allocator *) circlist_iter_next(&iter))) {
		for (size_t i = entry->used; i; /* nada */)
			term_mark(entry->base[--i]);
	}
}

void
heap_allocator_register(struct allocator *alloc)
{
	circlist_add_tail(&the_allocators_sentinel, &alloc->entry);
}

void
heap_allocator_deregister(struct allocator *alloc)
{
	circlist_remove(&alloc->entry);
}

/*
 * Pointer-reversing mark operation.
 */
static void
term_mark(struct term *term)
{
	struct term *last, *next;

	for (last = NULL; term != NULL; last = term, term = next) {
		switch (term->mark) {
		case 0:
			/*
			 * First encounter with the term; traverse the
			 * first outgoing link, if one exists.
			 */
			switch (term->type) {
			case ABS:
				term->mark = 2;
				next = term->abs.body;
				term->abs.body = last;
				break;
			case APP:
				term->mark = 3;
				next = term->app.fun;
				term->app.fun = last;
				break;
			case SYM:
				if (term->sym.body) {
					term->mark = 2;
					next = term->sym.body;
					term->sym.body = last;
				} else {
					term->mark = 1;
					next = last;
				}
				break;
			case ERR:
			case VAR:
				term->mark = 1;
				next = last;
				break;
			default:
				panic("Invalid term while marking\n");
			}
			break;
		case 1:
			/*
			 * Term is completely marked--reverse course.
			 */
			next = last;
			break;
		case 2:
			/*
			 * Need to backtrack through this term.
			 */
			term->mark--;
			switch (term->type) {
			case ABS:
				next = term->abs.body;
				term->abs.body = last;
				break;
			case APP:
				next = term->app.arg;
				term->app.arg = last;
				break;
			case SYM:
				next = term->sym.body;
				term->sym.body = last;
				break;
			default:
				panic("Term can't have mark value 2\n");
			}
			break;
		case 3:
			/*
			 * Outstanding outgoing reference to traverse
			 * before backtracking.  This is only possible
			 * with applications; they're the only term type
			 * with two outgoing references.
			 */
			term->mark--;
			switch (term->type) {
			case APP:
				next = term->app.arg;
				term->app.arg = term->app.fun;
				term->app.fun = last;
				break;
			default:
				panic("Term can't have mark value 3\n");
			}
			break;
		default:
			panicf("Invalid term mark: %u\n", term->mark);
		}
	}
}
