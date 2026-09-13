.PHONY: test clean

test:
	@mkdir -p build
	$(CXX) -std=c++17 -O1 -g -fsanitize=undefined -fno-sanitize-recover=all -I../../Heap-Layers -I../../runtime source.cpp -o build/source
	@./build/source
	@echo 'PASS: code-source failure, retry, tail preservation, reuse and overflow'

clean:
	rm -f build/source
