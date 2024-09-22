make_binary(slc, beta.c crumble.c env.c form.c heap.c interpret.c
		 memloc.c node.c parse.c readback.c reduce.c
		 resolve.c slc.c slc.l slc.y
		 stmt.c term.c uncrumble.c, util, readline)

export SLC_INCLUDE := lib/slc

%.runout %.runerr: %.slc $(subdir)slc
	src/slc/slc -q $*.slc > $*.runout 2> $*.runerr
