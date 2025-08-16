#ifndef LARK_MLC_SOURCEFILE_H
#define LARK_MLC_SOURCEFILE_H
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

#include <stdbool.h>

#include <util/circlist.h>
#include <util/symtab.h>
#include <util/wordbuf.h>
#include <util/wordtab.h>

struct stmt;

struct sourcefile {
	struct circlist entry;		/* for run/blocked queuing */
	struct wordbuf contents,	/* statements in sourcefile */
		       locals;		/* locally required sections */
	struct wordtab namespaces;	/* active namespaces */
	symbol_mt library,		/* containing library */
		  filename,		/* name of containing file */
		  namespace,		/* current section namespace */
		  requirement;		/* section we're blocked on */
	size_t pos, bound;		/* next line to resolve vs bound */
	int line;			/* file line number for requirement */
};

extern void sourcefile_init(struct sourcefile *sf, symbol_mt library,
			    symbol_mt filename);
extern void sourcefile_fini(struct sourcefile *sf);
extern void sourcefile_add(struct sourcefile *sf, struct stmt *stmt);
extern int sourcefile_begin(struct sourcefile *sf, symbol_mt section);

#endif /* LARK_MLC_SOURCEFILE_H */
