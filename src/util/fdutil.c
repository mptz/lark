/*
 * Copyright (c) 2001-2002 Michael P. Touloumtzis.
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
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <util/fdutil.h>
#include <util/message.h>

static inline off_t
i_lseek(int fd, off_t offset, int whence)
{
	off_t r;
	r = lseek(fd, offset, whence);
	if (r == (off_t) -1)
		ppanic("lseek");
	return r;
}

FILE *
e_fopen(const char *path, const char *mode)
{
	FILE *f;
	if ((f = fopen(path, mode)) == NULL)
		perrf("fopen(\"%s\")", path);
	return f;
}

int
e_open(const char *path, int flags)
{
	int fd;
	if ((fd = open(path, flags, 0666)) == -1)
		perrf("open(\"%s\")", path);
	return fd;
}

int
e_rename(const char *oldpath, const char *newpath)
{
	int r;
	if ((r = rename(oldpath, newpath)) != 0)
		perrf("rename(\"%s\", \"%s\")", oldpath, newpath);
	return r;
}

int
e_stat(const char *path, struct stat *buf)
{
	int r;
	if ((r = stat(path, buf)) != 0)
		perrf("stat(\"%s\")", path);
	return r;
}

int
e_unlink(const char *path)
{
	int r;
	if ((r = unlink(path)) != 0)
		perrf("unlink(\"%s\")", path);
	return r;
}

void
p_close(int fd)
{
	if (close(fd))
		ppanic("close");
}

void
p_fchdir(int fd)
{
	if (fchdir(fd) != 0)
		ppanic("fchdir");
}

int
p_fcntl2(int fd, int cmd, long arg)
{
	int r;
	if ((r = fcntl(fd, cmd, arg)) == -1)
		ppanic("fcntl");
	return r;
}

void
p_fstat(int fd, struct stat *buf)
{
	if (fstat(fd, buf) != 0)
		ppanic("fstat");
}

off_t
p_fstat_size(int fd)
{
	struct stat statbuf;

	if (fstat(fd, &statbuf) != 0)
		ppanic("fstat");
	return statbuf.st_size;
}

void
p_fsync(int fd)
{
	if (fsync(fd) != 0)
		ppanic("fsync");
}

off_t
p_lseek(int fd, off_t offset, int whence)
{
	return i_lseek(fd, offset, whence);
}

ssize_t
p_read(int fd, void *buf, size_t count)
{
	ssize_t r;
	do {
		r = read(fd, buf, count);
	} while ((r < 0) && (errno == EINTR));
	if (r < 0)
		ppanic("read");
	return r;
}

ssize_t
p_readall(int fd, void *buf, size_t count)
{
	ssize_t nread = 1, remain;
	unsigned char *bytes = buf;
	for (remain = count; nread && remain; remain -= nread)
		bytes += (nread = p_read(fd, bytes, remain));
	return count - remain;
}

off_t
p_seekto(int fd, off_t offset)
{
	return i_lseek(fd, offset, SEEK_SET);
}

void
p_stat(const char *path, struct stat *buf)
{
	if (stat(path, buf) != 0)
		ppanic("stat");
}

off_t
p_stat_size(const char *path)
{
	struct stat statbuf;

	if (stat(path, &statbuf) != 0)
		ppanic("stat");
	return statbuf.st_size;
}

off_t
p_tell(int fd)
{
	return i_lseek(fd, 0, SEEK_CUR);
}

void
p_unlink(const char *path)
{
	if (unlink(path) != 0)
		ppanic("unlink");
}

ssize_t
p_write(int fd, const void *buf, size_t count)
{
	ssize_t r;
	do {
		r = write(fd, buf, count);
	} while ((r < 0) && (errno == EINTR));
	if (r < 0)
		ppanic("write");
	return r;
}

ssize_t
p_writeall(int fd, const void *buf, size_t count)
{
	ssize_t nwritten, remain;
	const unsigned char *bytes = buf;
	for (remain = count; remain; remain -= nwritten)
		bytes += (nwritten = p_write(fd, bytes, remain));
	return count;
}

inline ssize_t
r_read(int fd, void *buf, size_t count)
{
	ssize_t r;
	do {
		r = read(fd, buf, count);
	} while ((r < 0) && (errno == EINTR));
	if (r < 0) {
		if (errno == EBADF || errno == EFAULT ||
		    errno == EINVAL || errno == EISDIR)
			ppanic("read");
		xperror("read");
	}
	return r;
}

ssize_t
r_readall(int fd, void *buf, size_t count)
{
	ssize_t nread;
	unsigned char *bytes = buf;
	for (/* nada */; count; count -= nread) {
		bytes += (nread = r_read(fd, bytes, count));
		if (nread <= 0) {
			assert(nread || errno);
			return -1;
		}
	}
	return 0;
}

inline ssize_t
r_write(int fd, const void *buf, size_t count)
{
	ssize_t r;
	do {
		r = write(fd, buf, count);
	} while ((r < 0) && (errno == EINTR));
	if (r < 0) {
		if (errno == EBADF || errno == EFAULT ||
		    errno == EINVAL || errno == EISDIR)
			ppanic("write");
		xperror("write");
	}
	return r;
}

ssize_t
r_writeall(int fd, const void *buf, size_t count)
{
	ssize_t nwritten;
	const unsigned char *bytes = buf;
	for (/* nada */; count; count -= nwritten) {
		bytes += (nwritten = r_write(fd, bytes, count));
		if (nwritten <= 0) {
			assert(nwritten);
			return -1;
		}
	}
	return 0;
}
