#define _POSIX_C_SOURCE 200809L
#include "native-helper.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern uint64_t stabilizer_completed_epochs(void);
extern int stabilizer_retained_exhausted(void);
extern int stabilizer_request_epoch(void);
extern void *stabilizer_code_location(void *original_entry);

static _Atomic unsigned long checks;
static _Atomic int stop_checks;
static _Atomic uint64_t first_checksum_epoch = UINT64_MAX;
static _Atomic uint64_t last_checksum_epoch;
static int completed;
static int early_shutdown;

static void require(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "retained-code: %s\n", message);
        _Exit(1);
    }
}

__attribute__((noinline)) static uint64_t checksum(uint64_t value) {
    return (value * UINT64_C(6364136223846793005)) ^ UINT64_C(1442695040888963407);
}

__attribute__((noinline)) static uint64_t suspended_callback(uint64_t value) {
    void *body = stabilizer_code_location((void *)suspended_callback);
    require(body != NULL, "callback has no published body");
    native_wait_with_return(body);
    /* Deliberate work after the native call: its return cannot be a tail call. */
    return checksum(value) + 1;
}

static void *suspended_worker(void *unused) {
    (void)unused;
    uint64_t expected = (UINT64_C(41) * UINT64_C(6364136223846793005)) ^ UINT64_C(1442695040888963407);
    require(suspended_callback(41) == expected + 1, "old-generation return checksum");
    return NULL;
}

static void *checksum_worker(void *unused) {
    (void)unused;
    uint64_t value = 0;
    while (!atomic_load_explicit(&stop_checks, memory_order_acquire)) {
        uint64_t epoch = stabilizer_completed_epochs();
        uint64_t expected = (value * UINT64_C(6364136223846793005)) ^ UINT64_C(1442695040888963407);
        require(checksum(value) == expected, "concurrent checksum");
        if (value == 0)
            atomic_store_explicit(&first_checksum_epoch, epoch, memory_order_release);
        atomic_store_explicit(&last_checksum_epoch, epoch, memory_order_release);
        atomic_fetch_add_explicit(&checks, 1, memory_order_relaxed);
        ++value;
    }
    return NULL;
}

static double monotonic_seconds(void) {
    struct timespec now;
    require(clock_gettime(CLOCK_MONOTONIC, &now) == 0, "monotonic clock");
    return now.tv_sec + now.tv_nsec / 1e9;
}

static void exit_check(void) {
    require(completed, "main did not finish checks");
    if (early_shutdown) {
        require(stabilizer_completed_epochs() >= 2, "automatic epoch missing at exit");
        require(!stabilizer_retained_exhausted(), "early exit exhausted its budget");
    } else {
        require(stabilizer_completed_epochs() == 4, "epoch counter changed at exit");
    }
    require(checksum(0) == UINT64_C(1442695040888963407), "atexit executable code");
    puts("retained-code: atexit checksum passed");
}

int main(int argc, char **argv) {
    early_shutdown = argc == 2 && (strcmp(argv[1], "early-return") == 0 || strcmp(argv[1], "early-exit") == 0);
    require(argc == 1 || early_shutdown || (argc == 2 && strcmp(argv[1], "exit") == 0), "unexpected argument");
    require(atexit(exit_check) == 0, "register atexit");
    if (early_shutdown) {
        native_enable_shutdown_check();
        double limit = monotonic_seconds() + 10;
        while (stabilizer_completed_epochs() < 2) {
            require(monotonic_seconds() < limit, "automatic epoch deadline");
            require(checksum(0) == UINT64_C(1442695040888963407), "automatic epoch checksum");
            struct timespec pause = {0, 100000};
            nanosleep(&pause, NULL);
        }
        require(!stabilizer_retained_exhausted(), "early shutdown needs a live epoch budget");
        completed = 1;
        if (strcmp(argv[1], "early-exit") == 0)
            exit(0);
        return 0;
    }
    pthread_t suspended, busy;
    require(pthread_create(&suspended, NULL, suspended_worker, NULL) == 0, "start suspended worker");
    native_wait_until_blocked();
    uint64_t before = native_blocked_epoch();
    require(before == 1, "manual mode must start at the eager initial epoch");
    require(native_saved_return() != NULL, "native return address missing");
    require(pthread_create(&busy, NULL, checksum_worker, NULL) == 0, "start checksum worker");
    double limit = monotonic_seconds() + 10;
    for (uint64_t epoch = 1; epoch <= 4; ++epoch) {
        while (atomic_load_explicit(&last_checksum_epoch, memory_order_acquire) != epoch)
            require(monotonic_seconds() < limit, "checksum worker epoch handshake");
        if (epoch == 4)
            break;
        require(stabilizer_request_epoch() == 1, "owner rejected manual epoch request");
        while (stabilizer_completed_epochs() != epoch + 1)
            require(monotonic_seconds() < limit, "manual epoch completion deadline");
    }
    while (!stabilizer_retained_exhausted()) {
        require(monotonic_seconds() < limit, "epoch exhaustion deadline");
        struct timespec pause = {0, 100000};
        nanosleep(&pause, NULL);
    }
    uint64_t after = stabilizer_completed_epochs();
    require(after == 4 && after >= before + 2, "epoch budget/progress");
    require(stabilizer_request_epoch() == 0, "exhausted owner accepted another epoch");
    void *latest = stabilizer_code_location((void *)suspended_callback);
    require(latest != NULL && latest != native_saved_body(), "callback body did not change");
    require(atomic_load_explicit(&checks, memory_order_relaxed) > 0, "no concurrent checksum calls");
    while (atomic_load_explicit(&last_checksum_epoch, memory_order_acquire) != after)
        require(monotonic_seconds() < limit, "checksum worker did not reach final epoch");
    require(atomic_load_explicit(&first_checksum_epoch, memory_order_acquire) <= after - 2,
            "checksum worker did not span two epochs");
    /* Exhaustion is the owner's completion acknowledgement, not a guessed sleep. */
    for (int i = 0; i < 10000; ++i)
        require(stabilizer_completed_epochs() == after, "exhausted counter not stable");
    printf("retained-code: epochs=%llu->%llu old=%p new=%p native-return=%p checks=%lu\n",
           (unsigned long long)before, (unsigned long long)after,
           native_saved_body(), latest, native_saved_return(),
           atomic_load_explicit(&checks, memory_order_relaxed));
    native_release();
    require(pthread_join(suspended, NULL) == 0, "join suspended worker");
    atomic_store_explicit(&stop_checks, 1, memory_order_release);
    require(pthread_join(busy, NULL) == 0, "join checksum worker");
    completed = 1;
    if (argc == 2)
        exit(0);
    return 0;
}
