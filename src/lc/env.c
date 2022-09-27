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

#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "alloc.h"
#include "env.h"
#include "heap.h"
#include "term.h"

static struct allocator the_global_env = { .name = "Global environment" };

void
env_dump(void)
{
	for (size_t i = 0; i < the_global_env.used; ++i) {
		term_print(the_global_env.base[i]);
		putchar('\n');
	}
}

void
env_init(void)
{
	allocator_init(&the_global_env, ALLOCATOR_DEFAULT_SLOTS);
}

void
env_install(struct term *sym)
{
	allocator_push(&the_global_env, sym);
}

struct term *
env_lookup(symbol_mt name)
{
	for (size_t i = the_global_env.used; i; /* nada */) {
		if (name == the_global_env.base[--i]->sym.name)
			return the_global_env.base[i]->sym.body;
	}
	fprintf(stderr, "unbound symbol: %s\n", symtab_lookup(name));
	return &the_error_term;
}
