#ifndef LARK_MLC_LIBRARY_H
#define LARK_MLC_LIBRARY_H
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

/*
 * At any given time we're resolving (at most) once sourcefile.
 * Library functionality handles swapping among them and keeping
 * this pointer up to date.  Only guaranteed to be valid during
 * resolution.
 */
struct sourcefile;
extern struct sourcefile *the_current_sourcefile;

extern int library_load(const char *name);
extern int library_load_files(int nnames, char *const names[]);
extern int library_read_stdin(void);
extern int library_repl_init(void);
extern int library_repl_line(const char *line, int lineno);
extern int library_repl_fini(void);

#endif /* LARK_MLC_LIBRARY_H */
