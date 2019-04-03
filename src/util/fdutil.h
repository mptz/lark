#ifndef LARK_UTIL_FDUTIL_H
#define LARK_UTIL_FDUTIL_H
/*
 * Copyright (c) 2001-2019 Michael P. Touloumtzis.
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

#include <stdio.h>
#include <sys/types.h>

struct stat;

extern FILE *e_fopen(const char *path, const char *mode);
extern int e_open(const char *path, int flags);
extern int e_rename(const char *oldpath, const char *newpath);
extern int e_stat(const char *path, struct stat *buf);
extern int e_unlink(const char *path);

extern void p_fchdir(int fd);
extern void p_close(int fd);
extern int p_fcntl2(int fd, int cmd, long arg);
extern void p_fstat(int fd, struct stat *buf);
extern off_t p_fstat_size(int fd);
extern void p_fsync(int fd);
extern off_t p_lseek(int fd, off_t offset, int whence);
extern ssize_t p_read(int fd, void *buf, size_t count);
extern ssize_t p_readall(int fd, void *buf, size_t count);
extern off_t p_seekto(int fd, off_t offset);
extern void p_stat(const char *path, struct stat *buf);
extern off_t p_stat_size(const char *path);
extern off_t p_tell(int fd);
extern void p_unlink(const char *path);
extern ssize_t p_write(int fd, const void *buf, size_t count);
extern ssize_t p_writeall(int fd, const void *buf, size_t count);

/*
 * Restarting read(); a minimal wrapper.
 *
 * Will not return -1 due to EINTR, although it may return a short read.
 * Panics only on application errors (EBADF, EFAULT, EINVAL, EISDIR).
 *
 * Returns: -1 or amount read (0 for EOF on blocking fd, as read(2)).
 */
extern ssize_t r_read(int fd, void *buf, size_t count);

/*
 * Restarting whole-buffer read(); for blocking fds only.
 *
 * Like r_read(), but will reissue reads for the remaining data if a short
 * read occurs.  EOF (read returning 0) before the full requested byte
 * count will return -1 (as an error) with errno == 0.
 *
 * Use this function only in situations in which failure to read all of the
 * data is equivalent to failure to read any of it (i.e. if a partial read
 * represents an error).  Note that even if -1 is returned, the seek
 * position of 'fd' may have changed, and some data may have been read.
 *
 * Returns: 0 (success) or -1 (failure)
 */
extern ssize_t r_readall(int fd, void *buf, size_t count);

/*
 * Restarting write(); a minimal wrapper.
 *
 * Will not return -1 due to EINTR, although it may return a short write.
 * Panics only on application errors (EBADF, EFAULT, EINVAL, EISDIR).
 */
extern ssize_t r_write(int fd, const void *buf, size_t count);

/*
 * Restarting whole-buffer write(); for blocking fds only.
 *
 * Like r_write(), but will reissue writes for the remaining data if a
 * short write occurs.
 *
 * Use this function only in situations in which failure to write all of
 * the data is equivalent to failure to write any of it (i.e. if a partial
 * write represents an error).  Note that even if -1 is returned, the seek
 * position of 'fd' may have changed, and some data may have been written.
 *
 * Returns: 0 (success) or -1 (failure)
 */
extern ssize_t r_writeall(int fd, const void *buf, size_t count);

#endif /* LARK_UTIL_FDUTIL_H */
