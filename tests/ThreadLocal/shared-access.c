#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern uint64_t stabilizer_completed_epochs(void) __attribute__((weak));
extern int stabilizer_request_epoch(void) __attribute__((weak));
extern void *stabilizer_code_location(void *) __attribute__((weak));

extern _Thread_local int shared_tls_scalar;
extern _Thread_local int shared_tls_array[4];
extern void shared_tls_write(int value);
extern int shared_tls_read(void);
extern void *shared_tls_address(void);

static pthread_barrier_t barrier;
static void *seen_addresses[3];
static void *stable_addresses[3];

static void require(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "shared-access: %s\n", message);
        _Exit(1);
    }
}

static double monotonic_seconds(void) {
    struct timespec now;
    require(clock_gettime(CLOCK_MONOTONIC, &now) == 0, "monotonic clock");
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

static void wait_barrier(const char *message) {
    int result = pthread_barrier_wait(&barrier);
    require(result == 0 || result == PTHREAD_BARRIER_SERIAL_THREAD, message);
}

/* Keep both direct TLS accesses and the address-taking form in copied code. */
__attribute__((noinline)) static void check_tls(int role) {
    int expected = role * 100 + 7;
    require(shared_tls_scalar == expected, "TLS scalar changed");
    require(shared_tls_read() == expected, "native DSO read disagrees");
    shared_tls_scalar = expected + 1;
    require(shared_tls_read() == expected + 1, "native DSO missed instrumented write");
    shared_tls_scalar = expected;
    for (int i = 0; i < 4; ++i)
        require(shared_tls_array[i] == expected + i, "TLS array changed");
    require(shared_tls_address() == (void *)&shared_tls_scalar,
            "native DSO address disagrees");
    seen_addresses[role] = (void *)&shared_tls_scalar;
}

static void *worker(void *arg) {
    int role = *(int *)arg;
    shared_tls_write(role * 100 + 7);
    for (int round = 0; round < 4; ++round) {
        check_tls(role);
        wait_barrier("TLS arrival barrier failed");
        wait_barrier("TLS release barrier failed");
    }
    return NULL;
}

int main(int argc, char **argv) {
    const int instrumented = stabilizer_completed_epochs != NULL &&
                             stabilizer_request_epoch != NULL &&
                             stabilizer_code_location != NULL;
    require((argc == 1 && !instrumented) ||
            (argc == 2 && strcmp(argv[1], "retained") == 0 && instrumented),
            "unexpected instrumentation mode");
    require(pthread_barrier_init(&barrier, NULL, 3) == 0, "barrier init");

    int roles[2] = {1, 2};
    pthread_t threads[2];
    require(pthread_create(&threads[0], NULL, worker, &roles[0]) == 0,
            "start worker 1");
    require(pthread_create(&threads[1], NULL, worker, &roles[1]) == 0,
            "start worker 2");

    shared_tls_write(7);
    double deadline = monotonic_seconds() + 10.0;
    for (int round = 0; round < 4; ++round) {
        check_tls(0);
        wait_barrier("TLS arrival barrier failed");
        require(seen_addresses[0] != seen_addresses[1] &&
                seen_addresses[0] != seen_addresses[2] &&
                seen_addresses[1] != seen_addresses[2],
                "threads share a TLS address");
        if (round == 0) {
            for (int i = 0; i < 3; ++i)
                stable_addresses[i] = seen_addresses[i];
        } else {
            for (int i = 0; i < 3; ++i)
                require(seen_addresses[i] == stable_addresses[i],
                        "TLS address changed");
        }
        if (instrumented && round < 3) {
            void *old = stabilizer_code_location((void *)check_tls);
            require(old != NULL, "application TLS checker is not instrumented");
            uint64_t before = stabilizer_completed_epochs();
            require(stabilizer_request_epoch() == 1,
                    "manual epoch request rejected");
            while (stabilizer_completed_epochs() <= before)
                require(monotonic_seconds() < deadline,
                        "epoch completion deadline");
            require(stabilizer_code_location((void *)check_tls) != old,
                    "application TLS checker did not move");
        }
        wait_barrier("TLS release barrier failed");
    }

    require(pthread_join(threads[0], NULL) == 0, "join worker 1");
    require(pthread_join(threads[1], NULL) == 0, "join worker 2");
    require(pthread_barrier_destroy(&barrier) == 0, "barrier destroy");
    puts(instrumented ? "shared-access: retained epochs passed" :
         "shared-access: native control passed");
    return 0;
}
