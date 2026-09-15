#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#ifdef LEGACY_RESERVOIR
#include <heaplayers>
#include <shuffleheap.h>
#else
#include "CodeShuffleHeap.h"
#endif

struct FakeHeap {
    enum { Alignment = 32, num_size_classes = 2 };
    struct Slot { size_t size; bool live; alignas(32) char data[2048]; } slots[64] = {};
    unsigned calls = 0, frees = 0, fail_on = 0;
    int get_size_class(size_t s) { return s <= 1024 ? 0 : 1; }
    size_t get_class_size(int i) { return size_t(1024) << i; }
    void *malloc(size_t s) {
        assert(s > 0 && s <= 2048);
        s = get_class_size(get_size_class(s));
        if (++calls == fail_on) return nullptr;
        for (auto &slot : slots) if (!slot.live) {
            slot.live = true;
            slot.size = s;
            return slot.data;
        }
        return nullptr;
    }
    size_t getSize(void *p) {
        for (auto &slot : slots) if (slot.data == p) {
            assert(slot.live);
            return slot.size;
        }
        assert(false);
        return 0;
    }
    void free(void *p) {
        for (auto &slot : slots) if (slot.data == p) {
            assert(slot.live);
            slot.live = false;
            ++frees;
            return;
        }
        assert(false);
    }
};

struct ScriptedRandom {
    uint32_t value = 0;
    unsigned calls = 0;
    bool reject_first = false;
    uint32_t next() { ++calls; return reject_first && calls == 1 ? 0 : value; }
};

#ifdef LEGACY_RESERVOIR
struct Heap : ShuffleHeap<4096, 256, FakeHeap> {
    ScriptedRandom rng_;
    bool note_size(size_t) { return true; }
    bool configure(size_t) { return true; }
    size_t reserved_bytes() const { return 2048; }
};
#else
struct Heap : CodeShuffleHeap<FakeHeap, ScriptedRandom> { using CodeShuffleHeap::rng_; };
#endif

int main() {
    Heap heap;
    assert(heap.note_size(900));
    assert(heap.configure(2048));
    void *a = heap.malloc(900);
    assert(heap.calls == 3); // Old >256-byte bypass makes just one allocation.
    assert(a == heap.slots[0].data && heap.getSize(a) == 1024);
    heap.rng_.value = 1;
    void *b = heap.malloc(900);
    assert(b == heap.slots[1].data && a != b);
    heap.free(a); // Slot 1 holds a; its previously held object is evicted.
    assert(heap.frees == 1);
    void *c = heap.malloc(900);
    assert(c == a && c != b);
    heap.free(b);
    heap.free(c);
    heap.free(nullptr);
    assert(heap.reserved_bytes() == 2048);

#ifndef LEGACY_RESERVOIR
    assert(!heap.note_size(1024));
    assert(!heap.configure(4096));
    assert(heap.malloc(2048) == nullptr); // Unadmitted class, not a bypass.
    assert(heap.malloc(std::numeric_limits<size_t>::max()) == nullptr);

    Heap budget;
    assert(!budget.note_size(2049));
    assert(budget.note_size(17) && budget.note_size(2048));
    assert(!budget.configure(6143));
    assert(budget.configure(6144));
    assert(budget.reserved_bytes() == 6144);
    assert(budget.malloc(17) && budget.malloc(2048));

    // Fail each allocation in an initial two-slot fill plus incoming object.
    for (unsigned failure = 1; failure <= 3; ++failure) {
        Heap partial;
        assert(partial.note_size(1024) && partial.configure(2048));
        partial.fail_on = failure;
        assert(partial.malloc(1024) == nullptr);
        partial.fail_on = 0;
        void *p = partial.malloc(1024);
        assert(p && partial.getSize(p) == 1024);
        assert(partial.calls == 4); // Retry retains successfully filled slots.
        partial.free(p);
        assert(partial.frees == 1);
    }
    Heap capped;
    assert(capped.note_size(1024));
    assert(capped.configure(std::numeric_limits<size_t>::max()));
    assert(capped.reserved_bytes() == 256 * 1024);

    for (unsigned slot = 0; slot < 8; ++slot) {
        Heap selection;
        assert(selection.note_size(1024) && selection.configure(8192));
        selection.rng_.value = slot;
        assert(selection.malloc(1024) == selection.slots[slot].data);
    }
    Heap rejection;
    assert(rejection.note_size(1024) && rejection.configure(3072));
    rejection.rng_.reject_first = true;
    rejection.rng_.value = 2;
    assert(rejection.malloc(1024) == rejection.slots[2].data);
    assert(rejection.rng_.calls == 2); // Reject biased low draw for three slots.

    for (size_t limit = 0; limit < 20000; limit += 97) {
        Heap bounds;
        assert(bounds.note_size(1024) && bounds.note_size(2048));
        bool admitted = bounds.configure(limit);
        assert(admitted == (limit >= 6144));
        assert(bounds.reserved_bytes() <= limit);
        if (admitted) assert(bounds.reserved_bytes() >= 6144);
    }
    Heap ownership;
    assert(ownership.note_size(1024) && ownership.configure(2048));
    void *live[12];
    for (unsigned i = 0; i < 12; ++i) {
        live[i] = ownership.malloc(1024);
        assert(live[i]);
        for (unsigned j = 0; j < i; ++j) assert(live[i] != live[j]);
    }
    for (void *p : live) ownership.free(p);
    assert(ownership.frees == 12);
#endif
}
