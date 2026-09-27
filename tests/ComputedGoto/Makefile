.PHONY: test clean

test: build/goto
	LD_LIBRARY_PATH=../.. STABILIZER_CODE_MODE=legacy ./build/goto
	LD_LIBRARY_PATH=../.. STABILIZER_CODE_MODE=retained STABILIZER_MAX_EPOCHS=1 ./build/goto

build/goto: goto.c ../../szc ../../LLVMStabilizer.so
	mkdir -p build
	../../szc -O2 -Rcode goto.c -o $@

clean:
	rm -rf build
