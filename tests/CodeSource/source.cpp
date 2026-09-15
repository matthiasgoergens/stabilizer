#include <heaplayers>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>

#ifndef LEGACY_SOURCE
#include "CheckedCodeSource.h"
#endif

struct Mapping {
    enum { Alignment = 4096 };
    alignas(4096) unsigned char storage[8192] = {};
    bool fail = true;
    unsigned calls = 0;
    size_t used = 0;
    size_t last_size = 0;
    void *malloc(size_t size) {
        ++calls;
        last_size = size;
        if (fail || size > sizeof(storage) - used) return nullptr;
        void *result = storage + used;
        used += size;
        return result;
    }
};

#ifdef LEGACY_SOURCE
using Bump = HL::BumpAlloc<4096, Mapping, 32>;
using Source = HL::SizeHeap<HL::FreelistHeap<Bump>>;
#else
using Bump = CheckedCodeBump<4096, Mapping, 32>;
using Source = CheckedCodeSize<HL::FreelistHeap<Bump>>;
#endif

int main() {
    // Red regression: the old chain performs arithmetic on null here.
    Source source;
    assert(source.malloc(1024) == nullptr);
    assert(source.calls == 1);
    source.fail = false;
    auto *p = static_cast<unsigned char *>(source.malloc(1024));
    assert(p != nullptr && source.calls == 2);
    assert(reinterpret_cast<uintptr_t>(p) % Source::Alignment == 0);
    assert(source.getSize(p) == 1024);
    const void *constant = p;
    assert(Source::getSize(constant) == 1024);
    std::memset(p, 0x5a, 1024);
    source.free(p);
    assert(source.malloc(1024) == p); // Existing one-class free-list contract.
    assert(source.calls == 2);
    assert(source.getSize(p) == 1024);
    source.free(nullptr);

    Bump bump;
    bump.fail = false;
    auto *a = static_cast<unsigned char *>(bump.malloc(32));
    assert(a != nullptr && bump.calls == 1);
    std::memset(a, 0x6b, 32);
    bump.fail = true;
    assert(bump.malloc(4096) == nullptr);
    assert(bump.calls == 2);
    assert(bump.malloc(32) == a + 32); // Failed refill must preserve the tail.
    for (unsigned i = 0; i < 32; ++i) assert(a[i] == 0x6b);
    bump.fail = false;
    assert(bump.malloc(4096) != nullptr);
    assert(bump.calls == 3);

    const size_t maximum = std::numeric_limits<size_t>::max();
    Source overflow;
    overflow.fail = false;
    assert(overflow.malloc(maximum) == nullptr);
    assert(overflow.calls == 0); // Header addition overflow.
    Bump rounding;
    rounding.fail = false;
    assert(rounding.malloc(maximum) == nullptr);
    assert(rounding.calls == 0); // Alignment rounding overflow.
    assert(rounding.malloc(0) != nullptr);
    assert(rounding.malloc(1) != nullptr);
    assert(rounding.calls == 1);

    // Exercise the actual size-class fallback path: both the selected bin
    // and Kingsley's fallback source fail, without touching null headers.
    HL::ANSIWrapper<HL::KingsleyHeap<Source, Source>> heap;
    assert(heap.malloc(1024) == nullptr);
    assert(heap.getSize(nullptr) == 0);
    heap.free(nullptr);
}
