.PHONY: test clean

test:
	@mkdir -p build
	$(CXX) -std=c++17 -O1 -g -fsanitize=undefined -fno-sanitize-recover=all -I../../Heap-Layers -I../../runtime source.cpp -o build/source
	@./build/source
	$(CXX) -std=c++17 -O1 -g -fsanitize=undefined -fno-sanitize-recover=all -I../../runtime reservoir.cpp -o build/reservoir
	@./build/reservoir
	../../szc -O2 -Rcode admission.c -o build/admission
	python3 admission.py
	@echo 'PASS: code-source failure, retry, tail preservation, reuse and overflow'

clean:
	rm -f build/source build/reservoir build/admission
