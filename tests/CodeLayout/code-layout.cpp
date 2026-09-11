#include <cstdio>
#include <cstdlib>
#include <unistd.h>

static bool constructed = false;
static volatile int input = 41;

struct Global {
    Global() {
        constructed = true;
    }

    ~Global() {
        std::puts("code-layout destructor");
    }
};

Global global;

__attribute__((noinline)) static int increment(int value) {
    return value + 1;
}

int main() {
    if(!constructed || increment(input) != 42) {
        return EXIT_FAILURE;
    }

    // Cross the runtime's 500 ms re-randomisation interval, then call the
    // already-relocated tiny function again to exercise a second epoch.
    usleep(600000);
    if(increment(input) != 42) {
        return EXIT_FAILURE;
    }

    std::puts("code-layout main");
    return EXIT_SUCCESS;
}
