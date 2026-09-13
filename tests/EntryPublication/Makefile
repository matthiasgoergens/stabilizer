ROOT = ../..
.DELETE_ON_ERROR:
TARGETS = entry-publication
SRCS = entry-publication.cpp

include $(ROOT)/common.mk

# Native by design: exercise entry publication independently of the runtime's
# not-yet-thread-safe relocation and reclamation machinery.
CXXFLAGS = -O2 -pthread
LIBS = pthread
FILECHECK ?= $(shell llvm-config --bindir)/FileCheck
INCLUDES += $(wildcard $(ROOT)/runtime/*.h)

alignment.out.ll: alignment.ll $(ROOT)/LLVMStabilizer.$(SHLIB_SUFFIX)
	opt --load-pass-plugin=$(ROOT)/LLVMStabilizer.$(SHLIB_SUFFIX) \
	    -passes=stabilize -stabilize-code=true -S $< -o $@

clean::
	rm -f alignment.out.ll alignment-fixture alignment-fixture.bc \
	    alignment-fixture.opt.bc alignment-fixture.llc.o \
	    alignment-unaligned alignment-unaligned.out

alignment-fixture: alignment.c $(ROOT)/szc $(ROOT)/LLVMStabilizer.$(SHLIB_SUFFIX)
	$(ROOT)/szc -v -Rcode alignment.c -o $@

alignment-unaligned: alignment-fixture unaligned.ld
	$(CC) alignment-fixture.llc.o -L$(ROOT) -lstabilizer -lstdc++ \
	    -Wl,--emit-relocs -Wl,-T,unaligned.ld -o $@

test:: entry-publication alignment.out.ll alignment-fixture alignment-unaligned
	timeout --kill-after=1s 20s ./entry-publication
	$(FILECHECK) alignment.ll --input-file=alignment.out.ll
	LD_LIBRARY_PATH=$(ROOT) ./alignment-fixture
	sh check-unaligned.sh
