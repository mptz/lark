#ifndef LARK_UTIL_MESSAGE_H
#define LARK_UTIL_MESSAGE_H
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

/*
 * Functions and macros allowing processes to report status with various
 * degrees of urgency.  These include source location information like file
 * and line number, so they're intended for reporting issues in the program
 * itself and wouldn't be appropriate for reporting errors in input (e.g.
 * compiler reporting type-checking failures).
 */

/* name of this executable */
extern const char *execname;
extern void set_execname(const char *execpath);

/* allow e.g. "compiler internal error" instead of just "error" */
extern const char *global_error_prefix;

/* exit code to use for failures (q.v. diff-style programs) */
extern int failure_exit_code;

/* errno/perror variants */
extern int xperror(const char *context);	/* returns -errno */

/* configurable message level */
#define MSGLEVEL_ERR	 	10
#define MSGLEVEL_WARN		20
#define MSGLEVEL_INFO		30
#define MSGLEVEL_TRACE		40

extern unsigned global_message_threshold;

/* flags for additional message control */
#define MSGFLAG_PERROR		0x10000
#define MSGFLAG_BACKTRACE	0x20000

/* message output macros */
#define msg(level, message)	msg_real(level, \
	__FILE__, __func__, __LINE__, message)
#define msgf(level, format, args...)	msgf_real(level, \
	__FILE__, __func__, __LINE__, format ,## args)

#define err(message)	 	msg_real(MSGLEVEL_ERR, \
	__FILE__, __func__, __LINE__, message)
#define errf(format, args...)	msgf_real(MSGLEVEL_ERR, \
	__FILE__, __func__, __LINE__, format ,## args)
#define perr(message)		msg_real(MSGLEVEL_ERR | MSGFLAG_PERROR, \
	__FILE__, __func__, __LINE__, message)
#define perrf(format, args...)	msgf_real(MSGLEVEL_ERR | MSGFLAG_PERROR, \
	__FILE__, __func__, __LINE__, format ,## args)
#define warn(message)		msg_real(MSGLEVEL_WARN, \
	__FILE__, __func__, __LINE__, message)
#define warnf(format, args...)	msgf_real(MSGLEVEL_WARN, \
	__FILE__, __func__, __LINE__, format ,## args)
#define pwarn(message)		msg_real(MSGLEVEL_WARN | MSGFLAG_PERROR, \
	__FILE__, __func__, __LINE__, message)
#define pwarnf(format, args...)	msgf_real(MSGLEVEL_WARN | MSGFLAG_PERROR, \
	__FILE__, __func__, __LINE__, format ,## args)
#define info(message)		msg_real(MSGLEVEL_INFO, \
	__FILE__, __func__, __LINE__, message)
#define infof(format, args...)	msgf_real(MSGLEVEL_INFO, \
	__FILE__, __func__, __LINE__, format ,## args)
#define trace(message)		msg_real(MSGLEVEL_TRACE, \
	__FILE__, __func__, __LINE__, message)
#define tracef(format, args...)	msgf_real(MSGLEVEL_TRACE, \
	__FILE__, __func__, __LINE__, format ,## args)

/* panic with an error message */
#define panic(message) \
	panic_real(__FILE__, __func__, __LINE__, message)
#define panicf(format, args...) \
	panicf_real(__FILE__, __func__, __LINE__, format ,## args)
#define ppanic(message) \
	ppanic_real(__FILE__, __func__, __LINE__, message)

/* marker for unreachable code */
extern const char *unreachable_message;
#define unreachable() panic(unreachable_message)

/* actual implementations of the above */
extern int msg_real(unsigned level, const char *file, const char *function,
		    int line, const char *message);
extern int msgf_real(unsigned level, const char *file, const char *function,
		     int line, const char *format, ...);
extern void panic_real(const char *file, const char *function, int line,
		       const char *message) __attribute__ ((noreturn));
extern void panicf_real(const char *file, const char *function, int line,
			const char *format, ...) __attribute__ ((noreturn));
extern void ppanic_real(const char *file, const char *function, int line,
			const char *message) __attribute__ ((noreturn));

#endif /* LARK_UTIL_MESSAGE_H */
