#include <set>
#include <vector>
#include <cmath>
#include <signal.h>
#include <cstdlib>
#include <execinfo.h>
#include <sys/time.h>

#include "Function.h"
#include "FunctionLocation.h"
#include "Debug.h"
#include "Heap.h"
#include "CodeWindow.h"
#include "Context.h"
#include "TextRelocations.h"

extern "C" int stabilizer_main(int argc, char **argv);

int main(int argc, char** argv);

void onTrap(int sig, siginfo_t* info, void*);
void onTimer(int sig, siginfo_t* info, void*);
void onFault(int sig, siginfo_t* info, void*);

void setTimer(int msec);
void setHandler(int sig, void(*fn)(int, siginfo_t*, void*));

typedef void(*ctor_t)();

std::set<Function*> functions;
std::set<Function*> live_functions;
std::set<uint8_t*> stack_pads;
std::vector<ctor_t> constructors;

bool rerandomizing = false;
size_t interval = 500;

// Set once stabilizer_main returns, so a SIGALRM still in flight during
// process teardown is a no-op instead of relocating/trapping torn-down code.
volatile sig_atomic_t shutting_down = 0;

void** topFrame = NULL;

/**
 * Entry point for a program run with Stabilizer.  The program's existing
 * main function has been renamed 'stabilizer_main' by the compiler pass.
 *
 * 1. Save the current top of the stack
 * 2. Set signal handlers for debug traps, timers, and segfaults for error handling
 * 3. Place a trap instruction at the start of each randomizable function to trigger relocation on-demand
 * 4. Set the re-randomization timer
 * 5. Call module constructors
 * 6. Invoke stabilizer_main
 */
int main(int argc, char **argv) {
    DEBUG("Initializing Stabilizer");

    topFrame = (void**)__builtin_frame_address(0);
    DEBUG("Stack top is at %p", topFrame);

    // Register signal handlers
    setHandler(Trap::TrapSignal, onTrap);
    setHandler(SIGALRM, onTimer);
    setHandler(SIGSEGV, onFault);
    DEBUG("Signal handlers installed");

#if !(defined(__linux__) && defined(__x86_64__))
    if(!functions.empty()) {
        ABORT("Code randomization (-Rcode) requires ELF text relocation fixups, which are currently only implemented on Linux x86_64. Disable -Rcode.");
    }
#endif

    // If code randomization is enabled, we must ensure relocated code stays
    // within range of x86_64 pc-relative relocations (signed 32-bit).
    if(!functions.empty()) {
        uintptr_t min_addr = (uintptr_t)-1;
        uintptr_t max_addr = 0;

        for(Function* f : functions) {
            uintptr_t base = (uintptr_t)f->getCodeBase();
            uintptr_t end = base + (uintptr_t)f->getCodeSize();

            if(base < min_addr) {
                min_addr = base;
            }
            if(end > max_addr) {
                max_addr = end;
            }
        }

        const uintptr_t ONE_GB = (uintptr_t)1 << 30;
        const uintptr_t TWO_GB = (uintptr_t)1 << 31;

        // If the randomized text span itself exceeds the pc-relative reach,
        // we cannot safely relocate without more invasive rewriting.
        if(max_addr < min_addr || (max_addr - min_addr) >= TWO_GB) {
            ABORT("Executable text span too large for 32-bit pc-relative relocations (span=%zu bytes, [%p, %p)). Code randomization requires all randomized code to fit within a 2GB window. Disable -Rcode.",
                (size_t)(max_addr - min_addr), (void*)min_addr, (void*)max_addr);
        }

        uintptr_t span = max_addr - min_addr;
        uintptr_t center = min_addr + span / 2;

        uintptr_t lo = (center > ONE_GB) ? (center - ONE_GB) : 0;
        uintptr_t hi = (center > (uintptr_t)-1 - ONE_GB) ? (uintptr_t)-1 : (center + ONE_GB);

        // Align to pages.
        uintptr_t mask = ~((uintptr_t)PAGESIZE - 1u);
        lo &= mask;
        hi &= mask;

        if(hi > lo) {
            stabilizer_set_code_window(lo, hi);
            DEBUG("Code allocation window: [%p, %p)", (void*)lo, (void*)hi);
        }
    }

    // Pre-compute relocation fixups for randomized code.
    // Code randomization requires ELF text relocations (link with --emit-relocs).
    if(!functions.empty()) {
        if(!stabilizer_init_text_relocations(functions)) {
          ABORT("Unable to initialize ELF text relocations required for code "
                "randomization. This requires Linux x86_64, reading "
                "/proc/self/exe, and linking with -Wl,--emit-relocs (szc adds "
                "this automatically for -Rcode). If unsupported relocation "
                "types are encountered inside randomized functions, Stabilizer "
                "will ABORT with a separate diagnostic. Rebuild without -Rcode "
                "if you cannot provide relocations.");
        }
    }

    // Lazily relocate functions
    for(std::set<Function*>::iterator iter = functions.begin(); iter != functions.end(); iter++) {
        Function* f = *iter;
        f->setTrap();
    }
    DEBUG("Trapped all functions");

    // Set the re-randomization timer
    setTimer(interval);
    DEBUG("Set re-randomization timer");

    // Call all constructors
    for(std::vector<ctor_t>::iterator i = constructors.begin(); i != constructors.end(); i++) {
        (*i)();
    }
    DEBUG("Finished with program constructors");

    // Call the old main function
    int r = stabilizer_main(argc, argv);

    // Disarm re-randomization before teardown. A SIGALRM delivered after
    // stabilizer_main returns would run onTimer against state that is being
    // torn down (the "Placing traps" path writes traps into function memory
    // that may already be unmapped, faulting in onFault). Set the flag first
    // so an alarm already in flight is a no-op, then stop the timer.
    shutting_down = 1;
    setTimer(0);

    DEBUG("Shutting down");

    return r;
}

