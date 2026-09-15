#define _POSIX_C_SOURCE 200809L

#include <stdint.h>

/* These definitions deliberately live in an ordinary native DSO. */
__thread int shared_tls_scalar __attribute__((visibility("default")));
__thread int shared_tls_array[4] __attribute__((visibility("default")));

void shared_tls_write(int value) {
    shared_tls_scalar = value;
    for (int i = 0; i < 4; ++i)
        shared_tls_array[i] = value + i;
}

int shared_tls_read(void) {
    return shared_tls_scalar;
}

void *shared_tls_address(void) {
    return (void *)&shared_tls_scalar;
}
