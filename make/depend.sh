#!/bin/sh
#
# depend.sh: postprocess dependency information.
#
# Copyright (c) 2001 Michael P. Touloumtzis.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#

# There's really little reason to keep this in a shell script, except to
# tidy up the make and to be able to clean up the generated Dependencies
# file on the event of failure (which is unlikely).

set -e

ME=$0
OUTPUT=$1
SUBDIR=`dirname $OUTPUT`
shift

if [ -z "$SUBDIR" ]; then
	echo "$ME: can't find dirname in $OUTPUT"
	exit 1
fi

if ! sed -e "s|^\([^[:space:]][^:]*:\)|$SUBDIR/\1|" $* > $OUTPUT; then
	echo "$ME: failed to generate $OUTPUT"
	rm -f $OUTPUT
	exit 2
fi
