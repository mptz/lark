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

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <util/memutil.h>
#include <util/message.h>
#include <util/sort.h>
#include <util/symtab.h>
#include <util/wordtab.h>

#include "env.h"
#include "form.h"
#include "library.h"
#include "sourcefile.h"
#include "stmt.h"

#include "mlc.tab.h"
#include "mlc.lex.h"

/*
 * Library-level data, unlike per-file data, is currently stored in
 * global static data structures since we don't interleave library
 * reading--there is only every one library being processed at once.
 */
static struct circlist resolve_queue, require_queue, complete_queue;
static struct wordtab sections_available;
static const char *libpath, *libname;
struct sourcefile *the_current_sourcefile;

static int library_filter(const struct dirent *dirent)
{
	return !fnmatch("*.mlc", dirent->d_name, FNM_PERIOD);
}

/*
 * We don't duplicate the library name since the lifetime of library
 * information is scoped entirely within library_load().
 */
static void library_init(const char *name)
{
	circlist_init(&resolve_queue);
	circlist_init(&require_queue);
	circlist_init(&complete_queue);
	wordtab_init(&sections_available, 50 /* size hint */);
	libname = name;
}

static void free_queue(struct circlist *queue)
{
	struct sourcefile *sf;
	while ((sf = node_of(struct sourcefile, circlist_remove_head(queue))))
		sourcefile_fini(sf), xfree(sf);
}

static void library_fini(void)
{
	free_queue(&resolve_queue);
	free_queue(&require_queue);
	free_queue(&complete_queue);
	wordtab_fini(&sections_available);
	xfree(libpath), libpath = NULL;
}

/*
 * Find a library by its name, checking all paths in the colon-separated
 * MLCLIB environment variable.  Return an open file descriptor of the
 * library's directory, or -1 on error.
 */
static int library_find(void)
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
		 * which may legitimately arise from path concatenenation.
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
		if (libfd >= 0) libpath = xstrdup(curpath);

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

static int library_read_stream(const char *filename, FILE *input)
{
	/*
	 * Sourcefiles are added to a queue immediately after creation
	 * so are automatically freed (eventually).
	 */
	struct sourcefile *sf = xmalloc(sizeof *sf);
	sourcefile_init(sf, filename);
	circlist_add_tail(&resolve_queue, &sf->entry);

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
	if (status) errf("Abandoning parse of %s\n", sf->filename);
	sf->bound = wordbuf_used(&sf->contents);

	mlc_yypstate_delete(parser);
	mlc_scan_fini(&scanner);
	return status;
}

static int library_read_file(const char *pathname)
{
	/* XXX doesn't handle "-" as an input filename convention */
	FILE *input = fopen(pathname, "r");
	if (!input) {
		perr(pathname);
		return -1;
	}
	int status = library_read_stream(pathname, input);
	if (input != stdin) fclose(input);
	return status;
}

static int library_read(int libfd)
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
		perrf("%s/%s", libpath, libname);
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
			perrf("%s/%s/%s", libpath, libname, filename);
			goto library_read_fatal;
		}
		input = fdopen(srcfd, "r");
		assert(input);
		int status = library_read_stream(filename, input);
		if (input != stdin) fclose(input);
		input = NULL;
		if (status) goto library_read_fatal;
	}
	retval = 0;	/* success! */

library_read_fatal:
	if (input) fclose(input);
	xfree(namelist);
	return retval;
}

/*
 * Complete a named section if one is open.  This may unblock other files
 * which require the section we've just completed.  For each file in the
 * require queue whose requirement is this section, move it to the resolve
 * queue (i.e. unblock it).
 */
static void complete_section(struct sourcefile *sf)
{
	if (sf->section == the_empty_symbol)
		return;

	wordtab_set(&sections_available, sf->section);
	if (sf->public) env_public(sf->section), sf->public = false;
	struct circlist *curr = require_queue.next;
	while (curr != &require_queue) {
		struct sourcefile *waiter = node_of(struct sourcefile, curr);
		curr = curr->next;
		if (waiter->requirement == sf->section) {
			waiter->requirement = the_empty_symbol;
			circlist_remove(&waiter->entry);
			circlist_add_tail(&resolve_queue, &waiter->entry);
		}
	}
	sf->section = the_empty_symbol;
}

/*
 * Require a section.  If the section is already available, just proceed.
 * Sections may be public (in the global environment) or may already be
 * available from the current library.
 *
 * Otherwise add the sourcefile to the require queue, blocking it until
 * the required section becomes available.  The given sourcefile should
 * not already be on a queue.
 *
 * Either way, it's safe to add the section to the sourcefile's active
 * namespaces since resolution won't proceed until it's available.
 *
 * Return 0 on success (no need to block) and 1 when blocked.
 */
static int require_section(struct sourcefile *sf, symbol_mt section, int line)
{
	wordtab_set(&sf->namespaces, section);
	if (env_is_public(section) ||
	    wordtab_test(&sections_available, section))
		return 0;
	sf->requirement = section;
	sf->line = line;
	circlist_add_head(&require_queue, &sf->entry);
	return 1;
}

