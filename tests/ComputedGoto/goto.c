/* A relocated copy must keep executing in itself across computed gotos:
   label-address tables hold original (link-time) block addresses. */
#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) void *where(void) { return __builtin_return_address(0); }

static void *seen[8];

__attribute__((noinline)) int interp(const unsigned char *code) {
    static void *targets[] = { &&op_inc, &&op_dbl, &&op_where, &&op_end };
    int acc = 0, n = 0;
    goto *targets[*code++];
op_inc: acc++; goto *targets[*code++];
op_dbl: acc *= 2; goto *targets[*code++];
op_where: seen[n++] = where(); goto *targets[*code++];
op_end: return acc;
}

int main(void) {
    const unsigned char program[] = {2, 0, 0, 2, 1, 2, 3};
    int result = interp(program);
    uintptr_t original = (uintptr_t)&interp;
    int failures = result != 4;
    for (int i = 0; i < 3; i++) {
        uintptr_t r = (uintptr_t)seen[i];
        if (r >= original && r < original + 4096) {
            printf("FAIL: dispatch %d returned into the original copy (%p)\n", i, seen[i]);
            failures++;
        }
    }
    if (!failures) printf("PASS: computed gotos stay in the relocated copy\n");
    return failures != 0;
}
