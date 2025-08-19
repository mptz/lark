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
#include "repl.h"
#include "sourcefile.h"
#include "stmt.h"

#include "mlc.tab.h"
#include "mlc.lex.h"

/*
 * Library-level data, unlike per-file data, is currently stored in
 * global static data structures since we don't interleave library
 * reading--there is only ever one library being processed at once.
 */
static struct circlist run_queue, blocked_queue, complete_queue;
static struct wordtab sections_available;
struct sourcefile *the_current_sourcefile;
static int library_initialized;

void library_init(void)
{
	assert(!library_initialized);
	circlist_init(&run_queue);
	circlist_init(&blocked_queue);
	circlist_init(&complete_queue);
	wordtab_init(&sections_available, 50 /* size hint */);
	library_initialized = 1;
}

static void free_queue(struct circlist *queue)
{
	for (struct sourcefile *sf;
	     (sf = node_of(struct sourcefile, circlist_remove_head(queue)));
	     sourcefile_fini(sf), xfree(sf));
}

void library_fini(void)
{
	assert(library_initialized);
	free_queue(&run_queue);
	free_queue(&blocked_queue);
	free_queue(&complete_queue);
	wordtab_fini(&sections_available);
	library_initialized = 0;
}

/*
 * Complete a section.  This may unblock other files which require the
 * section we've just completed.
 */
static void complete_section(struct sourcefile *sf)
{
	/*
	 * This will be the case if we're at the beginning of the file
	 * and haven't started a section yet.  Also don't complete the
	 * section in the REPL since we keep re-using the sourcefile
	 * with a single section/namespace.
	 */
	if (sf->namespace == the_empty_symbol || sf->library == repl_lib_id)
		return;

	/*
	 * Remove any local (section-scoped) requirements.
	 */
	for (size_t i = wordbuf_used(&sf->locals); i; /* nada */)
		wordtab_rub(&sf->namespaces, wordbuf_at(&sf->locals, --i));
	wordbuf_clear(&sf->locals);

	/*
	 * Once we mark the section available, unblock any other
	 * sourcefile that was blocked on its availability by moving
	 * it from the blocked queue to the run queue.
	 */
	wordtab_set(&sections_available, sf->namespace);
	struct circlist *curr = blocked_queue.next;
	while (curr != &blocked_queue) {
		struct sourcefile *waiter = node_of(struct sourcefile, curr);
		curr = curr->next;
		if (waiter->requirement == sf->namespace) {
			waiter->requirement = the_empty_symbol;
			circlist_remove(&waiter->entry);
			circlist_add_tail(&run_queue, &waiter->entry);
		}
	}
	sf->namespace = the_empty_symbol;
}

static int publish_section(struct sourcefile *sf, symbol_mt section, int line)
{
	if (sf->namespace == the_empty_symbol) {
		errf("[#%s]:%s:%d: No namespace yet, can't publish #%s\n",
		     symtab_lookup(sf->library), symtab_lookup(sf->filename),
		     line, symtab_lookup(section));
		return -1;
	}

	/*
	 * We can only publish sections from our own library, or which
	 * become available as a result of being published by another
	 * library.  If we try to publish a section which never becomes
	 * available, we'll never unblock and library loading will fail.
	 * This ensures that library sections can't be published against
	 * the will of the library's author.
	 *
	 * It may seem that publishing via env_publish() before checking
	 * availability is premature, but publishing only takes effect
	 * for completed sections, and if we block forever the current
	 * section will never be completed.
	 */
	assert(section != the_empty_symbol);
	env_publish(sf->namespace, section);
	if (wordtab_test(&sections_available, section))
		return 0;
	sf->requirement = section;
	sf->line = line;
	circlist_add_head(&blocked_queue, &sf->entry);
	return 1;
}

/*
 * Requiring an external (other-library) section adds all namespaces
 * that section *publishes* rather than the namespace of the section
 * itself.  This gives libraries control over their exports.  This
 * process can be recursive--libraries can publish sections from other
 * libraries, but only via the publish mechanism.
 */
static void require_gather(struct sourcefile *sf, symbol_mt section, int line)
{
	struct wordbuf pubs, reqs, spaces;

	wordbuf_init(&pubs);
	wordbuf_init(&reqs);
	wordbuf_init(&spaces);

	/*
	 * The initial pair is the current library (library of the current
	 * sourcefile) and the section being required.  Before entering
	 * this function we confirm this section is available, i.e. we
	 * won't block on it.  From this point onward we only enqueue
	 * published sections--if they don't exist it's a fatal error.
	 */
	wordbuf_push(&reqs, sf->library);
	wordbuf_push(&reqs, section);

	for (size_t i = 0; i < wordbuf_used(&reqs); i += 2) {
		assert(i % 2 == 0);	/* 'reqs' contains pairs */
		symbol_mt lib = wordbuf_at(&reqs, i + 0),
			  req = wordbuf_at(&reqs, i + 1);
		symbol_mt whose = env_whose(req);

		/*
		 * Can this happen?  I don't believe so.  We're only
		 * queuing published sections from other libraries,
		 * which by definition are complete due to serialized
		 * library processing.
		 */
		assert(whose != the_empty_symbol);

		/*
		 * There shouldn't be any way of circling back to the
		 * current library.  Such a definition would have to
		 * appear in an earlier library, but reference this
		 * one--such a dangling reference would have caused
		 * the earlier library's definition to fail.
		 */
		assert(whose != sf->library);

		/*
		 * If the library is publishing its own section, just
		 * add it to the list of namespaces to open.  Any
		 * library is allowed to expose its own sections.
		 */
		if (whose == lib) {
			wordbuf_push(&spaces, req);
			continue;
		}

		/*
		 * Otherwise the section is external to the publishing
		 * library, so we gather its publishes for expansion.
		 * If there aren't any, warn--possibly a mistake.
		 */
		env_published(req, &pubs);
		size_t b = wordbuf_used(&pubs);
		if (!b)
			warnf("[#%s]:%s:%d: Required external section "
			      "#%s publishes nothing\n",
				symtab_lookup(sf->library),
				symtab_lookup(sf->filename), line,
				symtab_lookup(req));
		for (size_t j = 0; j < b; ++j) {
			wordbuf_push(&reqs, whose);
			wordbuf_push(&reqs, wordbuf_at(&pubs, j));
		}
		wordbuf_clear(&pubs);
	}
	wordbuf_fini(&pubs);
	wordbuf_fini(&reqs);

	/*
	 * Now open all the namespaces we've gathered, and record them
	 * as local if they weren't already open.  Local requirements
	 * will be closed on exit from the current section.
	 */
	for (size_t i = 0, b = wordbuf_used(&spaces); i < b; ++i) {
		symbol_mt id = wordbuf_at(&spaces, i);
		if (wordtab_test(&sf->namespaces, id)) continue;
		wordtab_set(&sf->namespaces, id);
		if (sf->namespace != the_empty_symbol)
			wordbuf_push(&sf->locals, id);
	}
	wordbuf_fini(&spaces);
}

