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

#include "beta.h"
#include "heap.h"
#include "node.h"
#include "subst.h"

/*
 * Replace the redex (which is disappearing) with the chain resulting
 * from substitution.  Update references and list structure as needed.
 */
static void
replace_redex(struct node *redex, struct node *next, struct node *prev)
{
	/* depths */
	assert(next->depth == redex->depth);
	assert(prev->depth == redex->depth);

	/* backref and references */
	assert(redex->backref);
	next->backref = redex->backref;
	next->backref->subst = next;
	assert(redex->nref == 1);
	next->nref = redex->nref;
	redex->nref--;

	/* linked-list structure */
	next->prev = redex->prev;
	prev->next = redex->next;
	next->prev->next = next;
	prev->next->prev = prev;
}

struct node *beta_reduce(struct node *redex, struct node *body,
			 int depth, int delta)
{
	assert(depth >= 0);
	assert(delta >= 0);
	struct subst subst = {
		.vals = redex,
		.basedepth = depth,
		.shift = delta - 1,	/* extra -1 for abstraction elim */
	};
	assert(body->variety == NODE_SENTINEL);
	subst_copy(body, &subst);
	replace_redex(redex, subst.next, subst.prev);
	return subst.prev;
}

/*
 * A subtle point... reduce() will free the redex, and we don't want
 * to free the body since it's put into use (spliced into the evaluation
 * chain), but the *sentinel* for the body is no longer in use; free that.
 */
struct node *beta_nocopy(struct node *redex, struct node *body,
			 int depth, int delta)
{
	assert(depth >= 0);
	assert(delta >= 0);
	struct subst subst = {
		.vals = redex,
		.basedepth = depth,
		.shift = delta - 1,	/* extra -1 for abstraction elim */
	};
	assert(body->variety == NODE_SENTINEL);
	subst_edit(body, &subst);
	replace_redex(redex, body->next, body->prev);
	struct node *tmp = body->prev;
	node_heap_free(body);
	return tmp;
}
