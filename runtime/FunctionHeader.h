#ifndef RUNTIME_FUNCTIONHEADER_H
#define RUNTIME_FUNCTIONHEADER_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "Jump.h"
#include "Trap.h"

struct Function;

#if defined(__x86_64__)
// Install once, before application execution. After installation only _target
// changes: no instruction fetched by another CPU is rewritten. On x86_64 the
// indirect jump's aligned pointer load is atomic and has acquire ordering;
// publishing a fully prepared destination uses a release store.
//
// This protects publication, NOT target lifetime. A caller may have loaded an
// old destination immediately before publication, so reclamation needs its own
// protocol even when every future entry is redirected.
struct alignas(8) FunctionHeader {
private:
    uint8_t _entry[8];
    std::atomic<void*> _target;
    uint8_t _trap[8];
    Function* _f;

public:
    explicit FunctionHeader(Function* f) :
        _entry{0xff, 0x25, 0x02, 0, 0, 0, 0x90, 0x90},
        _target(_trap),
        _trap{0xcc, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90}, _f(f) {
        static_assert(offsetof(FunctionHeader, _target) == 8,
            "entry jump must address the aligned target slot");
        static_assert(offsetof(FunctionHeader, _trap) == 16,
            "trap decoding must match the header layout");
        static_assert(offsetof(FunctionHeader, _f) == 24,
            "metadata must fit inside the reserved entry");
        static_assert(__atomic_always_lock_free(sizeof(void*), nullptr),
            "entry destination publication must be lock-free");
    }

    void jumpTo(void* target) {
        _target.store(target, std::memory_order_release);
    }

    void trap() { jumpTo(_trap); }
    void* destination() { return _target.load(std::memory_order_acquire); }

    void* trapAddress() { return _trap; }

    static FunctionHeader* fromTrapAddress(void* address) {
        return reinterpret_cast<FunctionHeader*>(
            static_cast<uint8_t*>(address) - offsetof(FunctionHeader, _trap));
    }

    Function* getFunction() { return _f; }
};

static_assert(sizeof(FunctionHeader) == 32, "entry header must occupy 32 bytes");
static_assert(alignof(FunctionHeader) == 8, "entry header must be 8-byte aligned");
#else
// Legacy targets retain their existing mutable-instruction header. Code
// randomisation is currently admitted only on Linux x86_64 by the runtime.
struct FunctionHeader {
private:
    union {
        uint8_t _jmp[sizeof(Jump)];
        uint8_t _trap[sizeof(Trap)];
    };
    Function* _f;

public:
    explicit FunctionHeader(Function* f) : _f(f) {}
    void jumpTo(void* target) { new(_jmp) Jump(target); }
    void trap() { new(_trap) Trap(); }
    void* trapAddress() { return _trap; }
    static FunctionHeader* fromTrapAddress(void* address) {
        return static_cast<FunctionHeader*>(address);
    }
    Function* getFunction() { return _f; }
};
#endif

#endif
