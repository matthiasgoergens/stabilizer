#if !defined(RUNTIME_FUNCTION_H)
#define RUNTIME_FUNCTION_H

#include <cstdint>
#include <string.h>
#include <vector>
#include <sys/mman.h>

#include "Util.h"
#include "Jump.h"
#include "Trap.h"
#include "Heap.h"
#include "MemRange.h"
#include "FunctionHeader.h"

struct Function;
struct FunctionLocation;

static const size_t PATCHABLE_ENTRY_SIZE = 32;

struct TextReloc {
    size_t offset;      // Offset from the function base to the relocation field
    uint32_t type;      // ELF relocation type (e.g., R_X86_64_PC32)
    int64_t addend;     // ELF relocation addend (RELA)
    bool internal;      // The referenced value moves with this function
};

// The pass reserves PATCHABLE_ENTRY_SIZE bytes at every randomized entry. Fail
// the build if an architecture/header change would outgrow that contract.
static_assert(sizeof(FunctionHeader) <= PATCHABLE_ENTRY_SIZE,
    "increase Stabilizer's patchable entry size");

struct Function {
private:
    friend class FunctionLocation;
    
    MemRange _code;
    MemRange _table;
    FunctionHeader* _header;
    uint8_t _savedHeader[PATCHABLE_ENTRY_SIZE];
    
    uint8_t* _stackPad;		//< The address of the stack pad value for this function
    
    FunctionLocation* _current;

    std::vector<TextReloc> _textRelocs;
    
    /**
     * \brief Place a jump instruction to forward calls to this function
     * \arg target The destination of the jump instruction
     */
    inline void forward(void* target) {
        _header->jumpTo(target);
#if !defined(__x86_64__)
        flush_icache(_header, sizeof(FunctionHeader));
#endif
    }
    
    void copyTo(void* target);

    void applyTextRelocs(void* source, void* dest);
    
public:
    /**
     * \brief Allocate Function objects on the randomized heap
     * \arg sz The object size
     */
    void* operator new(size_t sz) {
        return getDataHeap()->malloc(sz);
    }
    
    /**
     * \brief Free allocated memory to the randomized heap
     * \arg p The object base pointer
     */
    void operator delete(void* p) {
        getDataHeap()->free(p);
    }
    
    /**
    * \brief Create a new runtime representation of a function
    * \arg codeBase The address of the function
    * \arg tableBase The address of the function's relocation table
    * \arg tableSize The size of the function's relocation table
	* \arg stackPad The address of this function's stack pad size
    */
    inline Function(void* codeBase, void* tableBase, uint32_t tableSize, uint8_t* stackPad) :
        _code(codeBase, (size_t)0), _table(tableBase, tableSize), _header(NULL) {
        this->_stackPad = stackPad;
        this->_current = NULL;
    }

    /**
     * Set the exact final-link function extent obtained from the ELF symbol
     * table.  This does not modify executable memory.
     */
    inline void setCodeSize(size_t size) {
        _code = MemRange(_code.base(), size);
    }

    /**
     * Save and replace the patchable entry only after every function's ELF
     * metadata and text relocations have been validated.
     */
    inline void installHeader() {
        if(_code.size() <= PATCHABLE_ENTRY_SIZE) {
            ABORT("Randomized function at %p has no body after its %zu-byte patchable entry (ELF size=%zu)",
                _code.base(), PATCHABLE_ENTRY_SIZE, _code.size());
        }

        // Make the function header writable
        if(mprotect(_code.pageBase(), _code.pageSize(), PROT_READ | PROT_WRITE | PROT_EXEC)) {
            perror("Unable make code writable");
            abort();
        }
        
        // Make a copy of the function header
        memcpy(_savedHeader, _code.base(), sizeof(_savedHeader));
        _header = new(_code.base()) FunctionHeader(this);
    }
    
    /**
     * \brief Free all code locations when deleted
     */
    ~Function();
    
    FunctionLocation* relocate();
    
    /**
     * \brief Place a trap instruction at the beginning of this function
     */
    inline void setTrap() {
        _header->trap();
    }
    
    inline void* getCodeBase() {
        return _code.base();
    }
    
    inline size_t getCodeSize() {
        return _code.size();
    }

    inline void* getCodeBodyBase() {
        return _code.offsetIn(PATCHABLE_ENTRY_SIZE);
    }

    inline size_t getCodeBodySize() {
        return _code.size() - PATCHABLE_ENTRY_SIZE;
    }

    inline void addTextReloc(size_t offset, uint32_t type, int64_t addend, bool internal) {
        _textRelocs.push_back({offset, type, addend, internal});
    }
    
    inline size_t getAllocationSize() {
        return getCodeBodySize();
    }
    
    inline FunctionLocation* getCurrentLocation() {
        return _current;
    }
};

#endif
