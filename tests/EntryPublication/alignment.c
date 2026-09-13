#include <stdio.h>

__attribute__((noinline, aligned(1), section(".stabilizer_bad_alignment")))
int weak_alignment(int n) { return n + 1; }

int main(int argc, char** argv) {
    (void)argv;
    if(weak_alignment(argc) != argc + 1) return 1;
    puts("aligned entry passed");
    return 0;
}
