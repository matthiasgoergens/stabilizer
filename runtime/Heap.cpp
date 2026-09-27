#include "Heap.h"
#include "Function.h"
#include <cstdlib>
#include <cstring>
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

// Code copies otherwise all start at the same offset modulo the bump
// alignment (16 mod 32), and large functions at nearly the same offset modulo
// the page size, so cache-line and page alignment of hot code (e.g. an
// interpreter loop) would hardly be sampled.  Move each copy by a random
// multiple of 16 bytes (the alignment compilers assume for x86 function
// entries) within the slack of its size class, up to one page.  At least
// CODE_OFFSET_PAD bytes are reserved so the offset modulo 64 is always uniform.
// STABILIZER_CODE_OFFSET=0 restores the unshifted placement.
enum { CODE_OFFSET_GRANULE = 16, CODE_OFFSET_PAD = 64, CODE_OFFSET_MAX = PAGESIZE };

static bool codeOffsetEnabled() {
    static int enabled = -1;
    if(enabled < 0) {
        const char* text = std::getenv("STABILIZER_CODE_OFFSET");
        if(!text || std::strcmp(text, "1") == 0) {
            enabled = 1;
        } else if(std::strcmp(text, "0") == 0) {
            enabled = 0;
        } else {
            ABORT("Stabilizer: STABILIZER_CODE_OFFSET must be 0 or 1");
        }
    }
    return enabled;
}

size_t codePlacementPad() {
    return codeOffsetEnabled() ? CODE_OFFSET_PAD : 0;
}

void* randomizeStart(void* raw, size_t size) {
    if(raw == NULL || !codeOffsetEnabled()) {
        return raw;
    }
    // Only the (single) relocating thread allocates code.
    static RandomNumberGenerator rng;
    size_t capacity = getCodeHeap()->getSize(raw);
    size_t slack = capacity > size ? capacity - size : 0;
    if(slack > CODE_OFFSET_MAX - CODE_OFFSET_GRANULE) {
        slack = CODE_OFFSET_MAX - CODE_OFFSET_GRANULE;
    }
    size_t choices = slack / CODE_OFFSET_GRANULE + 1;
    size_t offset = (rng.next() % choices) * CODE_OFFSET_GRANULE;
    return (uint8_t*)raw + offset;
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
        if(!heap->note_size(f->getAllocationSize() + codePlacementPad())) {
            ABORT("Stabilizer: unsupported code allocation size %zu for shuffling at %p",
                  f->getAllocationSize(), f->getCodeBase());
        }
    }
    if(!heap->configure(budget)) {
        ABORT("Stabilizer: STABILIZER_MAX_SHUFFLE_BYTES cannot provide two slots per code class");
    }
}
