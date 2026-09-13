ROOT = ../..
.DELETE_ON_ERROR:
TARGETS = relocation-types

include $(ROOT)/common.mk

CC = $(ROOT)/szc -Rcode
CXX = $(CC)
CFLAGS = -O2
CXXFLAGS = -O2
FILECHECK ?= $(shell llvm-config --bindir)/FileCheck

$(OBJS):: $(ROOT)/szc $(ROOT)/LLVMStabilizer.$(SHLIB_SUFFIX)

types.out.ll: types.ll $(ROOT)/LLVMStabilizer.$(SHLIB_SUFFIX)
	opt --load-pass-plugin=$(ROOT)/LLVMStabilizer.$(SHLIB_SUFFIX) \
	    -passes=stabilize -stabilize-code=true -S $< -o $@

clean::
	rm -f types.out.ll

test:: types.out.ll relocation-types
	$(FILECHECK) types.ll --input-file=types.out.ll
	LD_LIBRARY_PATH=$(ROOT) timeout --kill-after=1s 5s ./relocation-types
