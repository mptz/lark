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
#include <stdlib.h>

#include <util/memutil.h>
#include <util/message.h>
#include <util/wordbuf.h>
#include <util/wordtab.h>

#include "binder.h"
#include "env.h"
#include "node.h"
#include "term.h"

#define ENV_SIZE_HINT 1000
#define NS_SIZE_HINT 100

static struct wordbuf	global_env_by_index;
static struct wordtab	global_env_by_name,
			global_env_spaces,
			global_env_published;

void env_init(void)
{
	wordbuf_init(&global_env_by_index);
	wordtab_init(&global_env_by_name, ENV_SIZE_HINT);
	wordtab_init(&global_env_spaces, NS_SIZE_HINT);
	wordtab_init(&global_env_published, NS_SIZE_HINT);

	/*
	 * To reserve 0 for use as an out-of-band environment index,
	 * push a dummy node in a dummy namespace at the 0th index.
	 */
	struct binder *dummy = xmalloc(sizeof *dummy);
	symbol_mt name = symtab_intern("HPkvAMF3.6rLHX9XL.gG5J02VT");
	dummy->index = 0;
	dummy->name = name;
	dummy->space = symtab_intern("8CzDnhVz.I1KxfudP.bDbC38_S");
	dummy->val = NULL;
	dummy->flags = BINDING_OPAQUE;
	wordbuf_push(&global_env_by_index, (word) dummy);
}

const struct binder *env_at(size_t index)
{
	assert(index < wordbuf_used(&global_env_by_index));
	return (const struct binder *) wordbuf_at(&global_env_by_index, index);
}

void env_dump(const char *substr)
{
	size_t ndefs = wordbuf_used(&global_env_by_index);
	for (size_t i = 1 /* skip dummy */; i < ndefs; ++i) {
		const struct binder *binder = (const struct binder *)
			wordbuf_at(&global_env_by_index, i);
		const char *name = symtab_lookup(binder->name);
		if (!strstr(name, substr ?: ""))
			continue;
		printf("#%zu\t%s\t%s := ", binder->index,
			symtab_lookup(binder->space), name);
		if (binder->flags & BINDING_OPAQUE)
			fputs("$opaque", stdout);
		else if (binder->term)
			term_print(binder->term);
		else if (binder->val)
			node_print_body(binder->val);
		putchar('\n');
	}
}

static struct binder *env_put(symbol_mt name, symbol_mt space,
			      struct term *term, struct node *val,
			      unsigned flags, struct wordbuf *slot)
{
	assert(name != the_empty_symbol);
	assert(space != the_empty_symbol);

	if (term) {
		assert(flags & BINDING_LIFTING);
		assert(!val);
	} else
		assert(!term);

	struct binder *binder = xmalloc(sizeof *binder);
	binder->index = wordbuf_used(&global_env_by_index);
	binder->name = name;
	binder->space = space;
	binder->term = term;
	binder->val = val;
	binder->flags = flags;

	wordbuf_push(&global_env_by_index, (word) binder);
	wordbuf_push(slot, (word) binder);
	return binder;
}

/*
 * Is the given name available for use in the given space?
 */
static bool
env_name_is_available(symbol_mt name, symbol_mt space, struct wordbuf *slot)
{
	for (size_t i = 0, n = wordbuf_used(slot); i < n; ++i) {
		struct binder *b = (struct binder *) wordbuf_at(slot, i);
		assert(b->name == name);
		if (b->space == space) {
			errf("Name '%s' already defined in space #%s\n",
			     symtab_lookup(name), symtab_lookup(space));
			return false;
		}
	}
	return true;
}

/*
 * Get the slot for a given name, creating it if needed.
 */
static struct wordbuf *env_slot(symbol_mt name)
{
	struct wordbuf *slot = wordtab_get(&global_env_by_name, name);
	if (!slot) {
		slot = xmalloc(sizeof *slot);
		wordbuf_init(slot);
		wordtab_put(&global_env_by_name, name, slot);
	}
	return slot;
}

/*
 * Defining a variable fails if it's already defined in the current
 * namespace.  Unlike with declarations, other namespaces aren't relevant.
 */
struct binder *env_define(symbol_mt name, symbol_mt space, struct node *val)
{
	struct wordbuf *slot = env_slot(name);
	if (!env_name_is_available(name, space, slot)) return NULL;
	return env_put(name, space, NULL, val, BINDING_DEFAULT, slot);
}

struct binder *env_install(symbol_mt name, symbol_mt space, struct term *term)
{
	struct wordbuf *slot = env_slot(name);
	if (!env_name_is_available(name, space, slot)) return NULL;
	return env_put(name, space, term, NULL, BINDING_LIFTING, slot);
}

struct binder *env_lookup(symbol_mt name, const struct wordtab *spaces)
{
	struct wordbuf *slot = env_slot(name);
	struct binder *binder = NULL;
	bool collision = false;
	for (size_t i = 0, n = wordbuf_used(slot); i < n; ++i) {
		struct binder *b = (struct binder *) wordbuf_at(slot, i);
		assert(b->name == name);
		if (!wordtab_test(spaces, b->space))
			continue;
		if (binder) {
			collision = true;
			errf("Ambiguous name '%s' in space #%s\n",
				symtab_lookup(name),
				symtab_lookup(binder->space));
		}
		binder = b;
	}
	if (collision) {
		errf("Ambiguous name '%s' in space #%s\n",
			symtab_lookup(name), symtab_lookup(binder->space));
		return NULL;
	}
	if (!binder)
		errf("Name '%s' not found in available spaces\n",
			symtab_lookup(name));
	return binder;
}

/*
 * env_test is currently used to see if we need to freshen local vars
 * to avoid conflict with global names (which is more about confusion
 * than correctness) so it doesn't need access to current namespaces.
 * Look for an instance of the name in any namespace.
 */
bool env_test(symbol_mt name)
{
	return !!wordtab_get(&global_env_by_name, name);
}

/*
 * Namespace- and library-related functionality.
 */

int env_begin(symbol_mt space, symbol_mt library)
{
	symbol_mt prior = (word) wordtab_get(&global_env_spaces, space);
	if (prior) {
		errf("Section #%s already exists in '%s', "
		     "can't begin in '%s'\n", symtab_lookup(space),
		     symtab_lookup(prior), symtab_lookup(library));
		return -1;
	}
	wordtab_put(&global_env_spaces, space, (void*) (word) library);
	return 0;
}

void env_publish(symbol_mt space, symbol_mt published)
{
	struct wordbuf *slot = wordtab_get(&global_env_published, space);
	if (!slot) {
		slot = xmalloc(sizeof *slot);
		wordbuf_init(slot);
		wordtab_put(&global_env_published, space, slot);
	}
	wordbuf_push(slot, (word) published);
}

void env_published(symbol_mt space, struct wordbuf *buf)
{
	struct wordbuf *slot = wordtab_get(&global_env_published, space);
	for (size_t i = 0, n = slot ? wordbuf_used(slot) : 0;
	     i < n;
	     wordbuf_push(buf, wordbuf_at(slot, i++)));
}

symbol_mt env_whose(symbol_mt space)
{
	symbol_mt library = (word) wordtab_get(&global_env_spaces, space);
	return library ? : the_empty_symbol;
}
