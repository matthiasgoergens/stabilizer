ROOT = ../..
TARGETS = code-layout

build:: code-layout

include $(ROOT)/common.mk

CC = $(ROOT)/szc -Rcode
CXX = $(CC)
CXXFLAGS = -O2

$(OBJS):: $(ROOT)/szc $(ROOT)/LLVMStabilizer.$(SHLIB_SUFFIX)

test:: code-layout
	@echo $(INDENT)[test] Running the tightly packed C++ code-layout regression
	@$(LD_PATH_VAR)=$(ROOT) ./code-layout
