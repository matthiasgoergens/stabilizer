#include <stddef.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>

extern void* stabilizer_malloc(size_t);
extern void stabilizer_free(void*);
extern void stabilizer_register_module(uint32_t, uint32_t);

/* Deliberately compiled natively: these calls carry no module metadata. */
int fixture_marker(void) {
#if defined(LEGACY_HEAP)
    void* allocation = stabilizer_malloc(8);
    stabilizer_free(allocation);
#elif defined(LATE_REGISTRATION)
    stabilizer_register_module(1, 1);
#elif defined(FORK)
    pid_t child = fork();
    if(child == 0) _exit(0);
    if(child < 0) return -1;
    int status;
    if(waitpid(child, &status, 0) != child) return -1;
#endif
    return 42;
}
