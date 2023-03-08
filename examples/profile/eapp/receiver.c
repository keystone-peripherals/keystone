
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "app/callee.h"
#include "app/syscall.h"

#include "util.h"

static bool setup;
static struct sharedmem_info *sm = NULL;

void microbenchmark2() {
    uint64_t diff = pthread_create_end - pthread_create_start;
    record(sm, 1, diff);
}

void microbenchmark3(uint64_t now, uint64_t enter) {
    uint64_t diff = now - enter;
    record(sm, 2, diff);
}

int handler(void *arg) {
    uint64_t now, *args = arg;
    __asm__ volatile ("rdcycle %0" : "=r"(now));
    uint64_t enter;

    // Map shared region
    if(!setup) {
        assert(map(args[0], args[1], args[0]) == args[0]);
        sm = (void *) args[0];
        setup = true;
        return 0;
    }

    enter = args[3];
    microbenchmark2();
    microbenchmark3(now, enter);
    return 0;
}

int main() {
    int ret;
#ifdef USE_EXPORT_FUNC
    ret = spawn_callee_handler(NULL, CALL_MAPPED);
#else
    ret = spawn_callee_handler(handler, CALL_RECEIVER);
#endif
    if(ret < 0) {
        printf("Failed to start callee handler\n");
        return -1;
    }

    setup = false;
    while(1) {
        yield_thread();
    }
}