make_binary(lc, alloc.c env.c heap.c include.c lc.l lc.y main.c
	    readback.c reduce.c term.c, util)

# lc doesn't have a proper command-line interface and only works when
# run from its root directory, so we feed it tests on stdin.
%.runout %.runerr: %.lc $(subdir)lc
	(cd src/lc; ./lc -q) < $*.lc > $*.runout 2> $*.runerr
