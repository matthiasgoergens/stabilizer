#ifndef STABILIZER_CODE_SHUFFLE_HEAP_H
#define STABILIZER_CODE_SHUFFLE_HEAP_H

#include <cassert>
#include <cstddef>
#include <cstdint>

// One reservoir per rounded size class, as in the original Stabilizer heap.
// Admission is quiescent and precedes all allocations. After that the caller
// serialises access (the retained owner or the legacy relocation handler).
// Held objects, like code mappings, remain allocated for the process lifetime.
template <class SuperHeap, class Random>
class CodeShuffleHeap : public SuperHeap {
    static constexpr size_t Classes = SuperHeap::num_size_classes;
    struct Bin {
        bool active = false;
        size_t count = 0;
        size_t filled = 0;
        void* slots[256] = {};
    } bins_[Classes];
    bool configured_ = false;
    size_t reserved_ = 0;

    int classify(size_t size) {
        // Match ANSIWrapper's minimum/alignment without overflowing its
        // rounding expression or Kingsley's unchecked size2class conversion.
        if(size < 16) size = 16;
        for(size_t i = 0; i < Classes; ++i)
            if(size <= SuperHeap::get_class_size(i)) return int(i);
        return -1;
    }

    size_t choose(size_t count) {
        const uint32_t n = uint32_t(count);
        const uint32_t threshold = (uint32_t(0) - n) % n;
        uint32_t value;
        do { value = uint32_t(rng_.next()); } while(value < threshold);
        return value % n;
    }
protected:
    Random rng_;
public:
    bool note_size(size_t size) {
        if(configured_ || !size) return false;
        int i = classify(size);
        if(i < 0) return false;
        bins_[i].active = true;
        return true;
    }

    bool configure(size_t budget) {
        if(configured_) return false;
        size_t minimum = 0, classes = 0;
        for(size_t i = 0; i < Classes; ++i) if(bins_[i].active) {
            size_t size = SuperHeap::get_class_size(i);
            if(size > (budget - minimum) / 2) return false;
            minimum += 2 * size;
            ++classes;
        }
        size_t share = classes ? (budget - minimum) / classes : 0;
        for(size_t i = 0; i < Classes; ++i) if(bins_[i].active) {
            size_t size = SuperHeap::get_class_size(i);
            size_t extra = share / size;
            bins_[i].count = 2 + (extra < 254 ? extra : 254);
            reserved_ += bins_[i].count * size;
        }
        configured_ = true;
        return true;
    }

    size_t reserved_bytes() const { return reserved_; }

    void* malloc(size_t size) {
        int i = classify(size);
        if(!configured_ || i < 0 || !bins_[i].active) return nullptr;
        Bin& bin = bins_[i];
        size_t rounded = SuperHeap::get_class_size(i);
        // Partial fill remains owned by the reservoir and is reused on retry.
        while(bin.filled < bin.count) {
            void* p = SuperHeap::malloc(rounded);
            if(!p) return nullptr;
            bin.slots[bin.filled++] = p;
        }
        void* incoming = SuperHeap::malloc(rounded);
        if(!incoming) return nullptr;
        size_t slot = choose(bin.count);
        void* result = bin.slots[slot];
        bin.slots[slot] = incoming;
        return result;
    }

    void free(void* p) {
        if(!p) return;
        int i = classify(SuperHeap::getSize(p));
        // Only pointers returned by this heap may be freed through it.
        assert(configured_ && i >= 0 && bins_[i].active);
        Bin& bin = bins_[i];
        assert(bin.filled == bin.count);
        size_t slot = choose(bin.count);
        void* evicted = bin.slots[slot];
        bin.slots[slot] = p;
        SuperHeap::free(evicted);
    }
};

#endif
