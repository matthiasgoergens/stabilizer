#include <stdint.h>
#include <stdio.h>

// Weak initial alignment keeps the integer expression alive through the
// optimiser. The Stabilizer pass subsequently guarantees 8-byte alignment.
__attribute__((noinline, aligned(1)))
int weak_alignment(int n) { return n + 1; }

int main(int argc, char** argv) {
    (void)argv;
    if((uintptr_t)&weak_alignment % 8 != 0) return 1;
    int (*volatile callback)(int) = weak_alignment;
    if(callback(argc) != argc + 1) return 1;
    puts("integer and pointer relocation values passed");
    return 0;
}
