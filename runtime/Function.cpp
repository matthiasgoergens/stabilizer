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
 * Copy the code for this function.  Use the previous relocated copy if the
 * function has already been relocated.  Relocation tables remain at their
 * final-link addresses and pc-relative references are patched accordingly.
 * 
 * \arg target The destination of the copy.
 */
void Function::copyTo(void* target) {
    void* source = NULL;

    if(_current == NULL) {
        source = getCodeBodyBase();

        // Copy the semantic body, omitting the NOP prefix reserved solely for
        // the original entry's trap and forwarding jump.
        memcpy(target, source, getCodeBodySize());

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

    for(const TextReloc& r : _textRelocs) {
        // All relocation types we currently record are 32-bit fields.
        if(r.offset + sizeof(int32_t) > getCodeBodySize()) {
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
                // The ELF parser classifies whether the referenced value moves
                // with this function.  Do not reconstruct that decision from
                // P+disp here: RIP is the end of the whole instruction, which
                // need not be the end of this four-byte field.
                if(r.internal) {
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
