#define _POSIX_C_SOURCE 200809L
#include <lean/lean.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern uint64_t stabilizer_completed_epochs(void) __attribute__((weak));
extern int stabilizer_request_epoch(void) __attribute__((weak));
extern int stabilizer_retained_exhausted(void) __attribute__((weak));
extern void *stabilizer_code_location(void *) __attribute__((weak));
extern lean_object *adler_clock(void);
extern void adler_kernel_probe(unsigned);
extern void adler_observer_reset(void);
extern unsigned adler_clock_count(void), adler_kernel_count(void);
extern void *adler_clock_pc(unsigned), *adler_kernel_pc(unsigned);
extern unsigned adler_kernel_tag(unsigned);

static pthread_t initial_thread;
static lean_object *(*application)(int, char **);
static unsigned calls;

static void require(int ok, const char *reason) {
    if (!ok) {
        fprintf(stderr, "Adler repeated: %s\n", reason);
        _Exit(1);
    }
}

static double now(void) {
    struct timespec ts;
    require(clock_gettime(CLOCK_MONOTONIC, &ts) == 0, "clock failed");
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static lean_object *callback(int argc, char **argv) {
    const char *thread = getenv("LEAN_MAIN_USE_THREAD");
    require(thread && (strcmp(thread, "0") == 0 || strcmp(thread, "1") == 0),
            "explicit thread mode required");
    require(pthread_equal(initial_thread, pthread_self()) == (strcmp(thread, "0") == 0),
            "wrong callback thread");
    ++calls; /* Callback joined before driver reads/reset observer state. */
    return application(argc, argv);
}

static lean_object *repeated_run_main(lean_object *(*fn)(int, char **),
                                      int argc, char **argv) {
    const char *order = getenv("ADLER_ORDER"), *mode = getenv("ADLER_MODE");
    require(argc == 5 && order && mode, "arguments/order/mode missing");
    size_t n = strlen(order);
    require(n >= 8 && n <= 128 && n % 4 == 0, "invalid order length");
    const char tokens[] = "hHdr";
    for (size_t block = 0; block < n; block += 4) {
        unsigned mask = 0;
        for (size_t j = block; j < block + 4; ++j) {
            const char *token = strchr(tokens, order[j]);
            require(token != NULL, "unknown order token");
            mask |= 1u << (token - tokens);
        }
        require(mask == 15, "block must contain h H d r once each");
    }
    int native = strcmp(mode, "native") == 0;
    int sampled = strcmp(mode, "sampled") == 0;
    require(native || sampled || strcmp(mode, "fixed") == 0, "invalid mode");
    require(native == (stabilizer_completed_epochs == NULL), "wrong binary mode");
    const char *interval = getenv("STABILIZER_INTERVAL_MS");
    require(interval && strcmp(interval, "0") == 0, "automatic epochs forbidden");
    char *end;
    unsigned long reps = strtoul(argv[3], &end, 10);
    require(argv[3][0] && !*end && reps <= 8, "invalid repetitions");
    initial_thread = pthread_self();
    application = fn;
    uint64_t initial_epoch = native ? 0 : stabilizer_completed_epochs();
    require(native || initial_epoch > 0, "missing initial generation");
    require(native || !stabilizer_retained_exhausted(), "exhausted before first callback");
    for (size_t i = 0; i < n; ++i) {
        if (sampled && i && i % 4 == 0) {
            uint64_t before = stabilizer_completed_epochs();
            void *old = stabilizer_code_location((void *)fn);
            require(old != NULL && stabilizer_request_epoch(), "epoch request rejected");
            double deadline = now() + 5;
            while (stabilizer_completed_epochs() <= before ||
                   stabilizer_code_location((void *)fn) == old) {
                require(now() < deadline, "epoch deadline");
                struct timespec pause = {0, 100000};
                nanosleep(&pause, NULL);
            }
        }
        uint64_t before = native ? 0 : stabilizer_completed_epochs();
        require(before == initial_epoch + (sampled ? i / 4 : 0), "unexpected epoch count");
        require(native || !stabilizer_retained_exhausted(), "exhausted before invocation");
        unsigned tag;
        char *kernel;
        switch (order[i]) {
        case 'h': case 'H': tag = 1; kernel = "helper"; break;
        case 'd': tag = 2; kernel = "direct"; break;
        case 'r': tag = 3; kernel = "reference"; break;
        default: require(0, "unknown order token"); return NULL;
        }
        char *call_argv[] = {argv[0], kernel, argv[2], argv[3], argv[4], NULL};
        adler_observer_reset();
        lean_object *result = lean_run_main(callback, 5, call_argv);
        if (!lean_io_result_is_ok(result)) return result;
        require(calls == i + 1, "callback count mismatch");
        require(adler_clock_count() == 2 && adler_kernel_count() == reps + 1,
                "observer count mismatch");
        require(native || (stabilizer_completed_epochs() == before &&
                           !stabilizer_retained_exhausted()), "epoch changed or exhausted");
        fprintf(stderr, "{\"invocation\":%zu,\"label\":\"%c\",\"epoch\":%llu,"
                "\"exhausted\":0,\"clocks\":[\"%p\",\"%p\"],\"kernel_pcs\":[",
                i, order[i], (unsigned long long)before, adler_clock_pc(0), adler_clock_pc(1));
        for (unsigned j = 0; j <= reps; ++j) {
            require(adler_kernel_tag(j) == tag && adler_kernel_pc(j), "kernel tag/PC mismatch");
            fprintf(stderr, "%s\"%p\"", j ? "," : "", adler_kernel_pc(j));
        }
        fprintf(stderr, "]}\n");
        if (i + 1 == n) return result;
        lean_dec_ref(result);
    }
    return NULL;
}

/* Keep generated initialisation/finalisation. Injected probes only add
 * observations at generated helper/direct/reference function entry. */
#define lean_run_main repeated_run_main
#define lean_io_mono_nanos_now adler_clock
#include "Adler.c"
#undef lean_io_mono_nanos_now
#undef lean_run_main
