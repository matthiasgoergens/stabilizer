.PHONY: test clean

RUN = LD_LIBRARY_PATH=../.. STABILIZER_CODE_MODE=retained STABILIZER_MAX_EPOCHS=48 STABILIZER_INTERVAL_MS=0 timeout 20s

test: build/start-offset
	$(RUN) ./build/start-offset
	STABILIZER_CODE_OFFSET=0 $(RUN) ./build/start-offset fixed

build/start-offset: start-offset.c ../../szc ../../LLVMStabilizer.so
	mkdir -p build
	../../szc -O2 -Rcode start-offset.c -o $@

clean:
	rm -rf build
