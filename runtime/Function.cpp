#include "Function.h"
#include "FunctionLocation.h"

#if defined(__x86_64__)
#include <elf.h>
#include <limits>
#endif

/**
 * Free the current function location and stack pad table
 */
Function::~Function() {
    if(_current != NULL) {
        _current->release();
    }
    
    if(_stackPad != NULL) {
        getDataHeap()->free(_stackPad);
    }
}

/**
 * Copy the code and relocation table for this function.  Use the pre-assembled
 * code/table chunk if the function has already been relocated.
 * 
 * \arg target The destination of the copy.
 */
void Function::copyTo(void* target) {
    void* source = NULL;

    if(_current == NULL) {
        source = _code.base();

        // Copy the code from the original function
        memcpy(target, _code.base(), _code.size());

        // Patch in the saved header, since the original has been overwritten
        *(FunctionHeader*)target = _savedHeader;

        // If there is a stack pad table, move it to a random location
        if(_stackPad != NULL && _table.base() != NULL && _table.size() >= sizeof(uintptr_t)) {
            uintptr_t* table = (uintptr_t*)_table.base();
            size_t n = _table.size() / sizeof(uintptr_t);
            for(size_t i = 0; i < n; i++) {
                if(table[i] == (uintptr_t)_stackPad) {
                    _stackPad = (uint8_t*)getDataHeap()->malloc(1);
                    table[i] = (uintptr_t)_stackPad;
                }
            }
        }

        // Copy the relocation table, if needed
        if(_tableAdjacent) {
            uint8_t* a = (uint8_t*)target;
            memcpy(&a[_code.size()], _table.base(), _table.size());
        }
    } else {
        source = _current->_memory.base();
        memcpy(target, source, getAllocationSize());
    }

    // After copying the code to a new location, patch any pc-relative
    // references (e.g., constant pools) so they still point to the intended
    // external targets from the new location.
    if(source != NULL) {
        applyTextRelocs(source, target);
    }
}

/**
 * Create a new FunctionLocation for this Function.
 * \arg relocation The ID for the current relocation phase.
 * \returns Whether or not a new location was created
 */
FunctionLocation* Function::relocate() {
    FunctionLocation* oldLocation = _current;
    _current = new FunctionLocation(this);
    _current->activate();

    // Fill the stack pad table with random bytes
    if(_stackPad != NULL) {
        // Update random stack pad
        *_stackPad = getRandomByte();
    }
    
    return oldLocation;
}

void Function::applyTextRelocs(void* source, void* dest) {
#if defined(__x86_64__)
    if(_textRelocs.empty()) {
        return;
    }

    intptr_t delta = (uint8_t*)dest - (uint8_t*)source;

    // If the function didn't move, nothing to do.
    if(delta == 0) {
        return;
    }

    uint8_t* src = (uint8_t*)source;
    uint8_t* dst = (uint8_t*)dest;

    // Anything within the copied allocation should move with the function,
    // and therefore should NOT be re-targeted back to the original address.
    uintptr_t internal_begin = (uintptr_t)src;
    uintptr_t internal_end = (uintptr_t)src + _code.size() + (_tableAdjacent ? _table.size() : 0);

    for(const TextReloc& r : _textRelocs) {
        // All relocation types we currently record are 32-bit fields.
        if(r.offset + sizeof(int32_t) > _code.size()) {
            continue;
        }

        uint8_t* oldP = src + r.offset;
        uint8_t* newP = dst + r.offset;

        int32_t oldVal = 0;
        memcpy(&oldVal, newP, sizeof(oldVal));

        switch(r.type) {
            case R_X86_64_PC32:
            case R_X86_64_PLT32:
            {
                // Relocation semantics for a pc-relative field: stored value is
                // S + A - P, so S + A = oldVal + oldP is the ELF relocation value
                // (the resolved reference address, modulo the x86 RIP +4/+trailing
                // adjustment). Test THIS against the copied range, NOT the bare
                // symbol value S = oldVal + oldP - addend. References to the
                // function's own adjacent relocation table are emitted against the
                // .text SECTION symbol plus a large addend, so S is the section
                // base (outside the function) while S + A is the table (inside).
                // Keying the internal/external decision on S misclassifies these
                // internal table references as external and rewrites them,
                // corrupting the relocated copy. (Caveat: S + A is ~4 bytes short
                // of the true x86 effective address; a reference to the very first
                // or last few bytes of the copied region could still be
                // misclassified. Not exercised by the known reproducers.)
                intptr_t target = (intptr_t)oldVal + (intptr_t)oldP;

                // If the target is within our copied allocation, leave it alone
                // so it continues to resolve into the relocated copy (including
                // the adjacent relocation table).
                if((uintptr_t)target >= internal_begin && (uintptr_t)target < internal_end) {
                    break;
                }

                int64_t newVal64 = (int64_t)oldVal - (int64_t)delta;
                if(newVal64 < std::numeric_limits<int32_t>::min() || newVal64 > std::numeric_limits<int32_t>::max()) {
                    ABORT("Text relocation overflow (PC32/PLT32): func=%p src=%p dst=%p off=%zu old=%d delta=%ld",
                        _code.base(), source, dest, r.offset, (int)oldVal, (long)delta);
                }

                int32_t newVal = (int32_t)newVal64;
                memcpy(newP, &newVal, sizeof(newVal));
                break;
            }

            case R_X86_64_GOTPCREL:
            case R_X86_64_GOTPCRELX:
            case R_X86_64_REX_GOTPCRELX:
            case R_X86_64_GOTPC32:
            case R_X86_64_TLSGD:
            case R_X86_64_TLSLD:
            case R_X86_64_GOTTPOFF:
            case R_X86_64_GOTPC32_TLSDESC:
            {
                int64_t newVal64 = (int64_t)oldVal - (int64_t)delta;
                if(newVal64 < std::numeric_limits<int32_t>::min() || newVal64 > std::numeric_limits<int32_t>::max()) {
                    ABORT("Text relocation overflow (x86_64 pc-relative 32, type=%u): func=%p src=%p dst=%p off=%zu old=%d delta=%ld",
                        (unsigned)r.type, _code.base(), source, dest, r.offset, (int)oldVal, (long)delta);
                }

                int32_t newVal = (int32_t)newVal64;
                memcpy(newP, &newVal, sizeof(newVal));
                break;
            }

            default:
                // Unknown relocation type: leave untouched.
                break;
        }
    }
#else
    (void)source;
    (void)dest;
#endif
}
