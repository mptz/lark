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

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <regex.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <util/fdutil.h>
#include <util/huid.h>
#include <util/huidrand.h>
#include <util/memutil.h>
#include <util/message.h>
#include <util/sort.h>

#include "libload.h"
#include "library.h"
#include "sourcefile.h"

#include "mlc.tab.h"
#include "mlc.lex.h"

static int library_filter(const struct dirent *dirent)
{
	return !fnmatch("*.mlc", dirent->d_name, FNM_PERIOD);
}

/*
 * Find a library by its name, checking all paths in the colon-separated
 * MLCLIB environment variable.  Return an open file descriptor of the
 * library's directory, or -1 on error.
 */
static int library_find(const char *libname)
{
	char *envpaths = getenv("MLCLIB") ? : "/usr/lib/mlc",
	     *pathbuf = xmalloc(strlen(envpaths) + 1),
	     *allpaths = pathbuf;
	int dirfd = -1, libfd = -1;

	strcpy(pathbuf, envpaths);
	const char *curpath;
	while ((curpath = strsep(&allpaths, ":")) && libfd < 0) {
		/*
		 * Silently ignore empty paths e.g. "lib/one::/lib/two"
		 * which may legitimately arise from path concatenation.
		 */
		if (!strlen(curpath))
			continue;

		dirfd = open(curpath, O_DIRECTORY | O_PATH);
		/*
		 * If a directory in the library paths list doesn't
		 * exist, silently ignore it.  Otherwise we error.
		 */
		if (dirfd < 0 && errno == ENOENT)
			goto try_next_path;
		if (dirfd < 0) {
			fprintf(stderr, "Directory '%s' from library path "
				"('%s'): %s\n", curpath, envpaths,
				strerror(errno));
			goto library_find_fatal;
		}

		/*
		 * Try opening the library relative to the library path.
		 * Again, if not found we silently continue.  If libname
		 * is an absolute (vs relative) path, openat ignores dirfd
		 * so the library, if it exists, will be found no matter
		 * from which library directory we first inspect.
		 */ 
		libfd = openat(dirfd, libname, O_DIRECTORY | O_PATH);
		if (libfd < 0 && errno == ENOENT)
			goto try_next_path;
		if (libfd < 0) {
			fprintf(stderr, "%s/%s: %s\n", curpath, libname,
				strerror(errno));
			goto library_find_fatal;
		}

	try_next_path:
		if (dirfd >= 0) close(dirfd), dirfd = -1;
	}

	/*
	 * If we exited the loop normally without successfully opening
	 * a library directory, it's because we never found the named
	 * library in any of the configured library paths.
	 */
	if (libfd < 0)
		fprintf(stderr, "Library '%s' not found in library paths: "
				"'%s'\n", libname, envpaths);

library_find_fatal:
	if (dirfd >= 0) close(dirfd);
	xfree(pathbuf);
	return libfd;
}

static regex_t *huid_pattern(void)
{
	static regex_t regexp;
	bool initialized = false;
	if (initialized) return &regexp;

	const char *pattern =	"[A-Za-z0-9_-]{8}\\."
				"[A-Za-z0-9_-]{8}\\."
				"[A-Za-z0-9_-]{8}";
	int errcode = regcomp(&regexp, pattern, REG_EXTENDED | REG_NOSUB);
	assert(!errcode);
	initialized = true;
	return &regexp;
}

#define HBUFSIZE 80	/* more than big enough */

static symbol_mt library_id(const char *name, int libfd)
{
	symbol_mt retval = the_empty_symbol;

	/*
	 * Open the library's HUID file and verify it's plausible.
	 */
	int huidfd = openat(libfd, "HUID", O_RDONLY);
	if (huidfd < 0) {
		perrf("%s/HUID", name);
		goto fail0;
	}
	struct stat stat;
	if (fstat(huidfd, &stat)) {
		perrf("%s/HUID", name);
		goto fail1;
	}
	if (stat.st_size < HUID_CHARS + 1) {
		errf("%s/HUID too small to contain HUID + newline\n", name);
		goto fail1;
	}
	if (stat.st_size >= HBUFSIZE) {
		errf("%s/HUID too large to contain HUID + newline\n", name);
		goto fail1;
	}

	/*
	 * Read the HUID file and trim trailing whitespace.
	 */
	char buf [HBUFSIZE];
	if (r_readall(huidfd, buf, stat.st_size)) {
		perrf("%s/HUID", name);
		goto fail1;
	}
	buf [stat.st_size] = '\0';
	for (char *ptr = buf + stat.st_size;
	     ptr > buf && isspace(*--ptr);
	     *ptr = '\0');

	/*
	 * Match the buffer against a HUID regexp for final confirmation.
	 */
	int code = regexec(huid_pattern(), buf, 0, NULL, 0);
	if (code) {
		errf("%s/HUID not a bare HUID + newline: '%s'\n",
			name, buf);
		goto fail1;
	}
	retval = symtab_intern(buf);
fail1:
	close(huidfd);
fail0:
	return retval;
}

