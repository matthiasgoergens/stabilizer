#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int tiny(int x) { return x + 1; }

__attribute__((noinline)) int user_noinline(int x) { return x + 2; }

int public_sum(int x) { return tiny(x) + user_noinline(x); }

int read_value(const int *p) { return *p; }

int frontend_optimised(void) {
#ifdef __OPTIMIZE__
    return 1;
#else
    return 0;
#endif
}

#ifdef __cplusplus
}
#endif

int main(void) {
    volatile int input = 21;
    printf("%d %d\n", frontend_optimised(), public_sum(input));
    return 0;
}
