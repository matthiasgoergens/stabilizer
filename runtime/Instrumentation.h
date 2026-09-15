#ifndef STABILIZER_INSTRUMENTATION_H
#define STABILIZER_INSTRUMENTATION_H

#include <stdint.h>

enum {
    STABILIZER_MODULE_ABI = 1,
    STABILIZER_CODE = 1,
    STABILIZER_HEAP = 2,
    STABILIZER_STACK = 4
};

#ifdef __cplusplus
extern "C" {
#endif
void stabilizer_register_module(uint32_t abi, uint32_t flags);
/* Experimental retained-code observability/control. Outside retained mode
   these return zero/null. An epoch counts a completed whole publication pass,
   not an atomic whole-program transition. The initial eager layout is epoch 1. */
uint64_t stabilizer_completed_epochs(void);
int stabilizer_retained_exhausted(void);
/* Requests coalesce. Success means a request was accepted, not a promise of
   one distinct epoch for each caller. Shutdown cancels pending requests;
   waits on completed_epochs must have a deadline. */
int stabilizer_request_epoch(void);
void* stabilizer_code_location(void* original_entry);
#ifdef __cplusplus
}
#endif

#endif
