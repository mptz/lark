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
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <util/fdutil.h>
#include <util/memutil.h>
#include <util/message.h>

#include "fixup.h"
#include "heap.h"
#include "vpheader.h"
#include "vpu.h"

static void
fread_panic(FILE *f, const char *pathname)
{
	if (feof(f))
		panicf("unexpected EOF in '%s'\n", pathname);
	assert(ferror(f));
	ppanic(pathname);
}

static void
init(void)
{
	heap_init();
}

static void
usage(void)
{
	fprintf(stderr, "Usage: vpasm <options> [<filename>]\n");
	exit(EXIT_FAILURE);
}

unsigned flag_dummy1, flag_dummy2;

static struct vpu VPU;

int main(int argc, char *argv[])
{
	set_execname(argv[0]);
	global_message_threshold = 100;	/* traces, etc */
	init();

	int c;
	while ((c = getopt(argc, argv, "D:dq")) != -1) {
		switch (c) {
		case 'D': {
			while (*optarg) switch (*optarg++) {
			case '1': flag_dummy1 = 1; break;
			case '2': flag_dummy2 = 1; break;
			default: panicf("Unrecognized debug flag '%c'\n",
					*--optarg);
			}
			break;
		}
		case 'd': /* set debug option */; break;
		case 'q': global_message_threshold = 20; break;
		}
	}
	if (optind + 1 != argc)
		usage();

	/*
	 * Open input file; read & verify header.
	 */
	const char *pathname = argv[optind];
	char buf [VPU_HEADER_SIZE];

	int fd = open(pathname, O_RDONLY);
	if (fd < 0) {
		/* try pathname with ".vpb" suffix */
		const char suffix[] = ".vpb";
		char *s = xmalloc(strlen(pathname) + sizeof suffix);
		strcpy(stpcpy(s, pathname), suffix);
		pathname = s;
		fd = open(pathname, O_RDONLY);
		if (fd < 0) {
			xperror(pathname);
			exit(EXIT_FAILURE);
		}
	}
	if (r_readall(fd, buf, sizeof buf)) {
		xperror(pathname);
		exit(EXIT_FAILURE);
	}
	if (vpu_header_verify(buf)) {
		errf("failed header verification for '%s'\n", pathname);
		exit(EXIT_FAILURE);
	}
	struct vpu_header_metadata md = *vpu_header_metadata(buf);

	/*
	 * First, map the literal pool into memory.  We'll need the
	 * mapping's address for fixups when we read in code.  mmap(2)
	 * requires size > 0 so skip literal mapping on empty pool.
	 */
	void *lbase = 0;
	if (md.poolsize > 0) {
		lbase = mmap(NULL, md.poolsize, PROT_READ, MAP_PRIVATE,
			     fd, md.poolbase);
		if (lbase == MAP_FAILED)
			ppanic("mmap");
	}
	if (md.nfixups) assert(lbase);

	/*
	 * Initialize virtual CPU.  This must be done before we use the
	 * externally visible VPU instruction table, which occurs during
	 * code loading.
	 */
	vpu_init(&VPU, "vprun main VPU");

	/*
	 * Now we read the fixup table and code using buffered I/O.
	 */
	FILE *f = fdopen(fd, "r");
	assert(ftell(f) == md.fixupbase);
	struct fixup *fixups = xmalloc(md.nfixups * FIXUP_SIZE);
	if (fread(fixups, FIXUP_SIZE, md.nfixups, f) != md.nfixups)
		fread_panic(f, pathname);

	assert(ftell(f) == md.insnbase);
	void **code = xmalloc(md.insnwords * sizeof *code);
	for (size_t i = 0, fi = 0; i < md.insnwords; /* nada */) {
		if (fi < md.nfixups)
			assert(fixups[fi].pos > i);   /* can't fix up insn */
		word insn;
		if (fread(&insn, sizeof insn, 1, f) != 1)
			fread_panic(f, pathname);
		code[i++] = vpu_insn_table[insn];
		char c = vpu_insn_arg_table[insn];
		if (c != '0') {
			word adj = 0;
			if (fi < md.nfixups && fixups[fi].pos == i) {
				adj = (word) lbase;
				++fi;
			}
			if (fread(&insn, sizeof insn, 1, f) != 1)
				fread_panic(f, pathname);
			code[i++] = (void*) (insn + adj);
		}
	}
	vpu_set_code(&VPU, code);
	vpu_run(&VPU);

	/*
	 * Clean up.
	 */
	vpu_fini(&VPU);
	if (fclose(f) != 0)
		ppanic(pathname);

	return 0;
}
