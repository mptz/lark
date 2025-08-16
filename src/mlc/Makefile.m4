make_binary(mlc, beta.c binder.c env.c flatten.c form.c
		 heap.c interpret.c libload.c library.c
		 memloc.c mlc.c mlc.l mlc.y
		 node.c num.c prim.c readback.c reduce.c
		 repl.c resolve.c sourcefile.c stmt.c subst.c
		 term.c unflatten.c, util, readline)

export MLCLIB := src/mlc/test:lib/mlc

# Build the library-level reference file from the individual references
# within the library.  Note that we need to use ASCII sort to match the
# order in which mlc traverses directories.
%.ref: %.lib/*.ref
	LANG=C ls $*.lib/*.ref | xargs cat > $@

# The LIBDEP variable is a temporary hack until we have a better way
# to reference library dependencies
%.runout %.runerr: %.lib $(subdir)mlc
	src/mlc/mlc -q $(shell cat $*.lib/LIBDEP) -l $(notdir $*.lib) /dev/null > $*.runout 2> $*.runerr
