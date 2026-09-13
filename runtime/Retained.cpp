#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <pthread.h>
#include <set>
#include <time.h>
#include <unistd.h>

#include "Function.h"
#include "FunctionLocation.h"
#include "Instrumentation.h"
#include "Retained.h"

extern std::set<Function*> functions;

namespace {
// Startup is quiescent by contract. After configuration these values and the
// function set are immutable. Only the owner mutates Function/Location state.
bool enabled = false;
bool registration_closed = false;
uint32_t module_flags = 0;
uint32_t module_count = 0;
uint64_t epoch_limit = 8;
uint64_t byte_limit = 64 * 1024 * 1024;
uint64_t period_ms = 500;
uint64_t epoch_bytes = 0;
std::atomic<uint64_t> completed{0};
std::atomic<bool> exhausted{false};
pthread_mutex_t owner_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t changed;
pthread_t owner;
bool started = false;
bool joining = false;
bool stopping = false;
bool requested = false;
uint64_t reserved_epoch = 1; // Under owner_mutex, includes an in-flight publication.

// Rejection must not allocate or inspect owner-mutated code registries.
void message(const char* text) {
    size_t length = std::strlen(text);
    while(length) {
        ssize_t written = write(STDERR_FILENO, text, length);
        if(written < 0 && errno == EINTR) continue;
        if(written <= 0) break;
        text += written;
        length -= size_t(written);
    }
}

[[noreturn]] void fail(const char* text) {
    message(text);
    _Exit(78);
}

uint64_t option(const char* name, uint64_t fallback, uint64_t minimum,
                uint64_t maximum) {
    const char* text = std::getenv(name);
    if(!text) return fallback;
    if(!*text) fail("Stabilizer retained mode: empty numeric option\n");
    uint64_t value = 0;
    for(const char* p = text; *p; ++p) {
        if(*p < '0' || *p > '9' || value > maximum / 10 ||
           (value == maximum / 10 && uint64_t(*p - '0') > maximum % 10))
            fail("Stabilizer retained mode: invalid numeric option\n");
        value = value * 10 + uint64_t(*p - '0');
    }
    if(value < minimum || value > maximum)
        fail("Stabilizer retained mode: numeric option out of range\n");
    return value;
}

void publish_epoch() {
    for(Function* f : functions) {
        // Never release/sweep an old location in this experimental mode. A
        // native caller may retain a return address into it indefinitely.
        (void)f->relocate();
    }
    uint64_t epoch = completed.fetch_add(1, std::memory_order_release) + 1;
    if(epoch == epoch_limit) {
        exhausted.store(true, std::memory_order_release);
        // Do not write diagnostics from the owner: a full stderr pipe could
        // otherwise block shutdown's join. Exhaustion is an observable API state.
    }
}

#if defined(__linux__) && defined(__x86_64__)
void* maintain(void*) {
    if(pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, nullptr))
        fail("Stabilizer retained mode: cannot disable owner cancellation\n");
    pthread_mutex_lock(&owner_mutex);
    while(!stopping && !exhausted.load(std::memory_order_acquire)) {
        timespec deadline = {};
        if(period_ms) {
            if(clock_gettime(CLOCK_MONOTONIC, &deadline))
                fail("Stabilizer retained mode: clock_gettime failed\n");
            deadline.tv_sec += time_t(period_ms / 1000);
            deadline.tv_nsec += long(period_ms % 1000) * 1000000;
            if(deadline.tv_nsec >= 1000000000) {
                ++deadline.tv_sec;
                deadline.tv_nsec -= 1000000000;
            }
        }
        while(!stopping && !requested) {
            int status = period_ms ? pthread_cond_timedwait(&changed, &owner_mutex, &deadline)
                                   : pthread_cond_wait(&changed, &owner_mutex);
            if(status == ETIMEDOUT) break;
            if(status) fail("Stabilizer retained mode: condition wait failed\n");
        }
        if(stopping) break;
        requested = false;
        ++reserved_epoch;
        pthread_mutex_unlock(&owner_mutex);
        publish_epoch();
        pthread_mutex_lock(&owner_mutex);
    }
    pthread_mutex_unlock(&owner_mutex);
    return nullptr;
}

void reject_fork() {
    fail("Stabilizer retained mode: fork is not supported\n");
}
#endif
}

extern "C" void stabilizer_register_module(uint32_t abi, uint32_t flags) {
    retained_guard_registration();
    if(abi != STABILIZER_MODULE_ABI ||
       (flags & ~(STABILIZER_CODE | STABILIZER_HEAP | STABILIZER_STACK)))
        fail("Stabilizer: unsupported module instrumentation ABI/flags\n");
    ++module_count;
    module_flags |= flags;
}

