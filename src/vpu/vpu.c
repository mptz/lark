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

#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include <util/message.h>
#include <util/word.h>

#include "bignum.h"
#include "heap.h"
#include "pstr.h"
#include "vpu.h"

/*
 * The externally accessible pointer to the dispatch table.  Set by run()
 * when it's called with a NULL argument.  We can't initialize it statically
 * because gcc only allows you to take the address of a label within the
 * function defining the label.
 */
void **vpu_insn_table;

char vpu_insn_arg_table [] = {
#include "opargs.c"
};

#ifdef VPU_STACK_IMPLEMENTED
/*
 * This specifies the default number of words to allocate for the stack,
 * but we may round up to abut guard pages, i.e. we make no guarantee of
 * overflow on reaching this limit.
 */
#define VPU_STACK_WORDS 5000

/*
 * We currently have two stacks rather than one; the value stack contains
 * GC-visible value pointers while the code stack contains instruction
 * pointers, which should not be scanned during GC.  Neither stack is
 * currently growable.  The stacks are heap-allocated and guarded on both
 * ends by VM-protected areas.
 */
static union value **S, **SP, **FP;	/* stack with stack and frame ptrs */
static word **CS, **CSP;		/* code stack (non-GC'able) */
static void *voflo_guard, *vuflo_guard, *vuflo_bound,
	    *coflo_guard, *cuflo_guard, *cuflo_bound;

#endif	/* VPU_STACK_IMPLEMENTED */

#if 0	/* XXX */
/*
 * Wrappers for typecasts.
 */
static inline uintptr_t i2w(insn insn) { return (uintptr_t) insn; }
#endif

#ifdef VPU_STACK_IMPLEMENTED
static void
segvhandler(int sig, siginfo_t *si, void *unused)
{
	assert(sig == SIGSEGV);
	if (si->si_addr >= voflo_guard && si->si_addr < (void*) S) {
		fprintf(stderr, "Stack overflow: "
			"SIGSEGV at 0x%lX in guard area\n",
			(long) si->si_addr);
		exit(EXIT_FAILURE);
	}
	if (si->si_addr >= vuflo_guard && si->si_addr < vuflo_bound) {
		fprintf(stderr, "Stack underflow: "
			"SIGSEGV at 0x%lX in guard area\n",
			(long) si->si_addr);
		exit(EXIT_FAILURE);
	}
	if (si->si_addr >= coflo_guard && si->si_addr < (void*) CS) {
		fprintf(stderr, "Code stack overflow: "
			"SIGSEGV at 0x%lX in guard area\n",
			(long) si->si_addr);
		exit(EXIT_FAILURE);
	}
	if (si->si_addr >= cuflo_guard && si->si_addr < cuflo_bound) {
		fprintf(stderr, "Code stack underflow: "
			"SIGSEGV at 0x%lX in guard area\n",
			(long) si->si_addr);
		exit(EXIT_FAILURE);
	}

	/*
	 * Segmentation fault not attributable to stack overflow; revert
	 * to default behavior and re-raise.
	 */
	signal(sig, SIG_DFL);
	raise(sig);
}

/*
 * A single 4K guard page seems a bit small--easy to overshoot?
 */
static size_t
get_guardsize(size_t pagesize)
{
	return (pagesize < 8192)  ? pagesize * 4 :
	       (pagesize < 16384) ? pagesize * 2 :
	       pagesize;
}

/*
 * Set the stack to the smallest page-multiple size larger than the
 * desired size.
 */
static size_t
get_stacksize(size_t pagesize)
{
	size_t stacksize = sizeof *S * STACKWORDS,
	       remainder = stacksize % pagesize;
	return remainder ? stacksize - remainder + pagesize : stacksize;
}

static void
stackconf(void)
{
	/*
	 * Allocate page-aligned regions with guard areas at either end;
	 * both stacks grow down and will write into lower-guarded regions
	 * on overflow and guards above on underflow.
	 */
	long pagesize = sysconf(_SC_PAGESIZE);
	if (pagesize == -1)
		ppanic("sysconf(_SC_PAGESIZE)");
	size_t guardsize = get_guardsize(pagesize),
	       stacksize = get_stacksize(pagesize),
	       allocsize = 2 * guardsize + stacksize;

	/*
	 * Allocate value and code stacks.
	 */
	if (posix_memalign(&voflo_guard, pagesize, allocsize) != 0)
		ppanic("posix_memalign");
	S = voflo_guard + guardsize;
	vuflo_guard = S + (stacksize / sizeof *S);
	vuflo_bound = vuflo_guard + guardsize;
	assert(voflo_guard + allocsize == vuflo_bound);

	if (posix_memalign(&coflo_guard, pagesize, allocsize) != 0)
		ppanic("posix_memalign");
	CS = coflo_guard + guardsize;
	cuflo_guard = CS + (stacksize / sizeof *CS);
	cuflo_bound = cuflo_guard + guardsize;
	assert(coflo_guard + allocsize == cuflo_bound);

	/*
	 * Set up a signal handler for SIGSEGV to catch references into
	 * the guard regions.
	 */
	struct sigaction sa;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = segvhandler;
	if (sigaction(SIGSEGV, &sa, NULL) == -1)
		ppanic("sigaction");

	/*
	 * Now configure memory protection on the guard regions.
	 */
	if (mprotect(voflo_guard, guardsize, PROT_NONE) == -1)
		ppanic("mprotect");
	if (mprotect(vuflo_guard, guardsize, PROT_NONE) == -1)
		ppanic("mprotect");
	if (mprotect(coflo_guard, guardsize, PROT_NONE) == -1)
		ppanic("mprotect");
	if (mprotect(cuflo_guard, guardsize, PROT_NONE) == -1)
		ppanic("mprotect");

	SP = FP = vuflo_guard;
	CSP = cuflo_guard;
	//heap_stack_register((void **) SP, (void ***) &SP); XXX
}
#endif	/* VPU_STACK_IMPLEMENTED */

void vpu_init(struct vpu *vpu, const char *name)
{
	vpu->name = name;
	vpu->r0 = vpu->r1 = vpu->r2 = vpu->r3 =
	vpu->r4 = vpu->r5 = vpu->r6 = vpu->r7 =
	vpu->r8 = vpu->r9 = vpu->rA = vpu->rB =
	vpu->rC = vpu->rD = vpu->rE = vpu->rF = 0;
	vpu->mm = 0;		/* nothing gc-managed */
	vpu->rr = 0;
	heap_register_vpu(vpu);
	vpu_run(NULL);		/* externalize dispatch table */
}

void vpu_fini(struct vpu *vpu)
{
	/* in the absence of stack impl, nothing to do here. */
}

/*
 * Dispatch variants handling the current instruction or the next
 * instruction, respectively.  The current-instruction variant is
 * used for branches and other instructions which override sequential
 * code flow.
 */
#define CURR goto **(m->ip)
#define NEXT goto **++(m->ip)

void
vpu_run(struct vpu *m)
{
	static void *dispatch[] = {
#include "oplabels.c"
	};

	/* During initialization we're called to expose the dispatch table. */
	if (!m) {
		vpu_insn_table = dispatch;
		return;
	}

	/* Begin execution. */
	assert(m->ip);
	CURR;

#include "opimpls.c"
}

void
vpu_set_code(struct vpu *vpu, void **code)
{
	assert(vpu);
	assert(code);
	vpu->ip = code;
}
