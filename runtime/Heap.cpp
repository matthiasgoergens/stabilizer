#include "Heap.h"
#include "Function.h"
#include <cstdlib>
#include <set>

extern std::set<Function*> functions;

DataHeapType* getDataHeap() {
    static char buf[sizeof(DataHeapType)];
    static DataHeapType* _theDataHeap = new (buf) DataHeapType;
    return _theDataHeap;
}

CodeHeapType* getCodeHeap() {
    alignas(CodeHeapType) static char buf[sizeof(CodeHeapType)];
    static CodeHeapType* _theCodeHeap = new (buf) CodeHeapType;
    return _theCodeHeap;
}

void configureCodeHeap() {
    // This cap counts held rounded payload bytes, not logical code bodies,
    // mapping chunks, headers or RSS. Keep the retained code-byte limit intact.
    size_t budget = 16 * 1024 * 1024;
    const size_t maximum = size_t(1) << 30;
    if(const char* text = std::getenv("STABILIZER_MAX_SHUFFLE_BYTES")) {
        budget = 0;
        if(!*text) { ABORT("Stabilizer: invalid STABILIZER_MAX_SHUFFLE_BYTES"); }
        for(const char* p = text; *p; ++p) {
            if(*p < '0' || *p > '9' || budget > (maximum - size_t(*p - '0')) / 10) {
                ABORT("Stabilizer: invalid STABILIZER_MAX_SHUFFLE_BYTES");
            }
            budget = budget * 10 + size_t(*p - '0');
        }
        if(!budget) { ABORT("Stabilizer: invalid STABILIZER_MAX_SHUFFLE_BYTES"); }
    }
    CodeHeapType* heap = getCodeHeap();
    for(Function* f : functions) {
        if(!heap->note_size(f->getAllocationSize())) {
            ABORT("Stabilizer: unsupported code allocation size %zu for shuffling at %p",
                  f->getAllocationSize(), f->getCodeBase());
        }
    }
    if(!heap->configure(budget)) {
        ABORT("Stabilizer: STABILIZER_MAX_SHUFFLE_BYTES cannot provide two slots per code class");
    }
}
