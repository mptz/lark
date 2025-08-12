/*
 * Copyright (c) 2001-2024 Michael P. Touloumtzis.
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
#include <fcntl.h>
#include <getopt.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <sys/poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fdutil.h"
#include "memutil.h"
#include "message.h"
#include "timeutil.h"

static volatile sig_atomic_t child_exited = 0;

static void sighandler(int signum)
{
	child_exited = 1;
}

static void kill_child_group(pid_t childpid, time_t kill_time)
{
	warnf("Forcing exit of child %u\n", childpid);

	/*
	 * Ignore SIGTERM ourself, then send one to our process group.
	 * This is more comprehensive than just signaling the child,
	 * which may have spawned its own subprocess tree.
	 */
	struct sigaction ignore_action, prior_action;
	memset(&ignore_action, 0, sizeof ignore_action);
	ignore_action.sa_handler = SIG_IGN;
	sigaction(SIGTERM, &ignore_action, &prior_action);
	kill(0, SIGTERM);
	sigaction(SIGTERM, &prior_action, NULL);

	/*
	 * Before waiting for the child's status, we deal with the fact
	 * that it might have blocked/handled SIGTERM and not exited.
	 * Set an alarm to interrupt ourselves after the specified kill
	 * timeout, and terminate with prejudice if we're so awoken.
	 */
	struct sigaction alarm_action;
	memset(&alarm_action, 0, sizeof alarm_action);
	alarm_action.sa_handler = sighandler;
	sigaction(SIGALRM, &alarm_action, NULL);
	alarm(kill_time);
	if (waitpid(childpid, NULL, 0) == -1 && errno == EINTR) {
		warnf("Killing child %u after %d-second kill timeout\n",
			childpid, kill_time);
		fflush(NULL);
		kill(0, SIGKILL);
	}

	/*
	 * From sysexits.h: "A service is unavailable.  This can occur
	 * if a support program or file does not exist.  This can also
	 * be used as a catchall message when something you wanted to
	 * do doesn't work, but you don't know why."
	 */
	exit(EX_UNAVAILABLE);
}

static int handle_child_status(int status)
{
	if (WIFEXITED(status)) {
		/*
		 * Normal child exit, which we replicate.  We warn if the
		 * child process failed.
		 */
		int exit_status = WEXITSTATUS(status);
		if (exit_status)
			warnf("Child exited with status %d\n", exit_status);
		return exit_status;
	}
	if (WIFSIGNALED(status)) {
		/*
		 * Child exited with a signal; first warn about this fact,
		 * then try to exit the same way by signaling ourselves.
		 *
		 * If we're still alive after signaling ourselves, exit
		 * with an EX_OSERR exit code (exit codes are very weakly
		 * standardized, bordering on not standardized at all,
		 * but this seems like the least-bad choice).
		 */
		int termsig = WTERMSIG(status);
		warnf("Child exited with signal %d, signaling self\n", termsig);
		fflush(NULL);
		kill(getpid(), termsig);
		return EX_OSERR;
	}

	/*
	 * Use EX_SOFTWARE if we get here since this is a "should never
	 * happen" condition.
	 */
	warnf("Child exited with unrecognized status %d\n", status);
	return EX_SOFTWARE;
}

static int match_copy(int src, int dst, const regex_t *regexp)
{
	const size_t bufsize = 16 * 1024;
	char buf [bufsize + 1];
	int matched = 0;

	while (1) {
		ssize_t nread = r_read(src, buf, bufsize);
		if (nread < 0 && errno != EAGAIN) return -1;
		if (nread <= 0) return matched;

		buf[nread] = '\0';	/* assumes no embedded NULs */
		int code = regexec(regexp, buf, 0, NULL, REG_NOTBOL|REG_NOTEOL);
		if (code == 0) matched = 1;
		else if (code != REG_NOMATCH) return -1;

		if (r_writeall(dst, buf, nread) < 0)
			return -1;
	}
}

