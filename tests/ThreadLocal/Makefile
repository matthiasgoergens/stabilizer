ROOT = ../..
TARGETS = thread-local
SRCS = thread-local.c
LIBS = pthread

include $(ROOT)/common.mk

CC = $(ROOT)/szc -Rcode
CXX = $(CC)
CFLAGS = -O2
CXXFLAGS = $(CFLAGS)
NATIVE_CC ?= clang
FILECHECK = $(shell llvm-config --bindir)/FileCheck

$(OBJS):: $(ROOT)/szc $(ROOT)/LLVMStabilizer.$(SHLIB_SUFFIX)

native: thread-local.c Makefile
	$(NATIVE_CC) -O2 -pthread $< -o $@

types.out.ll: types.ll $(ROOT)/LLVMStabilizer.$(SHLIB_SUFFIX)
	opt --load-pass-plugin=$(ROOT)/LLVMStabilizer.$(SHLIB_SUFFIX) -passes=stabilize -stabilize-code=true -S $< -o $@

pic-tls: pic-tls.c $(ROOT)/szc $(ROOT)/LLVMStabilizer.$(SHLIB_SUFFIX) $(ROOT)/libstabilizer.$(SHLIB_SUFFIX)
	$(ROOT)/szc -Rcode -O2 -f PIC $< -o $@

pic-ie: pic-tls.c $(ROOT)/szc $(ROOT)/LLVMStabilizer.$(SHLIB_SUFFIX) $(ROOT)/libstabilizer.$(SHLIB_SUFFIX)
	$(ROOT)/szc -Rcode -O2 -f PIC -f tls-model=initial-exec $< -o $@

libshared-tls.so: shared-tls.c Makefile
	$(NATIVE_CC) -O2 -fPIC -shared $< -o $@

shared-gd: shared-access.c libshared-tls.so $(ROOT)/szc $(ROOT)/LLVMStabilizer.$(SHLIB_SUFFIX) $(ROOT)/libstabilizer.$(SHLIB_SUFFIX)
	$(ROOT)/szc -Rcode -O2 -f PIC $< -L. -lshared-tls -lpthread -o $@

shared-ie: shared-access.c libshared-tls.so $(ROOT)/szc $(ROOT)/LLVMStabilizer.$(SHLIB_SUFFIX) $(ROOT)/libstabilizer.$(SHLIB_SUFFIX)
	$(ROOT)/szc -Rcode -O2 -f PIC -f tls-model=initial-exec $< -L. -lshared-tls -lpthread -o $@

shared-native: shared-access.c libshared-tls.so Makefile
	$(NATIVE_CC) -O2 -fPIC -pthread $< -L. -lshared-tls -o $@

legacy-%: legacy-%.ll $(ROOT)/LLVMStabilizer.$(SHLIB_SUFFIX) $(ROOT)/libstabilizer.$(SHLIB_SUFFIX)
	opt --load-pass-plugin=$(ROOT)/LLVMStabilizer.$(SHLIB_SUFFIX) -passes=stabilize -stabilize-code=true $< -o $@.bc
	llc -relocation-model=pic -filetype=obj $@.bc -o $@.o
	$(NATIVE_CC) $@.o -Wl,--emit-relocs -L$(ROOT) -lstabilizer -lstdc++ -o $@

test:: thread-local native types.out.ll pic-tls pic-ie shared-gd shared-ie shared-native legacy-gd legacy-ie
	$(FILECHECK) types.ll --input-file=types.out.ll
	! grep --fixed-strings 'stabilizer_register_function(ptr @stabilizer.tls.' types.out.ll
	readelf --relocs --wide thread-local > relocations.out
	grep --fixed-strings R_X86_64_TPOFF32 relocations.out
	LD_LIBRARY_PATH=$(ROOT) STABILIZER_CODE_MODE=retained STABILIZER_MAX_EPOCHS=4 STABILIZER_MAX_CODE_BYTES=67108864 STABILIZER_INTERVAL_MS=0 timeout --kill-after=1s 20s ./thread-local retained
	timeout --kill-after=1s 20s ./native
	LD_LIBRARY_PATH=$(ROOT) STABILIZER_CODE_MODE=retained STABILIZER_MAX_EPOCHS=1 timeout --kill-after=1s 5s ./pic-tls
	LD_LIBRARY_PATH=$(ROOT) STABILIZER_CODE_MODE=retained STABILIZER_MAX_EPOCHS=1 timeout --kill-after=1s 5s ./pic-ie
	LD_LIBRARY_PATH=.:$(ROOT) STABILIZER_CODE_MODE=retained STABILIZER_MAX_EPOCHS=4 STABILIZER_MAX_CODE_BYTES=67108864 STABILIZER_INTERVAL_MS=0 timeout --kill-after=1s 20s ./shared-gd retained
	LD_LIBRARY_PATH=.:$(ROOT) STABILIZER_CODE_MODE=retained STABILIZER_MAX_EPOCHS=4 STABILIZER_MAX_CODE_BYTES=67108864 STABILIZER_INTERVAL_MS=0 timeout --kill-after=1s 20s ./shared-ie retained
	LD_LIBRARY_PATH=. timeout --kill-after=1s 20s ./shared-native
	sh check-pic.sh ./legacy-gd R_X86_64_TLSGD
	sh check-pic.sh ./legacy-ie R_X86_64_GOTTPOFF

clean::
	rm -f native types.out.ll relocations.out pic-tls pic-ie pic.out
	rm -f shared-gd shared-ie shared-native libshared-tls.so legacy-gd legacy-ie legacy-gd.bc legacy-ie.bc legacy-gd.o legacy-ie.o
