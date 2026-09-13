#include <lean/lean.h>
#include <stddef.h>

extern lean_object *lean_io_mono_nanos_now(void);

static void *pcs[4];
static unsigned calls;

/* Compile this file as a native DSO, never through szc. The saved return PC
 * is then in the generated application's actual clock-calling code copy.
 * Only the callback writes these records; the driver reads them after join. */
__attribute__((noinline)) lean_object *lean_integration_clock(void) {
    void *pc = __builtin_extract_return_addr(__builtin_return_address(0));
#ifdef LEAN_INTEGRATION_FAKE_PC
    /* Negative control: an observer that never reports a moved call site. */
    pc = (void *)1;
#endif
    if (calls < 4) pcs[calls] = pc;
    ++calls;
    return lean_io_mono_nanos_now();
}

unsigned lean_integration_clock_calls(void) { return calls; }

void *lean_integration_pc(unsigned index) {
    return index < 4 ? pcs[index] : NULL;
}
