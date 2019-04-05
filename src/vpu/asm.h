#ifndef LARK_VPU_ASM_H
#define LARK_VPU_ASM_H
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

#include <stdio.h>

#include <util/word.h>

extern void asm_init(void);
extern const word *asm_fixupwords(void);
extern const word *asm_insnwords(void);
extern word asm_num_fixups(void);
extern word asm_num_insnwords(void);
extern int asm_yydebug;
extern int asm_yyparse(void *scanner);

struct lexer { void *scanner, *buffer; };
extern void asm_init_cstr_lexer(struct lexer *lexer, const char *cstr);
extern void asm_init_file_lexer(struct lexer *lexer, FILE *file);
extern void asm_fini_lexer(struct lexer *lexer);

#endif /* LARK_VPU_ASM_H */
