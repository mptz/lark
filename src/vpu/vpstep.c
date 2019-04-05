/*
 * Copyright (c) 2009-2015 Michael P. Touloumtzis.
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

#include <curses.h>
#include <glob.h>
#include <stdio.h>
#include <readline/history.h>
#include <readline/readline.h>	/* must follow stdio.h */
#include <term.h>
#include <unistd.h>

#include <util/message.h>

#include "heap.h"
#include "opcode.h"
#include "vpstep.h"
#include "vpu.h"

#if UINTPTR_MAX == 0xFFFFFFFF
#define REGFMT "08X"
#elif UINTPTR_MAX == 0xFFFFFFFFFFFFFFFF
#define REGFMT "016lX"
#else
#error "Can't determine register size"
#endif

void dump_vpu(const struct vpu *vpu)
{
	printf(	"VPU: %s\n"
		"\n"
		"  R0: %"REGFMT"    R8: %"REGFMT"\n"
		"  R1: %"REGFMT"    R9: %"REGFMT"\n"
		"  R2: %"REGFMT"    RA: %"REGFMT"\n"
		"  R3: %"REGFMT"    RB: %"REGFMT"\n"
		"  R4: %"REGFMT"    RC: %"REGFMT"\n"
		"  R5: %"REGFMT"    RD: %"REGFMT"\n"
		"  R6: %"REGFMT"    RE: %"REGFMT"\n"
		"  R7: %"REGFMT"    RF: %"REGFMT"\n"
		"\n",
		vpu->name,
		vpu->r0, vpu->r8,
		vpu->r1, vpu->r9,
		vpu->r2, vpu->rA,
		vpu->r3, vpu->rB,
		vpu->r4, vpu->rC,
		vpu->r5, vpu->rD,
		vpu->r6, vpu->rE,
		vpu->r7, vpu->rF);
}

void step(struct vpu *vpu)
{
	/* this is static to avoid invalid vpu->ip */
	static void *code [4];

	void **pc = code;
	word i = insn_code2index(vpstep_insn);
	*pc++ = vpu_insn_table[i];
	char c = vpu_insn_arg_table[i];
	if (c != '0')
		*pc++ = (void*) vpstep_arg;
	*pc++ = vpu_insn_table[insn_code2index(opHALT)];

	vpu_set_code(vpu, code);
	vpu_run(vpu);
}

int main(int argc, char *argv[])
{
	set_execname(argv[0]);
	heap_init();
	insn_code2index_init();

	int doclear = (setupterm(NULL, 1, NULL) == OK) &&
		isatty(fileno(stdout));
	doclear = 0;	/* XXX */
	const char *cls = "";
	if (doclear)
		cls = tgetstr("cl", NULL);

	/*
	 * Load readline history from a dotfile.
	 */
	char *histfile = NULL;
	glob_t globbuf;
	if (glob("~/.vpstep_history", GLOB_TILDE, NULL, &globbuf) == 0 &&
	    globbuf.gl_pathc == 1) {
		histfile = globbuf.gl_pathv[0];
		read_history(histfile);
	}

	struct vpu vpu;
	vpu_init(&vpu, "vpstat VPU");

	char *input;
	int parseerr = 0;
	while (1) {
		if (!parseerr) {
			if (doclear) putp(cls);
			putchar('\n');
			dump_vpu(&vpu);
		}
		input = readline("> ");
		if (!input) break;
		if (*input)
			add_history(input);
		vpstep_init_lexer(input);
		parseerr = vpstep_yyparse();
		if (!parseerr)
			step(&vpu);
		vpstep_fini_lexer();
		free(input);
	}
	if (histfile) {
		write_history(histfile);
		globfree(&globbuf);
	}

	return 0;
}
