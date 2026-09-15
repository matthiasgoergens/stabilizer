#include <stdio.h>

extern int fixture_marker(void);

int main(void) {
    if(fixture_marker() != 42) return 1;
    puts("retained admission control passed");
    return 0;
}
