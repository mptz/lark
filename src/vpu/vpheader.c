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

/*
 * A VPU binary file header is 4KiB in size.  Current subsections:
 *
 * 1. Identity section, bytes 0..1023
 *    Beyond verifying file magic, the contents of this section are ignored.
 *    Field separator is ';'
 *    Fields:
 *	a. File magic ("Lark-VPU-binary")
 *	b. Human readable version ("v0.0.0")
 *	c. System information (from uname(2))
 *	d. Compilation time & date stamp
 * 2. Compatibility section, bytes 1024..2047
 *    This section must be bit-for-bit as expected or the file won't load.
 *    No field separator (binary data)
 *	a. Word size/endian verification: 4 or 8 bytes, write of
 *	   0x8877665544332211 truncated to word size, native endianness.
 *	   (These files are not portable across architectures)
 *	b. ABI HUID... new HUID represents a new, incompatible ABI.
 *	c. Zero padding to end of section.
 * 3. Metadata section, bytes 2048..3071
 *	   XXX revise this... WIP
 *	a. Length of the instruction stream, in machine words.
 *	   The instruction stream begins immediately after the header.
 *	b. Zero padding to end of section.
 * 4. Reserved, bytes 3072..4095.  Zeroed.
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>

#include "vpheader.h"

#define SECTION_SIZE 1024

static const char file_magic[] = "Lark-VPU-binary";
static const char file_version[] = "v0.0.0";
static const char abi_huid[] = "NWoaW1pm.-JtWTO5k.45ruRSTZ";

static void vpu_identity_section(char *buf)
{
	char *p = buf, *const b = buf + SECTION_SIZE;
	struct utsname u;

	p += snprintf(p, b - p, "%s;%s;", file_magic, file_version);
	uname(&u);
	p += snprintf(p, b - p, "%s %s %s %s %s.%s;", u.sysname, u.release,
		u.version, u.machine, u.nodename, u.domainname);
	if (p + 100 < b) {	/* room for timestamp; docs say 26 needed */
		time_t tm;
		time(&tm);
		ctime_r(&tm, p);
	}
}

static void vpu_compatibility_section(char *buf)
{
	uintptr_t bytes = (uintptr_t) 0x8877665544332211;
	memcpy(buf, &bytes, sizeof bytes);
	buf += sizeof bytes;
	memcpy(buf, abi_huid, sizeof abi_huid);
}

static void vpu_metadata_section(char *buf, struct vpu_header_metadata *md)
{
	memcpy(buf, md, sizeof *md);
}

void vpu_header_write(char *buf, struct vpu_header_metadata *md)
{
	char	*section1 = buf,
		*section2 = section1 + SECTION_SIZE,
		*section3 = section2 + SECTION_SIZE;
	memset(buf, 0, VPU_HEADER_SIZE);
	vpu_identity_section(section1);
	vpu_compatibility_section(section2);
	vpu_metadata_section(section3, md);
}

int vpu_header_verify(const char *buf)
{
	if (memcmp(buf, file_magic, sizeof file_magic - 1))
		return -1;

	char section [SECTION_SIZE] = { 0, };
	vpu_compatibility_section(section);
	if (memcmp(buf + SECTION_SIZE, section, sizeof section))
		return -2;

	return 0;
}

struct vpu_header_metadata *vpu_header_metadata(const char *buf)
{
	return (struct vpu_header_metadata*) (buf + 2 * SECTION_SIZE);
}
