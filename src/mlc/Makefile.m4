make_binary(mlc, beta.c crumble.c env.c form.c heap.c interpret.c
		 memloc.c mlc.c mlc.l mlc.y
		 node.c parse.c prim.c readback.c reduce.c
		 resolve.c stmt.c term.c uncrumble.c, util, readline)

export MLC_INCLUDE := lib/mlc

%.runout %.runerr: %.mlc $(subdir)mlc
	src/mlc/mlc -eq $*.mlc > $*.runout 2> $*.runerr
