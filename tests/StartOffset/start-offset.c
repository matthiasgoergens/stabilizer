/* Each code copy starts at a random multiple of 16 bytes, so the alignment of
   a function modulo 64 is sampled across generations. STABILIZER_CODE_OFFSET=0
   keeps every copy at the allocator's fixed offset (16 mod 32). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern uint64_t stabilizer_completed_epochs(void);
extern int stabilizer_request_epoch(void);
extern void *stabilizer_code_location(void *original_entry);

__attribute__((noinline)) int work(int x) { return x * 3 + 1; }

int main(int argc, char **argv) {
    int expect_fixed = argc > 1 && strcmp(argv[1], "fixed") == 0;
    int seen[4] = {0};
    int generations = 0;
    for (int i = 0; i < 48; i++) {
        uintptr_t where = (uintptr_t)stabilizer_code_location((void *)&work);
        if (where % 16 != 0 || work(i) != i * 3 + 1) {
            printf("FAIL: copy at %p\n", (void *)where);
            return 1;
        }
        seen[(where % 64) / 16] = 1;
        generations++;
        uint64_t before = stabilizer_completed_epochs();
        if (!stabilizer_request_epoch()) break;
        for (int spin = 0; spin < 2000 && stabilizer_completed_epochs() == before; spin++) {
            struct timespec ts = {0, 1000000};
            nanosleep(&ts, NULL);
        }
    }
    int distinct = seen[0] + seen[1] + seen[2] + seen[3];
    if (expect_fixed ? distinct > 2 : distinct < 3) {
        printf("FAIL: %d distinct start offsets mod 64 over %d generations\n", distinct, generations);
        return 1;
    }
    printf("PASS: %d distinct start offsets mod 64 over %d generations (%s)\n", distinct, generations,
           expect_fixed ? "offset disabled" : "offset enabled");
    return 0;
}
