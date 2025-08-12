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
#include <util/message.h>

#include "env.h"
#include "sourcefile.h"
#include "stmt.h"

/*
 * Sourcefiles begin with anonymous (random) namespaces, and new such
 * namespaces are created by unnumbered 'Section' directives.  These
 * namespaces are used to prevent name collisions when extending the
 * global environment, but can't be referenced in 'Requires' statements
 * since their identities are not known.
 */
static void sourcefile_begin_random_namespace(struct sourcefile *sf)
{
	char huid [HUID_STR];
	huid_fresh_str(huid, sizeof huid);
	sf->namespace = symtab_intern(huid);
	sf->section = sf->requirement = the_empty_symbol;
	sf->line = -1;
}

void sourcefile_init(struct sourcefile *sf, const char *filename)
{
	wordbuf_init(&sf->contents);
	wordtab_init(&sf->namespaces, 10 /* size hint */);
	sf->filename = xstrdup(filename);
	sf->pos = 0;
	sf->public = false;
	sourcefile_begin(sf, the_empty_symbol);
}

void sourcefile_fini(struct sourcefile *sf)
{
	for (size_t i = wordbuf_used(&sf->contents); i--; /* nada */)
		stmt_free((struct stmt *) wordbuf_at(&sf->contents, i));
	wordbuf_fini(&sf->contents);
	wordtab_fini(&sf->namespaces);
	xfree(sf->filename);
}

/*
 * Callback used by the parser to accumulate a flat list of statements
 * in a sourcefile.  Necessitated by the Yacc/Bison calling convention.
 */
void sourcefile_add(struct sourcefile *sf, struct stmt *stmt)
{
	assert(sf);
	wordbuf_push(&sf->contents, (word) stmt);
}

/*
 * Beginning a section doesn't make it available--that happens at the
 * end of a section once its definitions have beed added to the global
 * environment.  Beginning a section just updates the current namespace
 * as well as current section if numbered/named.
 */
int sourcefile_begin(struct sourcefile *sf, symbol_mt section)
{
	if (section == the_empty_symbol)
		sourcefile_begin_random_namespace(sf);
	else
		sf->namespace = sf->section = section;
	if (env_new_space(sf->namespace)) {
		errf("Section already exists: #%s\n",
		     symtab_lookup(sf->namespace));
		return -1;
	}
	wordtab_set(&sf->namespaces, sf->namespace);
	return 0;
}
