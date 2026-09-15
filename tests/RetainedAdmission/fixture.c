#include <stdlib.h>

/* Compile this TU separately with heap-only or stack-only instrumentation.
 * Its constructor must report the mixed mode before the code-only main runs.
 */
int fixture_marker(void) {
    volatile int stack_value[8];
    int* allocation = malloc(sizeof(*allocation));
    if(!allocation) return -1;
    *allocation = 42;
    stack_value[0] = *allocation;
    free(allocation);
    return stack_value[0];
}
