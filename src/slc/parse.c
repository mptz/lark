/*
 * Copyright (c) 2009-2022 Michael P. Touloumtzis.
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <util/message.h>

#include "parse.h"
#include "slc.h"
#include "slc.lex.h"

int parse_file(const char *pathname)
{
	FILE *input = strcmp(pathname, "-") ? fopen(pathname, "r") : stdin;
	if (!input)
		return xperror(pathname);

	slc_yyscan_t scanner;
	slc_yylex_init(&scanner);
	slc_yyrestart(input, scanner);
	int retval = slc_yyparse(scanner);
	slc_yylex_destroy(scanner);
	if (input != stdin) fclose(input);
	return retval;
}

int parse_include(const char *pathname)
{
	char *envpaths = getenv("SLC_INCLUDE");
	FILE *fin = NULL;

	if (envpaths) {
		char pathbuf [strlen(envpaths) + 1], *allpaths = pathbuf;
		strcpy(pathbuf, envpaths);
		const char *curpath;
		while ((curpath = strsep(&allpaths, ":")) &&
		       strlen(curpath) && !fin) {
			char curname [strlen(curpath) + strlen(pathname) + 2];
			char *p = stpcpy(curname, curpath);
			*p++ = '/';
			strcpy(p, pathname);
			fin = fopen(curname, "r");
		}
		if (!fin) {
			/*
			 * We can't use pathbuf in this error message since
			 * it has been modified by the calls to strsep().
			 */
			fprintf(stderr, "Include: No such file: %s in %s\n",
				pathname, envpaths);
			return -1;
		}

	} else {
		fin = fopen(pathname, "r");
		if (!fin) {
			fprintf(stderr, "Include: %s: %s\n",
				pathname, strerror(errno));
			return -1;
		}
	}

	slc_yyscan_t scanner;
	slc_yylex_init(&scanner);
	slc_yyrestart(fin, scanner);
	int retval = 0;
	if (slc_yyparse(scanner)) {
		fprintf(stderr, "File include failed (parse error): %s\n",
			pathname);
		retval = -1;
	}
	slc_yylex_destroy(scanner);
	fclose(fin);
	return retval;
}

int parse_stdin(void)
{
	slc_yyscan_t scanner;
	slc_yylex_init(&scanner);
	slc_yyrestart(stdin, scanner);
	int retval = slc_yyparse(scanner);
	slc_yylex_destroy(scanner);
	return retval;
}