extern "C" {
    void stabilizer_register_function(void* codeBase, void* codeLimit, void* tableBase, size_t tableSize, bool adjacent, uint8_t* stackPad) {
        Function* f = new Function(codeBase, codeLimit, tableBase, tableSize, adjacent, stackPad);
        functions.insert(f);
    }

    void stabilizer_register_constructor(ctor_t ctor) {
        constructors.push_back(ctor);
    }

    void stabilizer_register_stack_pad(uint8_t* pad) {
        stack_pads.insert(pad);
    }

    void* stabilizer_malloc(size_t sz) {
        return getDataHeap()->malloc(sz);
    }

    void* stabilizer_calloc(size_t n, size_t sz) {
        return getDataHeap()->calloc(n, sz);
    }

    void* stabilizer_realloc(void *p, size_t sz) {
        return getDataHeap()->realloc(p, sz);
    }

    void stabilizer_free(void *p) {
        if(getDataHeap()->getSize(p) == 0) {
            free(p);
        } else {
            getDataHeap()->free(p);
        }
    }

    void reportDoubleFreeError() {
        ABORT("Double free error");
    }
}

void onTrap(int sig, siginfo_t* info, void* p) {
    Context c(p);

    // Back up over the trap instruction
    c.ip() = (void*)((uintptr_t)c.ip() - Trap::TrapAdjust);

    // Extract the trapped function (stored next to the trap instruction)
    FunctionHeader* h = (FunctionHeader*)c.ip();
    Function* f = h->getFunction();

    // If the trap was placed to trigger a re-randomization
    if(rerandomizing) {
        DEBUG("Re-randomization started after trap on %p", c.ip());

        // Mark all on-stack function locations as used
        Stack s = c.stack();
        while(s.fp() != topFrame) {
            FunctionLocation::mark(s.ret());
            s++;
        }

        // Mark the current instruction pointer as used
        FunctionLocation::mark((void*)c.ip());

        // Mark the top return address on the stack as used
        FunctionLocation::mark(*(void**)c.sp());

        // Collect unused function locations
        FunctionLocation::sweep();

        rerandomizing = false;
        setTimer(interval);
    }

    // Relocate the function
    FunctionLocation* oldLocation = f->relocate();
    live_functions.insert(f);

    if(oldLocation != NULL) {
        oldLocation->release();
    }

    c.ip() = f->getCurrentLocation()->getBase();
}

void onTimer(int sig, siginfo_t* info, void* p) {
    // If the program is tearing down, do nothing: relocating or trapping
    // functions here races with unmapping of their code.
    if(shutting_down) {
        return;
    }

    Context c(p);

    DEBUG("Re-randomization timer fired at %p", c.ip());

    if(functions.size() == 0) {
        DEBUG("Re-randomizing stack pads");
        for(std::set<uint8_t*>::iterator iter = stack_pads.begin(); iter != stack_pads.end(); iter++) {
            uint8_t* pad = *iter;
            **iter = getRandomByte();
        }

        setTimer(interval);

    } else {
        DEBUG("Placing traps");
        for(std::set<Function*>::iterator iter = live_functions.begin(); iter != live_functions.end(); iter++) {
            Function* f = *iter;
            if(c.ip() == f->getCodeBase()) {
                DEBUG("Forwarding from trap at %p", c.ip());
                c.ip() = f->getCurrentLocation()->getBase();
            }
            f->setTrap();
        }

        live_functions.clear();
    }

    rerandomizing = true;
}

void onFault(int sig, siginfo_t* info, void* p) {
    Context c(p);
    ABORT("Fault at %p, accessing address %p", c.ip(), info->si_addr);
}

void setTimer(int msec) {
    struct itimerval timer;

    timer.it_value.tv_sec = (msec - msec % 1000) / 1000;
    timer.it_value.tv_usec = 1000 * (msec % 1000);
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;

    setitimer(ITIMER_REAL, &timer, 0);
}

void setHandler(int sig, void(*fn)(int, siginfo_t*, void*)) {
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = (void(*)(int, siginfo_t*, void*))fn;
    sa.sa_flags = SA_SIGINFO;
    sigaction(sig, &sa, NULL);
}
