#define _POSIX_C_SOURCE 200809L

#include <lean/lean.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern lean_object *adler_clock(void);
extern void adler_observer_reset(void);
extern unsigned adler_clock_count(void);
extern uint64_t adler_thread_cpu_before(unsigned);
extern uint64_t adler_thread_cpu_after(unsigned);

static struct timespec samples[8];
static unsigned sample_count;
static int fail_at = -1;
static unsigned requested;
static unsigned wall_count;
static void require(int condition, const char *message);

int observer_test_clock_gettime(clockid_t id, struct timespec *ts) {
    if (id != CLOCK_THREAD_CPUTIME_ID) return -1;
    ++requested;
    unsigned i = sample_count++;
    require(i < 8, "too many CPU calls");
    if ((int)i == fail_at) return -1;
    *ts = samples[i];
    return 0;
}

lean_object *lean_io_mono_nanos_now(void) {
    require(sample_count == 2 * wall_count + 1, "wall call not bracketed");
    ++wall_count;
    return (lean_object *)(uintptr_t)(0x12345678 + 16 * wall_count);
}

static void require(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "observer-test: %s\n", message);
        exit(1);
    }
}

static void set_sample(unsigned i, int64_t sec, long nsec) {
    samples[i].tv_sec = sec;
    samples[i].tv_nsec = nsec;
}

static void positive(void) {
    set_sample(0, 5, 10);
    set_sample(1, 5, 20);
    set_sample(2, 4294967297LL, 30);
    set_sample(3, 4294967297LL, 40);
    sample_count = requested = 0;
    adler_observer_reset();
    require(adler_clock() == (lean_object *)(uintptr_t)0x12345688, "first result changed");
    require(adler_clock() == (lean_object *)(uintptr_t)0x12345698, "second result changed");
    require(requested == 4 && sample_count == 4, "clock calls missing");
    require(wall_count == 2, "wall calls missing");
    require(adler_clock_count() == 2, "clock count mismatch");
    require(adler_thread_cpu_before(0) == 5000000010ULL, "first before mismatch");
    require(adler_thread_cpu_after(0) == 5000000020ULL, "first after mismatch");
    require(adler_thread_cpu_before(1) == 4294967297000000030ULL,
            "second before mismatch");
    require(adler_thread_cpu_after(1) == 4294967297000000040ULL,
            "second after mismatch");
}

static void extra(void) {
    for (unsigned i = 0; i < 6; ++i) set_sample(i, 10 + i, 1);
    sample_count = requested = 0;
    adler_observer_reset();
    require(adler_clock() != NULL && adler_clock() != NULL && adler_clock() != NULL,
            "extra calls failed");
    require(adler_clock_count() == 3 && requested == 6, "extra count mismatch");
    require(wall_count == 3, "extra wall count mismatch");
    require(adler_thread_cpu_before(0) == 10000000001ULL &&
            adler_thread_cpu_after(1) == 13000000001ULL, "extra call overwrote slots");
    require(adler_thread_cpu_before(2) == 0 && adler_thread_cpu_after(2) == 0,
            "extra CPU slot was retained");
    adler_observer_reset();
    require(adler_clock_count() == 0 && adler_thread_cpu_before(0) == 0 &&
            adler_thread_cpu_after(0) == 0, "reset did not clear getters");
    set_sample(6, 20, 1);
    set_sample(7, 20, 2);
    (void)adler_clock();
    require(adler_thread_cpu_before(0) == 20000000001ULL &&
            adler_thread_cpu_after(0) == 20000000002ULL &&
            adler_thread_cpu_before(1) == 0 && adler_thread_cpu_after(1) == 0,
            "reset leaked stale sample");
}

int main(int argc, char **argv) {
    require(argc == 2, "mode required");
    if (strcmp(argv[1], "positive") == 0) {
        positive();
        return 0;
    }
    if (strcmp(argv[1], "extra") == 0) {
        extra();
        return 0;
    }
    if (strcmp(argv[1], "maximum") == 0) {
        set_sample(0, 18446744073LL, 709551615);
        set_sample(1, 18446744073LL, 709551615);
        (void)adler_clock();
        require(adler_thread_cpu_before(0) == UINT64_MAX &&
                adler_thread_cpu_after(0) == UINT64_MAX, "maximum uint64 rejected");
        return 0;
    }
    if (strncmp(argv[1], "fail-", 5) == 0) {
        fail_at = atoi(argv[1] + 5);
        for (unsigned i = 0; i < 4; ++i) set_sample(i, 1, 1);
        sample_count = requested = 0;
        adler_observer_reset();
        (void)adler_clock();
        if (fail_at >= 2) (void)adler_clock();
        return 2;
    }
    if (strcmp(argv[1], "invalid-negative") == 0) set_sample(0, -1, 0);
    else if (strcmp(argv[1], "invalid-negative-nsec") == 0) set_sample(0, 1, -1);
    else if (strcmp(argv[1], "invalid-nsec") == 0) set_sample(0, 1, 1000000000L);
    else if (strcmp(argv[1], "invalid-overflow") == 0)
        set_sample(0, INT64_MAX, 999999999L);
    else if (strcmp(argv[1], "invalid-carry-overflow") == 0)
        set_sample(0, 18446744073LL, 709551616);
    else return 2;
    set_sample(1, 1, 1);
    sample_count = requested = 0;
    adler_observer_reset();
    (void)adler_clock();
    return 2;
}
