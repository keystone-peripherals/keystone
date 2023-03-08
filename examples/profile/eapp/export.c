#include <stdint.h>
#include <stdio.h>
#include <app/callee.h>
#include "app/syscall.h"

#define NUM_RELOCS  65536

#if NUM_RELOCS
int value = 5;

__attribute__((used))
int *relative[NUM_RELOCS] =
        {[0 ... NUM_RELOCS - 1] = &value};
#endif

int main() {
    int ret;
    uint64_t end, now;
    __asm__ volatile ("rdcycle %0" : "=r"(now));

    // Always self-propagate, would rather have a lot of error codes in data
    // rather than empty measurements
    ret = spawn_callee_handler(NULL, CALL_MAPPED);
    if(ret < 0) {
        return ret;
    }

    // Check timing info
    ret = get_timing_info(NULL, &end);
    if(ret < 0) {
        return -1;
    }

    if((now - end) > (uint64_t) INT32_MAX) {
        return -1;
    }

    return (int) (now - end);
}