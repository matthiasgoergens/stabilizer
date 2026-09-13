#include <lean/lean.h>
#include <stddef.h>

extern lean_object *lean_io_mono_nanos_now(void);
static void *clocks[2], *kernels[16];
static unsigned clock_count, kernel_count, tags[16];

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
    if (clock_count < 2) clocks[clock_count] = pc;
    ++clock_count;
    return lean_io_mono_nanos_now();
}

void adler_observer_reset(void) { clock_count = kernel_count = 0; }
unsigned adler_clock_count(void) { return clock_count; }
unsigned adler_kernel_count(void) { return kernel_count; }
void *adler_clock_pc(unsigned i) { return i < clock_count && i < 2 ? clocks[i] : NULL; }
void *adler_kernel_pc(unsigned i) { return i < kernel_count && i < 16 ? kernels[i] : NULL; }
unsigned adler_kernel_tag(unsigned i) { return i < kernel_count && i < 16 ? tags[i] : 0; }
