#ifndef STABILIZER_CHECKED_CODE_SOURCE_H
#define STABILIZER_CHECKED_CODE_SOURCE_H

#include <cstddef>
#include <limits>
#include <new>

// Code mappings can fail when the PC-relative address window fills up. The
// dependency's BumpAlloc/SizeHeap chain does not propagate that failure. Keep
// checked local layers for code without changing the data allocator or vendor
// sources. Like the old bump layer, mappings live for the process lifetime.
template <size_t ChunkSize, class Mapping, size_t Align>
class CheckedCodeBump : public Mapping {
    unsigned char* next_ = nullptr;
    size_t remaining_ = 0;
public:
    enum { Alignment = Align };
    static_assert(Align && !(Align & (Align - 1)), "power-of-two alignment required");
    static_assert(ChunkSize && ChunkSize % Align == 0, "unaligned chunk size");
    static_assert(Mapping::Alignment % Align == 0, "unaligned mapping source");

    void* malloc(size_t size) {
        if(size == 0) size = 1;
        if(size > std::numeric_limits<size_t>::max() - (Align - 1)) return nullptr;
        size = (size + Align - 1) & ~(Align - 1);
        if(size > remaining_) {
            const size_t request = size > ChunkSize ? size : ChunkSize;
            void* mapping = Mapping::malloc(request);
            if(!mapping) return nullptr; // Preserve any previous unused tail.
            next_ = static_cast<unsigned char*>(mapping);
            remaining_ = request;
        }
        void* result = next_;
        next_ += size;
        remaining_ -= size;
        return result;
    }

    bool free(void*) { return false; }
};

template <class SuperHeap>
class CheckedCodeSize : public SuperHeap {
    // Preserve the old two-word prefix and its resulting alignment. This
    // layer sits outside the one-size-class free list, so reuse rewrites it.
    struct Header { size_t size; size_t magic; };
    static constexpr size_t Magic = 0xCAFEBABE;
    static Header* header(void* p) { return static_cast<Header*>(p) - 1; }
    static const Header* header(const void* p) { return static_cast<const Header*>(p) - 1; }
public:
    static_assert(!(sizeof(Header) & (sizeof(Header) - 1)), "power-of-two header required");
    static_assert(!(SuperHeap::Alignment & (SuperHeap::Alignment - 1)),
                  "power-of-two source alignment required");
    enum { Alignment = SuperHeap::Alignment < sizeof(Header)
                           ? SuperHeap::Alignment : sizeof(Header) };
    static_assert(Alignment >= alignof(Header), "unaligned size header");

    void* malloc(size_t size) {
        if(size > std::numeric_limits<size_t>::max() - sizeof(Header)) return nullptr;
        void* raw = SuperHeap::malloc(size + sizeof(Header));
        if(!raw) return nullptr;
        Header* h = new (raw) Header{size, Magic};
        return h + 1;
    }

    void free(void* p) {
        if(p && header(p)->magic == Magic) SuperHeap::free(header(p));
    }

    static size_t getSize(const void* p) {
        return p && header(p)->magic == Magic ? header(p)->size : 0;
    }
};

#endif
