#if !defined(RUNTIME_FUNCTIONLOCATION_H)
#define RUNTIME_FUNCTIONLOCATION_H

#include <set>

#include "MemRange.h"
#include "Function.h"
#include "CodeWindow.h"

struct FunctionLocation {
private:
    friend class Function;
    
    Function* _f;
    void* _raw;       // Allocation returned by the code heap (freed on release)
    MemRange _memory; // The executing copy: _raw plus a random start offset
    bool _defunct;
    bool _marked;
    
    static inline std::set<FunctionLocation*>& getRegistry() {
        static std::set<FunctionLocation*> _registry;
        return _registry;
    }
    
    static FunctionLocation* find(void* p) {
        for(std::set<FunctionLocation*>::iterator iter = getRegistry().begin(); iter != getRegistry().end(); iter++) {
            FunctionLocation* l = *iter;
            if(l->_memory.contains(p)) {
                return l;
            }
        }
        
        return NULL;
    }
    
public:
    FunctionLocation(Function* f) :  _f(f),
        _raw(getCodeHeap()->malloc(_f->getAllocationSize() + codePlacementPad())),
        _memory(randomizeStart(_raw, _f->getAllocationSize()), _f->getAllocationSize()) {
        if(_raw == NULL) {
            perror("code malloc");
            uintptr_t lo = 0;
            uintptr_t hi = 0;
            if(stabilizer_get_code_window(&lo, &hi)) {
                ABORT("Couldn't allocate memory for function relocation within code window [%p, %p)", (void*)lo, (void*)hi);
            }
            ABORT("Couldn't allocate memory for function relocation");
        }
        
        _defunct = false;
        _marked = false;

        _f->copyTo(_memory.base());
        
        getRegistry().insert(this);
    }
    
    ~FunctionLocation() {
        getCodeHeap()->free(_raw);
    }
    
    /**
     * \brief Allocate FunctionLocation objects on the randomized heap
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
    
    void activate() {
        _f->forward(_memory.base());
    }
    
    void release() {
        _defunct = true;
    }
    
    void* getBase() {
        return _memory.base();
    }
    
    static void mark(void* p) {
        FunctionLocation* l = find(p);
        if(l != NULL) {
            l->_marked = true;
        }
    }
    
    static void sweep() {
        std::set<FunctionLocation*>::iterator iter = getRegistry().begin();
        
        while(iter != getRegistry().end()) {
            FunctionLocation* l = *iter;
            
            if(l->_defunct && !l->_marked) {
                getRegistry().erase(iter++);
                delete l;
            } else {
                l->_marked = false;
                iter++;
            }
        }
    }
    
    static void* adjust(void* p) {
        FunctionLocation* l = find(p);
        if(l != NULL) {
            size_t offset = l->_memory.offsetOf(p);
            return (uint8_t*)l->_f->getCodeBodyBase() + offset;
        } else {
            return p;
        }
    }
};

#endif