void retained_guard_registration() {
    if(registration_closed)
        fail("Stabilizer retained mode: registration after startup is not supported\n");
}

void retained_note_stack() { module_flags |= STABILIZER_STACK; }

void retained_guard_heap() {
    // Also catches old unreported heap instrumentation on first use, before
    // it can race the owner's metadata allocations from getDataHeap().
    if(enabled) fail("Stabilizer retained mode: heap instrumentation is not supported\n");
}

bool retained_configure() {
    const char* mode = std::getenv("STABILIZER_CODE_MODE");
    if(!mode || std::strcmp(mode, "legacy") == 0) return false;
    if(std::strcmp(mode, "retained") != 0)
        fail("Stabilizer: STABILIZER_CODE_MODE must be legacy or retained\n");
#if !(defined(__linux__) && defined(__x86_64__))
    fail("Stabilizer retained mode requires Linux x86_64\n");
#endif
    if(!module_count || module_flags != STABILIZER_CODE || functions.empty())
        fail("Stabilizer retained mode requires code-only instrumentation and current module metadata\n");
    epoch_limit = option("STABILIZER_MAX_EPOCHS", 8, 1, 1000000);
    byte_limit = option("STABILIZER_MAX_CODE_BYTES", 64 * 1024 * 1024, 1, uint64_t(1) << 30);
    period_ms = option("STABILIZER_INTERVAL_MS", 500, 0, 3600000);
    enabled = true;
    registration_closed = true;
    message("Stabilizer experimental retained mode: bounded sampling; inspect epoch/exhaustion counters\n");
    return true;
}

void retained_validate_layout() {
    for(Function* f : functions) {
        size_t size = f->getAllocationSize();
        if(size > byte_limit - epoch_bytes)
            fail("Stabilizer retained mode: initial layout exceeds code-byte budget\n");
        epoch_bytes += size;
    }
    if(!epoch_bytes) fail("Stabilizer retained mode: empty layout\n");
    uint64_t fitting = byte_limit / epoch_bytes;
    if(fitting < epoch_limit) epoch_limit = fitting;
}

void retained_start() {
#if defined(__linux__) && defined(__x86_64__)
    // Initialise the location registry and heaps before registering cleanup,
    // so cleanup runs before their atexit destructors. No application code has
    // entered an installed header yet: all initial destinations are prepared.
    publish_epoch();
    pthread_condattr_t attr;
    if(pthread_condattr_init(&attr) || pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) ||
       pthread_cond_init(&changed, &attr))
        fail("Stabilizer retained mode: condition initialisation failed\n");
    pthread_condattr_destroy(&attr);
    if(std::atexit(retained_stop) || pthread_atfork(reject_fork, nullptr, nullptr))
        fail("Stabilizer retained mode: lifecycle registration failed\n");
    if(!exhausted.load()) {
        if(pthread_create(&owner, nullptr, maintain, nullptr))
            fail("Stabilizer retained mode: owner creation failed\n");
        started = true;
    }
#else
    // Legacy targets still build this translation unit. In particular, Darwin
    // does not provide pthread_condattr_setclock; do not compile the Linux-only
    // owner merely because retained_configure() would reject it at run time.
    fail("Stabilizer retained mode requires Linux x86_64\n");
#endif
}

void retained_stop() {
    if(!enabled) return;
    pthread_mutex_lock(&owner_mutex);
    while(joining) pthread_cond_wait(&changed, &owner_mutex);
    if(!started) {
        pthread_mutex_unlock(&owner_mutex);
        return;
    }
    stopping = true;
    joining = true;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&owner_mutex);
    if(pthread_join(owner, nullptr)) fail("Stabilizer retained mode: owner join failed\n");
    pthread_mutex_lock(&owner_mutex);
    started = false;
    joining = false;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&owner_mutex);
    // No code is reclaimed: later exit callbacks may still use any generation.
}

extern "C" uint64_t stabilizer_completed_epochs() {
    return completed.load(std::memory_order_acquire);
}
extern "C" int stabilizer_retained_exhausted() {
    return exhausted.load(std::memory_order_acquire);
}
extern "C" int stabilizer_request_epoch() {
    if(!enabled) return 0;
    pthread_mutex_lock(&owner_mutex);
    bool accepted = started && !stopping && reserved_epoch < epoch_limit;
    if(accepted) {
        requested = true;
        pthread_cond_signal(&changed);
    }
    pthread_mutex_unlock(&owner_mutex);
    return accepted;
}
extern "C" void* stabilizer_code_location(void* entry) {
    if(!enabled) return nullptr;
    // Immutable function set after admission; do not read owner-only _current.
    for(Function* f : functions) {
        if(f->getCodeBase() == entry) return f->getPublishedLocation();
    }
    return nullptr;
}
