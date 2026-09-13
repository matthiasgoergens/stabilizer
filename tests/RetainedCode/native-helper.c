#define _POSIX_C_SOURCE 200809L
#include "native-helper.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

extern uint64_t stabilizer_completed_epochs(void);
extern int stabilizer_retained_exhausted(void);
extern int stabilizer_request_epoch(void);

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static int blocked, released;
static void *saved_return, *saved_body;
static uint64_t blocked_epoch;
static int shutdown_check_enabled;

static void require(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "native shutdown check: %s\n", message);
        _Exit(1);
    }
}

static void shutdown_check(void) {
    if (!shutdown_check_enabled)
        return;
    uint64_t epochs = stabilizer_completed_epochs();
    require(epochs >= 2, "automatic owner did not publish a second epoch");
    require(!stabilizer_retained_exhausted(), "budget exhausted before shutdown");
    require(stabilizer_request_epoch() == 0, "stopped owner accepted an epoch request");
    /* Supplementary observation only; rejection above is the shutdown contract. */
    struct timespec remaining = {0, 60000000};
    while (nanosleep(&remaining, &remaining) != 0)
        require(errno == EINTR, "stability observation nanosleep failed");
    require(stabilizer_completed_epochs() == epochs, "stopped epoch count changed");
    puts("retained-code: native post-cleanup shutdown check passed");
}

/* Register before runtime main registers owner cleanup, so this runs after it. */
__attribute__((constructor)) static void register_shutdown_check(void) {
    require(atexit(shutdown_check) == 0, "register late shutdown checker");
}

void native_enable_shutdown_check(void) { shutdown_check_enabled = 1; }

static void check(int error) {
    if (error) {
        fprintf(stderr, "native helper pthread/clock error: %d\n", error);
        _Exit(1);
    }
}

static void initialise(void) {
    pthread_condattr_t attr;
    check(pthread_condattr_init(&attr));
    check(pthread_condattr_setclock(&attr, CLOCK_MONOTONIC));
    check(pthread_cond_init(&condition, &attr));
    check(pthread_condattr_destroy(&attr));
}

static struct timespec deadline(void) {
    struct timespec result;
    check(clock_gettime(CLOCK_MONOTONIC, &result));
    result.tv_sec += 10;
    return result;
}

__attribute__((noinline)) void native_wait_with_return(void *body) {
    uint64_t epoch = stabilizer_completed_epochs();
    check(pthread_once(&once, initialise));
    check(pthread_mutex_lock(&lock));
    saved_return = __builtin_return_address(0);
    saved_body = body;
    blocked_epoch = epoch;
    blocked = 1;
    check(pthread_cond_broadcast(&condition));
    struct timespec limit = deadline();
    while (!released)
        check(pthread_cond_timedwait(&condition, &lock, &limit));
    check(pthread_mutex_unlock(&lock));
}

void native_wait_until_blocked(void) {
    check(pthread_once(&once, initialise));
    check(pthread_mutex_lock(&lock));
    struct timespec limit = deadline();
    while (!blocked)
        check(pthread_cond_timedwait(&condition, &lock, &limit));
    check(pthread_mutex_unlock(&lock));
}

void native_release(void) {
    check(pthread_mutex_lock(&lock));
    released = 1;
    check(pthread_cond_broadcast(&condition));
    check(pthread_mutex_unlock(&lock));
}

/* The caller first waits for 'blocked'; these fields are immutable thereafter. */
void *native_saved_return(void) { return saved_return; }
void *native_saved_body(void) { return saved_body; }
uint64_t native_blocked_epoch(void) { return blocked_epoch; }