/*
 * Require a section.  Behavior hinges on whether the section is from
 * another library (external) or from this one.  Library processing is
 * serialized, so external sections are fully complete; if a section
 * doesn't exist yet it must be from elsewhere in *this* library.  In
 * that case we return 1 to indicate we need to block.  Even when we
 * block, we add the section to this sourcefile's active namepaces
 * first--this is safe since resolution of this sourcefile won't
 * proceed until it's available.
 *
 * Return 0 on success (no need to block) and 1 when blocked.
 */
static int require_section(struct sourcefile *sf, symbol_mt section, int line)
{
	/*
	 * If the section is external (from another library) requiring
	 * it can't fail and can't block... finish that here.
	 */
	symbol_mt whose = env_whose(section);
	if (whose != the_empty_symbol && whose != sf->library) {
		require_gather(sf, section, line);
		return 0;
	}

	/*
	 * Set the namespace; if not already set and if this is a local
	 * requirement (associated with a section rather than at top of
	 * file) add it to our list of locals so we can clear it at end
	 * of section.
	 */
	if (!wordtab_test(&sf->namespaces, section)) {
		wordtab_set(&sf->namespaces, section);
		if (sf->namespace != the_empty_symbol)
			wordbuf_push(&sf->locals, section);
	}

	if (wordtab_test(&sections_available, section))
		return 0;
	sf->requirement = section;
	sf->line = line;
	circlist_add_head(&blocked_queue, &sf->entry);
	return 1;
}

void library_queue(struct sourcefile *sf)
{
	assert(sf->requirement == the_empty_symbol);
	circlist_add_tail(&run_queue, &sf->entry);
}

struct sourcefile *library_recycle(void)
{
	assert(circlist_is_inhabited(&complete_queue));
	struct sourcefile *sf = node_of(struct sourcefile,
					circlist_remove_head(&complete_queue));
	assert(circlist_is_empty(&complete_queue));
	return sf;
}

static int handle_marker(struct sourcefile *sf, const struct stmt *stmt)
{
	assert(stmt->variety == STMT_MARKER);
	const enum marker_variety variety = stmt->marker.variety;
	const symbol_mt huid = stmt->marker.huid;

	switch (variety) {
	case MARKER_INSPECT:
		wordtab_set(&sf->namespaces, huid);
		break;
	case MARKER_PUBLISH:
		return publish_section(sf, huid, stmt->line0);
	case MARKER_REQUIRE:
		return require_section(sf, huid, stmt->line0);
	case MARKER_SECTION:
		complete_section(sf);
		if (sourcefile_begin(sf, huid))
			return -1;
		break;
	default: panicf("Unimplemented marker variety %d\n", variety);
	}
	return 0;
}

int library_resolve(void)
{
	struct sourcefile *sf;

	while ((sf = node_of(struct sourcefile,
			     circlist_remove_head(&run_queue)))) {
		the_current_sourcefile = sf;
		int blocked = 0;
		while (!blocked && sf->pos < sf->bound) {
			struct stmt *stmt = (struct stmt *)
				wordbuf_at(&sf->contents, sf->pos++);
			/*
			 * We directly handle marker statements since
			 * they involve blocking & library handling,
			 * but delegate the rest to generic statement
			 * evaluation.
			 */
			if (stmt->variety == STMT_MARKER) {
				blocked = handle_marker(sf, stmt);
				if (blocked < 0)
					return -1;
			} else {
				int retval = stmt_eval(stmt);
				if (retval) {
					errf("[#%s]:%s:%d-%d: "
					     "Invalid statement\n",
					     symtab_lookup(sf->library),
					     symtab_lookup(sf->filename),
					     stmt->line0, stmt->line1);
					return -1;
				}
			}
		}

		/*
		 * If we exited without being blocked, we're done with
		 * this sourcefile; complete its final section and add
		 * add it to the complete queue.
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
	assert(circlist_is_empty(&run_queue));
	if (circlist_is_inhabited(&blocked_queue)) {
		err("Stalled: source files have unmet requirements, "
		    "but no files available:\n");
		struct circlist_iter iter;
		for (circlist_iter_init(&blocked_queue, &iter);
		     (sf = node_of(struct sourcefile,
				   circlist_iter_next(&iter)));
		     /* nada */)
			errf("[#%s]:%s:%d requires #%s\n",
			     symtab_lookup(sf->library),
			     symtab_lookup(sf->filename), sf->line,
			     symtab_lookup(sf->requirement));
		return -1;
	}
	return 0;
}