static void usage(void)
{
	fprintf(stderr,
		"Output Watchdog: kill a subprocess which stops "
		"producing output\n"
		"Runs a child process and kills it after a timeout interval.\n"
		"Writes to standard output or standard error reset the timer.\n"
		"Optionally requires a pattern written to stderr to reset.\n"
		"Usage: %s <options> -- command arg1 arg2 ...\n"
		"Timer options:\n"
		"	-d <seconds>	Delay starting the child.  "
					"Helps avoid restart thrashing.\n"
		"	-k <seconds>	Interval between SIGTERM and SIGKILL, "
					"if former is ineffective.\n"
		"			(default: 30 seconds)\n"
		"	-t <seconds>	Watchdog timer for standard output "
								"& error\n"
		"			(default: 60 seconds)\n"
		"Other options:\n"
		"	-h		Print this message\n"
		"	-r <regexp>	Optional regular expression; matches "
					"on stderr reset timer.\n",
		execname);
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	int delay_time = 0, kill_time = 30, watchdog_time = 60;
	const char *pattern = NULL;

	set_execname(argv[0]);
	int option;
	while ((option = getopt(argc, argv, "hd:k:t:r:")) != -1) {
		char *endptr;
		switch (option) {
		case 'h':
		default:
			usage();
			break;
		case 'd':
			delay_time = strtol(optarg, &endptr, 10);
			if (*endptr) usage();
			break;
		case 'k':
			kill_time = strtol(optarg, &endptr, 10);
			if (*endptr) usage();
			break;
		case 't':
			watchdog_time = strtol(optarg, &endptr, 10);
			if (*endptr) usage();
			break;
		case 'r':
			pattern = optarg;
			break;
		}
	}

	if (optind == argc) {
		fprintf(stderr, "%s: Missing command to execute, "
			"showing help instead...\n", execname);
		usage();
	}

	/*
	 * Compile the regexp before forking in case there's an error.
	 */
	regex_t regexp;
	if (pattern) {
		const int flags = REG_EXTENDED | REG_NOSUB;
		int errcode = regcomp(&regexp, pattern, flags);
		if (errcode) {
			size_t size = regerror(errcode, &regexp, NULL, 0);
			char *buf = xmalloc(size);
			regerror(errcode, &regexp, buf, size);
			fprintf(stderr, "Regular expression error: %s\n", buf);
			free(buf);
			exit(EXIT_FAILURE);
		}
	}

	/*
	 * Predelay can be useful if we're run from some kind of session
	 * manager or daemon--a short predelay prevents pure thrashing
	 * on restart.
	 */
	if (delay_time) sleep(delay_time);

	/*
	 * Store original standard output & error file descriptors and
	 * set up the pipes we'll use for parent/child communication.
	 */
	fflush(NULL);
	int origout = -1, origerr = -1;
	if ((origout = dup(STDOUT_FILENO)) == -1) ppanic("dup stdin");
	if ((origerr = dup(STDERR_FILENO)) == -1) ppanic("dup stderr");
	int stdout_pipe [2], stderr_pipe [2];
	if (pipe(stdout_pipe) || pipe(stderr_pipe)) ppanic("pipe");

	/*
	 * Install a SIGCHLD hander so we'll know when the child exits.
	 * We're doing this before forking, but POSIX specifies that
	 * handled signals are reset to default behavior by execve(2).
	 */
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = sighandler;
	sigaction(SIGCHLD, &sa, NULL);

	/*
	 * Fork & exec.
	 */
	pid_t pid;
	if ((pid = fork()) == -1) ppanic("fork");

	if (pid == 0) {
		/*
		 * We're the child.
		 */
		dup2(stdout_pipe[1], STDOUT_FILENO);
		dup2(stderr_pipe[1], STDERR_FILENO);
		close(stdout_pipe[0]);
		close(stdout_pipe[1]);
		close(stderr_pipe[0]);
		close(stderr_pipe[1]);

		execvp(argv[optind], argv + optind);
		/*
		 * If we get here, the exec failed.  Restore the original
		 * standard error (and input, though it's not necessary)
		 * so we can properly panic.
		 */
		dup2(origout, STDOUT_FILENO);
		dup2(origerr, STDERR_FILENO);
		errf("Couldn't exec %s\n", argv[optind]);
		ppanic("exec");
	}

	/*
	 * We're the parent.  Set read ends of our pipes to nonblocking
	 * and close the write ends; set up pollfd structures to allow
	 * us to wait for I/O from the child.
	 */
	fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
	fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);
	close(stdout_pipe[1]);
	close(stderr_pipe[1]);
	struct pollfd pollfds [2] = {
		[0] = { .fd = stdout_pipe[0], .events = POLLIN },
		[1] = { .fd = stderr_pipe[0], .events = POLLIN },
	};

	uint64_t now = time_now(), deadline = now + 1000000 * watchdog_time;
	while (!child_exited) {
		int reset = 0,
		    timeout = now >= deadline ? 0 : (deadline - now) / 1000,
		    result = poll(pollfds, 2, timeout);

		if (result == 0) {
			warnf("Child %u timed out after %d seconds\n",
				pid, watchdog_time);
			kill_child_group(pid, kill_time);
			break;		/* from while (!child_exited) */
		}
		if (result == -1 && errno == EINTR) {
			/*
			 * We might have been interrupted by the SIGCHLD
			 * which we get when the child exits.  Or it could
			 * be another stray signal.  Either way, return
			 * to top-of-loop to check for child exit.
			 */
			goto reset;
		}
		if (result == -1) ppanic("poll");

		/*
		 * We have output from the child; copy it to our output.
		 */
		if (!pattern) reset = 1;	/* reset on any output */
		if (pollfds[0].revents & POLLIN)
			r_copy(pollfds[0].fd, origout);
		if (pollfds[1].revents & POLLIN) {
			if (pattern) {
				int r = match_copy(pollfds[1].fd, origerr,
						   &regexp);
				if (r < 0) ppanic("match_copy");
				if (r > 0) reset = 1;
			} else
				r_copy(pollfds[1].fd, origerr);
		}

	reset:
		now = time_now();
		if (reset) deadline = now + 1000000 * watchdog_time;
	}
	if (pattern) regfree(&regexp);

	int status;
	wait(&status);
	return handle_child_status(status);
}
