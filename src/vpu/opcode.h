#ifndef LARK_VPU_OPCODE_H
#define LARK_VPU_OPCODE_H
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

#include <stdint.h>

/*
 * Opcodes are not actually constrained to 16 bits by the architecture...
 * we just don't need more bits at the moment.
 */
typedef uint16_t opcode;

/*
 * The list of opcodes is generated during build.
 */
#include "opcodes.h"

/*
 * String name for each of the above.
 */
extern const char *const opcode_names[];

/*
 * Instruction codes are really an intermediate form, regular in structure
 * and easy to assemble; they're mapped to instruction indexes during
 * assembly (which are in turn mapped to code pointers at load time).
 */
typedef uint32_t insncode;

extern void insn_code2index_init(void);
extern unsigned insn_code2index(insncode insncode);

#endif /* LARK_VPU_OPCODE_H */
