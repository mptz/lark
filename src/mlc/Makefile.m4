make_binary(mlc, beta.c env.c flatten.c form.c heap.c interpret.c
		 memloc.c mlc.c mlc.l mlc.y
		 node.c num.c parse.c prim.c readback.c reduce.c
		 resolve.c stmt.c subst.c
		 term.c unflatten.c, util, readline)

export MLC_INCLUDE := lib/mlc

%.runout %.runerr: %.mlc $(subdir)mlc
	src/mlc/mlc -eq $*.mlc > $*.runout 2> $*.runerr