static int library_resolve(void)
{
	struct sourcefile *sf;

	while ((sf = node_of(struct sourcefile,
			     circlist_remove_head(&resolve_queue)))) {
		the_current_sourcefile = sf;
		int blocked = 0;
		while (!blocked && sf->pos < sf->bound) {
			struct stmt *stmt = (struct stmt *)
				wordbuf_at(&sf->contents, sf->pos++);
			/*
			 * We directly handle statements which involve
			 * blocking & library handling, and delegate
			 * the rest to generic statement evaluation.
			 */
			switch (stmt->variety) {
			case STMT_INSPECT:
				wordtab_set(&sf->namespaces, stmt->sym);
				break;
			case STMT_PUBLIC:
				sf->public = true;
				break;
			case STMT_REQUIRE:
				blocked = require_section(
					sf, stmt->sym, stmt->line0);
				break;
			case STMT_SECTION:
				complete_section(sf);
				if (sourcefile_begin(sf, stmt->sym))
					return -1;
				break;
			default:
				stmt_eval(stmt);
				break;
			}
		}

		/*
		 * If we exited without being blocked, we're done with
		 * this sourcefile; complete its final section if
		 * necessary and add it to the complete queue.
		 */
		if (!blocked) {
			complete_section(sf);
			circlist_add_tail(&complete_queue, &sf->entry);
		}
		the_current_sourcefile = NULL;
	}

	/*
	 * If anything is on the require queue, we're deadlocked since
	 * no sourcefiles are runnable.
	 */
	assert(circlist_is_empty(&resolve_queue));
	if (circlist_is_inhabited(&require_queue)) {
		err("Stalled: source files have unmet requirements, "
		    "but no files available:\n");
		struct circlist_iter iter;
		for (circlist_iter_init(&require_queue, &iter);
		     (sf = node_of(struct sourcefile,
				   circlist_iter_next(&iter)));
		     /* nada */)
			errf("%s:%d requires %s\n", sf->filename, sf->line,
				symtab_lookup(sf->requirement));
		return -1;
	}
	return 0;
}

int library_load(const char *name)
{
	int retval = -1;

	library_init(name);
	int libfd = library_find();
	if (libfd < 0) goto done;
	retval = library_read(libfd) || library_resolve();
	close(libfd);
done:
	library_fini();
	return retval;
}

int library_load_files(int nnames, char *const names[])
{
	int retval = -1;

	library_init("<arguments>");
	for (int i = 0; i < nnames; ++i)
		if (library_read_file(names[i]))
			goto done;
	retval = library_resolve();
done:
	library_fini();
	return retval;
}

int library_read_stdin(void)
{
	int retval = -1;

	const char *filename = "<standard input>";
	library_init(filename);
	retval = library_read_stream(filename, stdin) || library_resolve();
	library_fini();
	return retval;
}

/*
 * The REPL functions make somewhat unusual use of the resolve & complete
 * queues.  We initially put the sole sourcefile used for resolution in
 * the complete queue.  Before processing each line, we move it to the
 * resolve queue--resolution will move it back to the complete queue.
 *
 * For convenience, pull all public sections into the REPL sourcefile.
 */

int library_repl_init(void)
{
	library_init("<REPL>");
	struct sourcefile *sf = xmalloc(sizeof *sf);
	sourcefile_init(sf, "<REPL>");
	circlist_add_tail(&complete_queue, &sf->entry);
	env_get_public(&sf->namespaces);
	return 0;
}

int library_repl_fini(void)
{
	assert(circlist_is_empty(&resolve_queue));
	assert(circlist_length(&complete_queue) == 1);
	free_queue(&complete_queue);
	library_fini();
	return 0;
}

int library_repl_line(const char *line, int lineno)
{
	if (circlist_is_empty(&resolve_queue)) {
		assert(circlist_length(&complete_queue) == 1);
		circlist_add_head(&resolve_queue,
				  circlist_remove_head(&complete_queue));
	}
	assert(circlist_length(&resolve_queue) == 1);
	struct sourcefile *sf = node_of(struct sourcefile,
					circlist_get_head(&resolve_queue));

	struct scanner_state scanner;
	mlc_yypstate *parser;

	mlc_scan_init_raw(&scanner);
	mlc_scan_string(line, &scanner, lineno);
	parser = mlc_yypstate_new();
	assert(parser);

	int status;
	do {
		YYSTYPE val;
		YYLTYPE loc;
		int token = mlc_yylex(&val, &loc, scanner.flexstate);
		status = mlc_yypush_parse(parser, token, &val, &loc, sf);
	} while (status == YYPUSH_MORE);
	sf->bound = wordbuf_used(&sf->contents);

	mlc_yypstate_delete(parser);
	mlc_scan_fini(&scanner);

	the_current_sourcefile = sf;
	if (status == 0)
		status = library_resolve();
	the_current_sourcefile = NULL;
	return status;
}
