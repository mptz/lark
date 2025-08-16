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

#include <util/memutil.h>
#include <util/message.h>

#include "env.h"
#include "sourcefile.h"
#include "stmt.h"

void sourcefile_init(struct sourcefile *sf, symbol_mt library,
		     symbol_mt filename)
{
	assert(library != the_empty_symbol);

	circlist_init(&sf->entry);
	wordbuf_init(&sf->contents);
	wordbuf_init(&sf->locals);
	wordtab_init(&sf->namespaces, 10 /* size hint */);
	sf->library = library;
	sf->filename = filename;
	sf->namespace = sf->requirement = the_empty_symbol;
	sf->pos = sf->bound = 0;
}

void sourcefile_fini(struct sourcefile *sf)
{
	for (size_t i = wordbuf_used(&sf->contents); i--; /* nada */)
		stmt_free((struct stmt *) wordbuf_at(&sf->contents, i));
	wordbuf_fini(&sf->contents);
	wordbuf_fini(&sf->locals);
	wordtab_fini(&sf->namespaces);
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
 * and section.
 */
int sourcefile_begin(struct sourcefile *sf, symbol_mt section)
{
	assert(section != the_empty_symbol);
	if (env_begin(section, sf->library)) return -1;
	sf->namespace = section;
	wordtab_set(&sf->namespaces, sf->namespace);
	return 0;
}
