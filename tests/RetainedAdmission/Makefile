ROOT = ../..
.DELETE_ON_ERROR:
.PHONY: all debug release build test clean
NATIVE_CC ?= clang
OPT ?= opt
LLC ?= llc
SZC = $(ROOT)/szc -Rcode
VARIANTS = plain heap stack legacy late fork
TARGETS = $(addprefix admission-, $(VARIANTS))

all debug release build: $(TARGETS)

fixture.bc: fixture.c Makefile
	$(NATIVE_CC) -O0 -fPIC -emit-llvm -c $< -o $@

heap.ll: fixture.bc $(ROOT)/LLVMStabilizer.so Makefile
	$(OPT) --load-pass-plugin=$(ROOT)/LLVMStabilizer.so -passes=stabilize -stabilize-heap=true -S $< -o $@

stack.ll: fixture.bc $(ROOT)/LLVMStabilizer.so Makefile
	$(OPT) --load-pass-plugin=$(ROOT)/LLVMStabilizer.so -passes=stabilize -stabilize-stack=true -S $< -o $@

%.o: %.ll Makefile
	$(LLC) -relocation-model=pic -filetype=obj $< -o $@

libheap.so: heap.o $(ROOT)/libstabilizer.so
	$(NATIVE_CC) -shared $< -L$(ROOT) -lstabilizer -o $@

libstack.so: stack.o $(ROOT)/libstabilizer.so
	$(NATIVE_CC) -shared $< -L$(ROOT) -lstabilizer -o $@

libplain.so: native-fixture.c Makefile
	$(NATIVE_CC) -O2 -fPIC -shared $< -o $@

liblegacy.so: native-fixture.c Makefile $(ROOT)/libstabilizer.so
	$(NATIVE_CC) -O2 -fPIC -shared -DLEGACY_HEAP $< -L$(ROOT) -lstabilizer -o $@

liblate.so: native-fixture.c Makefile $(ROOT)/libstabilizer.so
	$(NATIVE_CC) -O2 -fPIC -shared -DLATE_REGISTRATION $< -L$(ROOT) -lstabilizer -o $@

libfork.so: native-fixture.c Makefile
	$(NATIVE_CC) -O2 -fPIC -shared -DFORK $< -o $@

admission-%: main.c lib%.so $(ROOT)/szc $(ROOT)/LLVMStabilizer.so $(ROOT)/libstabilizer.so Makefile
	$(SZC) -O2 main.c -L. -l$* -o $@

test: $(TARGETS) heap.ll stack.ll
	sh ./check-admission.sh

clean:
	rm -f $(TARGETS) libplain.so libheap.so libstack.so liblegacy.so liblate.so libfork.so fixture.bc heap.ll stack.ll heap.o stack.o *.out
