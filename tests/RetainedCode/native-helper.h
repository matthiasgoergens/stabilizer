#ifndef RETAINED_NATIVE_HELPER_H
#define RETAINED_NATIVE_HELPER_H

#include <stdint.h>

void native_wait_with_return(void *body);
void native_wait_until_blocked(void);
void native_release(void);
void *native_saved_return(void);
void *native_saved_body(void);
uint64_t native_blocked_epoch(void);
void native_enable_shutdown_check(void);

#endif
