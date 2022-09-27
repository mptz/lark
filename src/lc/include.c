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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "include.h"
#include "lc.h"
#include "lc.lex.h"

int
lc_include(const char *pathname)
{
	FILE *fin = fopen(pathname, "r");
	if (!fin) {
		fprintf(stderr, "File include: %s: %s\n",
			pathname, strerror(errno));
		return -1;
	}

	struct allocator alloc = { .name = "Include file allocator" };
	allocator_init(&alloc, ALLOCATOR_DEFAULT_SLOTS);

	lc_yyscan_t scanner;
	lc_yylex_init_extra(&alloc, &scanner);
	lc_yyrestart(fin, scanner);
	int retval = 0;
	if (lc_yyparse(&alloc, scanner)) {
		fprintf(stderr, "File include failed (parse error): %s\n",
			pathname);
		retval = -1;
	}
	lc_yylex_destroy(scanner);
	allocator_fini(&alloc);
	return retval;
}
