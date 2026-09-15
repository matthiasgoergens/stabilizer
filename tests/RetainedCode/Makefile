ROOT = ../..
TARGETS = retained-code
SRCS = retained-code.c
INCLUDES = native-helper.h libnative-helper.so
LIBS = native-helper pthread

include $(ROOT)/common.mk

CC = $(ROOT)/szc -Rcode
CXX = $(CC)
CFLAGS = -O2 -L.
CXXFLAGS = $(CFLAGS)
NATIVE_CC ?= clang

$(OBJS):: $(ROOT)/szc $(ROOT)/LLVMStabilizer.$(SHLIB_SUFFIX)

# The post-cleanup checker queries runtime globals. Declare that dependency
# rather than relying on the ordering of otherwise unrelated shared libraries.
libnative-helper.so: native-helper.c native-helper.h Makefile $(ROOT)/libstabilizer.so
	@$(NATIVE_CC) -O2 -g -fPIC -shared -pthread native-helper.c -L$(ROOT) -lstabilizer -Wl,-z,defs -o $@

clean::
	@rm -f libnative-helper.so native-helper.dynamic

test:: retained-code
	@readelf --dynamic libnative-helper.so > native-helper.dynamic
	@grep -E '\(NEEDED\).*\[libstabilizer[.]so\]' native-helper.dynamic
	@echo $(INDENT)[test] Retained code with a return held in an uninstrumented pthread helper
	@LD_LIBRARY_PATH=.:$(ROOT) STABILIZER_CODE_MODE=retained STABILIZER_MAX_EPOCHS=4 STABILIZER_INTERVAL_MS=0 timeout 20s ./retained-code
	@LD_LIBRARY_PATH=.:$(ROOT) STABILIZER_CODE_MODE=retained STABILIZER_MAX_EPOCHS=4 STABILIZER_INTERVAL_MS=0 timeout 20s ./retained-code exit
	@LD_LIBRARY_PATH=.:$(ROOT) STABILIZER_CODE_MODE=retained STABILIZER_MAX_EPOCHS=1000 STABILIZER_MAX_CODE_BYTES=1073741824 STABILIZER_INTERVAL_MS=20 timeout 20s ./retained-code early-return
	@LD_LIBRARY_PATH=.:$(ROOT) STABILIZER_CODE_MODE=retained STABILIZER_MAX_EPOCHS=1000 STABILIZER_MAX_CODE_BYTES=1073741824 STABILIZER_INTERVAL_MS=20 timeout 20s ./retained-code early-exit
