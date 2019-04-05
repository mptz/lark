make_library(libvpu,
	     bignum.c heap.c pstr.c vpheader.c vpu.c)
make_binary(vpasm, asm.l asm.y opcode.c pool.c vpasm.c, vpu util)
make_binary(vpu, vprun.c, vpu util)
make_binary(vpstep, opcode.c vpstep.c vpstep.l vpstep.y,
	    vpu util, curses readline)
make_binary(bignumstress, bignum.c bignumstress.c heap.c, util, gmp)
make_binary(bignumtest, bignumtest.c, , gmp)

# Add some dependencies so make will understand how to generate the
# instruction files and how they interact with others.
# 
$(subdir)opcode.h: $(subdir)opcodes.h
$(subdir)opcode.c: $(subdir)insncodes.c $(subdir)opnames.c
$(subdir)vpu.c: $(subdir)oplabels.c $(subdir)opargs.c $(subdir)opimpls.c

$(subdir)@distclean: clean-files += \
	$(subdir)insncodes.c $(subdir)opargs.c $(subdir)opcodes.h \
	$(subdir)oplabels.c $(subdir)oplex.l \
	$(subdir)opnames.c $(subdir)opimpls.c
$(subdir)insncodes.c $(subdir)opcodes.h \
		$(subdir)oplabels.c $(subdir)oplex.l \
		$(subdir)opnames.c $(subdir)opimpls.c: $(subdir)mkvpu
	src/vpu/mkvpu

# Generate test cases using m4 (should maybe become more global)
$(subdir)%: $(subdir)%.m4
	$(M4) -P $^ > $@

# Implicit rule for assembling VPU files.
VPASM := $(subdir)vpasm
%.vpb: %.vps $(VPASM)
	$(VPASM) $(VPASMFLAGS) $<

# Implicit rule for vpu-based tests.
# both on the test script (foo.tlc) and the tlc binary; currently tests
# must also be run in the tlc build directory, whereas make runs tests from
# the root directory even when invoked from a subdir.
VPU := $(subdir)vpu
VPUFLAGS := -q
%.runout %.runerr: %.vpb $(VPU)
	$(VPU) $(VPUFLAGS) $< > $*.runout 2> $*.runerr
