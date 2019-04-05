/*
 * Copyright (c) 2009-2018 Michael P. Touloumtzis.
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

#include <util/message.h>
#include <util/wordtab.h>

#include "opcode.h"

const char *const opcode_names[] = {
#include "opnames.c"
};

static const insncode insncodes[] = {
#include "insncodes.c"
};

static struct wordtab insn_index_tab;

void insn_code2index_init(void)
{
	size_t b = sizeof insncodes / sizeof insncodes[0];
	wordtab_init(&insn_index_tab, b /* hint */);
	/* n.b. value in wordtab is actual + 1 because we can't store 0s
	   as table values; we decrement on the way back out. */
	for (size_t i = 0; i < b; ++i)
		wordtab_put(&insn_index_tab, insncodes[i], (void*) (i + 1));
}

unsigned insn_code2index(insncode insncode)
{
	size_t i = (size_t) wordtab_get(&insn_index_tab, insncode);
	if (!i)
		panic("Invalid instruction code given to code2index!\n");
	return i - 1;
}