static int library_read_stream(symbol_mt lib, symbol_mt filename,
			       FILE *input)
{
	/*
	 * Sourcefiles are added to a queue immediately after creation
	 * so are automatically freed (eventually).
	 */
	struct sourcefile *sf = xmalloc(sizeof *sf);
	sourcefile_init(sf, lib, filename);
	library_queue(sf);

	struct scanner_state scanner;
	mlc_yypstate *parser;

	mlc_scan_init_input(&scanner, input);
	parser = mlc_yypstate_new();
	assert(parser);

	int status;
	do {
		YYSTYPE val;
		YYLTYPE loc;
		int token = mlc_yylex(&val, &loc, scanner.flexstate);
		status = mlc_yypush_parse(parser, token, &val, &loc, sf);
	} while (status == YYPUSH_MORE);
	if (status)
		errf("Abandoning parse of '%s:%s'\n",
		     symtab_lookup(lib), symtab_lookup(filename));
	sf->bound = wordbuf_used(&sf->contents);

	mlc_yypstate_delete(parser);
	mlc_scan_fini(&scanner);
	return status;
}

static int library_read(const char *libname, int libfd, symbol_mt lib)
{
	struct dirent **namelist = NULL;
	FILE *input = NULL;
	int retval = -1;

	/*
	 * Scan the library directory looking for *.mlc source files.
	 */
	int nentries = scandirat(libfd, ".", &namelist,
				 library_filter, asciisort);
	if (nentries < 0) {
		perrf("%s", libname);
		return -1;
	}

	/*
	 * Open and parse each source file.
	 */
	int i;
	for (i = 0; i < nentries; ++i) {
		const char *filename = namelist[i]->d_name;
		int srcfd = openat(libfd, filename, O_RDONLY);
		if (srcfd < 0) {
			perrf("%s/%s", libname, filename);
			goto library_read_fatal;
		}
		input = fdopen(srcfd, "r");
		assert(input);
		int status = library_read_stream(lib,
			symtab_intern(filename), input);
		if (input != stdin) fclose(input);
		input = NULL;
		if (status) {
			errf("Failed to read '%s:%s'\n", libname, filename);
			goto library_read_fatal;
		}
	}
	retval = 0;	/* success! */

library_read_fatal:
	if (input) fclose(input);
	xfree(namelist);
	return retval;
}

int library_load(const char *name)
{
	int libfd, retval = -1;
	symbol_mt lib;

	library_init();
	if ((libfd = library_find(name)) < 0)
		goto fail0;
	if ((lib = library_id(name, libfd)) == the_empty_symbol ||
	    (retval = library_read(name, libfd, lib)))
		goto fail1;
	retval = library_resolve();
fail1:
	close(libfd);
fail0:
	library_fini();
	return retval;
}

int library_load_files(int nnames, char *const names[])
{
	int retval = -1;

	/* random library ID for command line arguments */
	char huid [HUID_STR];
	huid_fresh_str(huid, sizeof huid);
	symbol_mt lib = symtab_intern(huid);

	library_init();
	for (int i = 0; i < nnames; ++i) {
		FILE *input = strcmp(names[i], "-") ?
			fopen(names[i], "r") : stdin;
		if (!input) {
			perr(names[i]);
			retval = -1;
			goto fail0;
		}
		retval = library_read_stream(lib,
			symtab_intern(names[i]), input);
		if (input != stdin) fclose(input);
		if (retval) {
			errf("Failed to read '%s'\n", names[i]);
			goto fail0;
		}
	}
	retval = library_resolve();
fail0:
	library_fini();
	return retval;
}
