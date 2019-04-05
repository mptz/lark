#ifndef LARK_VPU_VPU_H
#define LARK_VPU_VPU_H
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

#include <util/circlist.h>
#include <util/word.h>

/*
 * This virtual CPU implementation is reentrant; all registers and other
 * metadata are stored in a structure passed to the run function.
 */
struct vpu {
	struct circlist gc_entry;	/* garbage collector registration */
	const char *name;		/* for gc and other debugging */
	word	r0, r1, r2, r3, r4, r5, r6, r7,
		r8, r9, rA, rB, rC, rD, rE, rF;
				/* general purpose registers */
	word	fp, sp;		/* frame and stack pointers */
	word	mm;		/* managed mask */
	offset	rr;		/* result register */
	void	**ip;		/* instruction pointer */
};

extern void vpu_init(struct vpu *vpu, const char *name);
extern void vpu_fini(struct vpu *vpu);
extern void vpu_run(struct vpu *vpu);
extern void vpu_set_code(struct vpu *vpu, void **code);

/*
 * This table is used when loading files to map instruction indices to
 * instruction-implementation pointers (for direct-threaded interpretation
 * by computed gotos).
 */
extern void **vpu_insn_table;

/*
 * An auxiliary table, also used during loading, identifying instructions
 * which are followed by inline arguments (following word in the stream).
 */
extern char vpu_insn_arg_table [];

#endif /* LARK_VPU_VPU_H */
