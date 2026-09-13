#define _POSIX_C_SOURCE 200809L
#include <lean/lean.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

extern lean_object *lean_io_mono_nanos_now(void);
static void *clocks[2], *kernels[16];
static unsigned clock_count, kernel_count, tags[16];
static uint64_t cpu_before[2], cpu_after[2];

static uint64_t thread_cpu_now(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) {
        fputs("Adler observer: thread CPU clock failed\n", stderr);
        _Exit(1);
    }
    if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000 ||
        (uint64_t)ts.tv_sec > (UINT64_MAX - (uint64_t)ts.tv_nsec) / 1000000000) {
        fputs("Adler observer: invalid thread CPU time\n", stderr);
        _Exit(1);
    }
    return (uint64_t)ts.tv_sec * 1000000000 + (uint64_t)ts.tv_nsec;
}

/* Native DSO: the return PCs belong to the instrumented callers, even if
 * Clang inlines a generated kernel into another application function. */
__attribute__((noinline)) void adler_kernel_probe(unsigned tag) {
    void *pc = __builtin_extract_return_addr(__builtin_return_address(0));
#ifdef ADLER_FAKE_PC
    pc = (void *)1;
#endif
    if (kernel_count < 16) {
        kernels[kernel_count] = pc;
        tags[kernel_count] = tag;
    }
    ++kernel_count;
}

__attribute__((noinline)) lean_object *adler_clock(void) {
    void *pc = __builtin_extract_return_addr(__builtin_return_address(0));
    unsigned i = clock_count;
    if (i < 2) clocks[i] = pc;
    ++clock_count;
    /* Bracket the original wall-clock call, including its Lean result
     * construction. These reads add observer cost; they do not subtract it.
     * For consecutive calls, CPU delta lies between before[1]-after[0]
     * and after[1]-before[0], subject to clock resolution/accounting error.
     * State is read/reset only after the one callback thread is joined. */
    uint64_t before = thread_cpu_now();
    lean_object *result = lean_io_mono_nanos_now();
    uint64_t after = thread_cpu_now();
    if (i < 2) {
        cpu_before[i] = before;
        cpu_after[i] = after;
    }
    return result;
}

void adler_observer_reset(void) { clock_count = kernel_count = 0; }
unsigned adler_clock_count(void) { return clock_count; }
unsigned adler_kernel_count(void) { return kernel_count; }
void *adler_clock_pc(unsigned i) { return i < clock_count && i < 2 ? clocks[i] : NULL; }
void *adler_kernel_pc(unsigned i) { return i < kernel_count && i < 16 ? kernels[i] : NULL; }
unsigned adler_kernel_tag(unsigned i) { return i < kernel_count && i < 16 ? tags[i] : 0; }
uint64_t adler_thread_cpu_before(unsigned i) { return i < clock_count && i < 2 ? cpu_before[i] : 0; }
uint64_t adler_thread_cpu_after(unsigned i) { return i < clock_count && i < 2 ? cpu_after[i] : 0; }
