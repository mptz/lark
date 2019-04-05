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
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <util/fdutil.h>
#include <util/memutil.h>
#include <util/message.h>
#include <util/page.h>

#include "asm.h"
#include "fixup.h"
#include "heap.h"
#include "opcode.h"
#include "pool.h"
#include "vpheader.h"

static void init(void)
{
	heap_init();
	pool_init();
	insn_code2index_init();
}

static const char *make_outpath(const char *inpath)
{
	char *p = strrchr(inpath, '.');
	if (!p || strcmp(p, ".vps"))
		return "vpasm.out";
	char *s = xstrdup(inpath);	/* never freed; that's OK */
	p = strrchr(s, '.');
	assert(p);
	assert(!strcmp(p, ".vps"));
	p[3] = 'b';			/* .vps -> .vpb */
	return s;
}

static void usage(void)
{
	fprintf(stderr, "Usage: vpasm <options> <filename>\n");
	exit(EXIT_FAILURE);
}

unsigned flag_dummy1, flag_dummy2;

int main(int argc, char *argv[])
{
	const char *opt_outpath = NULL;
	set_execname(argv[0]);
	global_message_threshold = 100;	/* everything */
	init();

	int c;
	while ((c = getopt(argc, argv, "D:do:")) != -1) {
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
		case 'd': asm_yydebug = 1; break;
		case 'o': opt_outpath = optarg; break;
		}
	}
	if (optind + 1 != argc)
		usage();

	/*
	 * Open input file.  We don't open the output file yet; since we
	 * finish assembly before writing anything, delaying creation of
	 * the output file means one fewer thing to clean up on failure.
	 */
	const char *inpath = argv[optind];
	FILE *input = strcmp(inpath, "-") ? fopen(inpath, "r") : stdin;
	if (!input)
		return xperror(inpath);

	/* parse input file */
	asm_init();
	struct lexer lexer;
	asm_init_file_lexer(&lexer, input);
	int retval = asm_yyparse(lexer.scanner);
	asm_fini_lexer(&lexer);

	/*
	 * Open output file... all our hard work might be for naught.
	 */
	const char *outpath = opt_outpath ? opt_outpath : make_outpath(inpath);
	int output = creat(outpath, 0666);
	if (output < 0)
		return xperror(outpath);

	/*
	 * Write output header, including instruction count for the loader.
	 */
	struct vpu_header_metadata md = {
		.fixupbase = VPU_HEADER_SIZE,
		.nfixups = asm_num_fixups(),
		.insnbase = md.fixupbase + md.nfixups * FIXUP_SIZE,
		.insnwords = asm_num_insnwords(),
		.poolbase = pageabove(md.insnbase + md.insnwords * WORD_SIZE),
		.poolsize = pool_size(),
	};
	{
		char header [VPU_HEADER_SIZE];
		vpu_header_write(header, &md);
		p_writeall(output, header, sizeof header);
	}

	/*
	 * Write CISC translation table.
	 */
	/* XXX TBD */

	/*
	 * Write sections, first seeking to base offsets determined above.
	 */
	assert(p_tell(output) == md.fixupbase);
	p_writeall(output, asm_fixupwords(), md.nfixups * FIXUP_SIZE);
	assert(p_tell(output) == md.insnbase);
	p_writeall(output, asm_insnwords(), md.insnwords * WORD_SIZE);
	p_lseek(output, md.poolbase, SEEK_SET);	/* seek forward to align */
	p_writeall(output, pool_base(), md.poolsize);

	/*
	 * Close input & output files.
	 */
	if (input != stdin)
		fclose(input);
	if (output != STDOUT_FILENO && close(output) != 0) {
		unlink(outpath);	/* best effort */
		return xperror(outpath);
	}

	return retval;
}
