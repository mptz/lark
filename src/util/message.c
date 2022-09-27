/*
 * Copyright (c) 2009-2019 Michael P. Touloumtzis.
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
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * This should actually depend on whether we're building against glibc
 * or not.
 */
#ifdef __linux__
#define HAVE_BACKTRACE
#include <execinfo.h>
#endif

#include <util/message.h>

static void print_backtrace(void);

const char *execname = "[UNNAMED]";
const char *global_error_prefix = "";
int failure_exit_code = EXIT_FAILURE;
unsigned global_message_threshold = 20;		/* errors and warnings */
const char *unreachable_message = "Should never get here!\n";

void set_execname(const char *execpath)
{
	const char *p = strrchr(execpath, '/');
	execname = p ? ++p : execpath;
}

int xperror(const char *context)
{
	int err = -errno;
	fprintf(stderr, "%s: %s: %s\n", execname, context, strerror(-err));
	return err;
}

int
msg_real(unsigned level, const char *file, const char *function, int line,
	const char *message)
{
	return msgf_real(level, file, function, line, "%s", message);
}

#define MSGLEVEL_MASK 0xFFFF

int
msgf_real(unsigned level, const char *file, const char *function, int line,
	const char *format, ...)
{
	va_list ap;

	/* save errno away before it's accidentally changed */
	int err = errno;

	if ((level & MSGLEVEL_MASK) > global_message_threshold)
		return 0;

	/*
	 * This could be tightened up... a previous version of this file
	 * had much more functionality (which sat unused).
	 */
	const char *typename;
	int print_location = 1;	/* XXX may want to disable for info? */
	switch (level & MSGLEVEL_MASK) {
	case MSGLEVEL_ERR:
		typename = "ERROR";
		break;
	case MSGLEVEL_WARN:
		typename = "warning";
		break;
	case MSGLEVEL_INFO:
		typename = "info";
		break;
	case MSGLEVEL_TRACE:
	default:
		typename = "trace";
		break;
	}

	fprintf(stderr, "%s: ", execname);
	if (print_location)
		fprintf(stderr, "%s: %s (%d): ", file, function, line);
	fprintf(stderr, "%s: ", typename);
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);

	if (level & MSGFLAG_PERROR)
		fprintf(stderr, ": %s\n", strerror(err));
	if (level & MSGFLAG_BACKTRACE)
		print_backtrace();

	fflush(stderr);
	return 0;
}

void
panic_real(const char *file, const char *function, int line,
	   const char *message)
{
	fprintf(stderr, "%s: %s in %s (%d): PANIC: %s\n", execname,
		file, function, line, message);
	print_backtrace();
	exit(failure_exit_code);
}

void
ppanic_real(const char *file, const char *function, int line,
	    const char *errstr)
{
	fprintf(stderr, "%s: %s in %s (%d): PANIC: %s: %s\n",
		execname, file, function, line, errstr, strerror(errno));
	print_backtrace();
	exit(failure_exit_code);
}

void
panicf_real(const char *file, const char *function, int line,
	    const char *format, ...)
{
	va_list ap;

	fprintf(stderr, "%s: %s in %s (%d): PANIC: ", execname,
		file, function, line);
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	print_backtrace();
	exit(failure_exit_code);
}

static void
print_backtrace(void)
{
	fputs("Backtrace:\n", stderr);
#ifdef HAVE_BACKTRACE
	void *frames [200];	/* glibc info page suggests 200 */
	int nframes = backtrace(frames, sizeof frames / sizeof *frames);
	fflush(stderr);
	backtrace_symbols_fd(frames, nframes, STDERR_FILENO);
#else
	fputs("[Backtrace unavailable]\n", stderr);
#endif
}
