#define _POSIX_C_SOURCE 200809L
#include <lean/lean.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern uint64_t stabilizer_completed_epochs(void) __attribute__((weak));
extern int stabilizer_request_epoch(void) __attribute__((weak));
extern void *stabilizer_code_location(void *) __attribute__((weak));
extern lean_object *lean_integration_clock(void);
extern unsigned lean_integration_clock_calls(void);
extern void *lean_integration_pc(unsigned);

static pthread_t initial_thread;
static lean_object *(*application)(int, char **);
static int calls;

static void require(int ok, const char *reason) {
    if (!ok) {
        fprintf(stderr, "Lean integration: %s\n", reason);
        _Exit(1);
    }
}

static double now(void) {
    struct timespec ts;
    require(clock_gettime(CLOCK_MONOTONIC, &ts) == 0, "clock failed");
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static lean_object *checked_callback(int argc, char **argv) {
    const char *thread = getenv("LEAN_MAIN_USE_THREAD");
    require(thread != NULL, "thread mode must be explicit");
    int same = pthread_equal(initial_thread, pthread_self());
    require(same == (strcmp(thread, "0") == 0), "unexpected callback thread");
    ++calls; /* lean_run_main joins each callback before the next call. */
    return application(argc, argv);
}

static lean_object *integration_run_main(lean_object *(*fn)(int, char **),
                                         int argc, char **argv) {
    initial_thread = pthread_self();
    application = fn;
    lean_object *result = lean_run_main(checked_callback, argc, argv);
    if (!lean_io_result_is_ok(result)) return result;
    lean_dec_ref(result);
    if (stabilizer_completed_epochs) {
        void *old = stabilizer_code_location((void *)fn);
        /* Read the counter after the destination: publication is per-function,
         * so a destination can already belong to the next incomplete pass. */
        uint64_t before = stabilizer_completed_epochs();
        require(before > 0 && old != NULL, "retained code missing");
        const char *interval = getenv("STABILIZER_INTERVAL_MS");
        require(interval != NULL, "epoch mode must be explicit");
        if (strcmp(interval, "0") == 0)
            require(stabilizer_request_epoch(), "epoch request rejected");
        double deadline = now() + 5;
        while (stabilizer_completed_epochs() <= before ||
               stabilizer_code_location((void *)fn) == old) {
            require(now() < deadline, "no completed epoch before deadline");
            struct timespec pause = {0, 100000};
            nanosleep(&pause, NULL);
        }
        require(stabilizer_code_location((void *)fn) != old, "entry did not move");
        fprintf(stderr, "Lean integration: completed epoch crossing\n");
    }
    result = lean_run_main(checked_callback, argc, argv);
    require(calls == 2, "expected two real Lean callbacks");
    require(lean_integration_clock_calls() == 4, "expected four application clock calls");
    void *pc[4];
    for (unsigned i = 0; i < 4; ++i) {
        pc[i] = lean_integration_pc(i);
        require(pc[i] != NULL, "missing application code PC");
    }
    fprintf(stderr, "Lean integration: code PCs %p %p %p %p\n", pc[0], pc[1], pc[2], pc[3]);
    if (stabilizer_completed_epochs) {
        require(pc[0] != pc[2] && pc[1] != pc[3], "application code PCs did not change");
    } else {
        require(pc[0] == pc[2] && pc[1] == pc[3], "native application code PCs changed");
    }
    fprintf(stderr, "Lean integration: two callbacks checked\n");
    return result;
}

/* Keep Lean's generated initialisation/main/finalisation unchanged. Only
 * wrap its call to lean_run_main; lean.h above prevents macro substitution
 * from altering the real runtime declaration. */
#define lean_run_main integration_run_main
#define lean_io_mono_nanos_now lean_integration_clock
#include "Variants.c"
#undef lean_io_mono_nanos_now
#undef lean_run_main
