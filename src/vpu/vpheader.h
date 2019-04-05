#ifndef LARK_VPU_VPHEADER_H
#define LARK_VPU_VPHEADER_H
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

#include <util/word.h>

#define VPU_HEADER_SIZE 4096

/*
 * On 32-bit architectures, file sizes (and seek pointers) can be 64-bit;
 * we're OK using machine words here since our sizes are also limited by
 * the fact that we're mmap()ing/reading-into-buffers so a de facto 32-bit
 * limit still applies.
 */
struct vpu_header_metadata {
	word	fixupbase, nfixups,
		insnbase, insnwords,
		poolbase, poolsize;
};

extern void vpu_header_write(char *buf, struct vpu_header_metadata *md);
extern int vpu_header_verify(const char *buf);
extern struct vpu_header_metadata *vpu_header_metadata(const char *buf);

#endif /* LARK_VPU_VPHEADER_H */
