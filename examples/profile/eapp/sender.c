
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "util.h"

#include "app/syscall.h"
#include "app/callee.h"
#include "app/export.h"

__attribute__ ((aligned(0x1000)))
uint8_t sharedmem[SHAREDMEM_SIZE] = {0};
struct sharedmem_info *sm = (void *) sharedmem;

#ifdef USE_EXPORT_FUNC
extern struct export_function __start_keystone_exported;
static const struct export_function *func = &__start_keystone_exported;

uint8_t export_func_backup[1024 * 1024 * 4] = {0};

#else
void microbenchmark1() {
    int ret;
    uint64_t start, end, diff;

    ret = call_enclave(0, CALL_RECEIVER, 3, 0xffffffffDEADBEEF, &start, &end);
    if(ret < 0) {
        fprintf(stderr, "Call failed\n");
        return;
    }

    diff = end - start;
    record(sm, 0, diff);
}
#endif

//#define FLUSH_EVERY_SAMPLE
int maybe_flush_sample_buffers(FILE *f) {
    int i;
    size_t written;

#ifdef FLUSH_EVERY_SAMPLE
    if(!sm->first) {
        for(i = 0; i < NUM_MBS; i++) {
            written = fwrite(&sm->samples[i][sm->count - 1 % SAMPLE_BUFFER_ELEMS], sizeof(uint32_t), 1, f);
            if(written != 1) {
                fprintf(stderr, "Only wrote %li elements (errno %s)\n",
                        written, strerror(errno));
                return -1;
            }

            if(sm->count % SAMPLE_BUFFER_ELEMS == 0) {
                memset(sm->samples[i], 0, SAMPLE_BUFFER_SIZE);
            }
        }

        fflush(f);
    } else {
        sm->first = false;
    }
#else
    int j;
    if(sm->count % SAMPLE_BUFFER_ELEMS == 0) {
        if(!sm->first) {
            // Flush sample buffers
            for(i = 0; i < NUM_MBS; i++) {
                // 1 megabyte at a time
                for(j = 0; j < SAMPLE_BUFFER_SIZE / (1024 * 1024); j++) {
                    written = fwrite(&((uint8_t *) &sm->samples[i])[j * 1024 * 1024], 1, 1024 * 1024, f);
                    if(written != 1024 * 1024) {
                        fprintf(stderr, "Only wrote %li elements (errno %s)\n",
                                written, strerror(errno));
                        return -1;
                    }

                    fflush(f);
                }

                memset(sm->samples[i], 0, SAMPLE_BUFFER_SIZE);
            }
        } else {
            sm->first = false;
        }
    }
#endif

    return 0;
}

int main(int argc, char *argv[]) {
    int i;
    uint64_t data;
    size_t written;
    FILE *f;

    f = fopen("/root/data.bin", "wb+");
    if(!f) {
        printf("Failed to open data file\n");
        return -1;
    }

    // Write some metadata
    data = NUM_MBS;
    written = fwrite(&data, sizeof(uint64_t), 1, f);
    assert(written == 1);

#ifdef FLUSH_EVERY_SAMPLE
    data = 1
#else
    data = SAMPLE_BUFFER_ELEMS;
#endif
    written = fwrite(&data, sizeof(uint64_t), 1, f);
    assert(written == 1);

    // Do setup
    int ret = share_region((uintptr_t) sharedmem, sizeof(sharedmem), 0);
    if(ret < 0) {
        printf("Failed to share region\n");
        return ret;
    }

    sm->count = 0;

#ifdef USE_EXPORT_FUNC
    // Save the initial state of the export function
    if(func->size > sizeof(export_func_backup)) {
        fprintf(stderr, "Export func backup not big enough\n");
        return -1;
    }

    memcpy(export_func_backup, (void *) func->addr, func->size);
    ret = share_region(func->addr, func->size, 0);
    if(ret < 0) {
        fprintf(stderr, "Failed to share region\n");
        return ret;
    }

    while(1) {
        maybe_flush_sample_buffers(f);

        // Reset the export function (undo relocations etc)
        memcpy((void *) func->addr, export_func_backup, func->size);

        // Call into the export function
        ret = call_enclave(0, CALL_MAPPED, 4, translate(func->addr), func->size,
                           translate((uintptr_t) sm), SHAREDMEM_SIZE);
        if(ret < 0) {
            printf("Failed to set up export function\n");
            return -1;
        }

        sm->count++;

        // Periodically yield to let the OS kernel do some work
        if(sm->count % 100 == 0) {
            printf("Iteration %li\n\t", sm->count);
            for(i = 0; i < NUM_MBS; i++) {
                printf("%i, ", sm->samples[i][sm->count - 1 % SAMPLE_BUFFER_ELEMS]);
            }
            printf("\n");
            yield_thread();
        }
    }
#else
    ret = call_enclave(0, CALL_RECEIVER, 2, translate((uintptr_t) sharedmem), sizeof(sharedmem));
    if(ret < 0) {
        printf("Failed to set up shared region\n");
        return ret;
    }

    while(1) {
        maybe_flush_sample_buffers(f);

        // This ends up calling the other benchmarks in the receiver
        microbenchmark1();

        if(sm->count % 10000 == 0) {
            printf("Iteration %li\n", sm->count);
        }

        sm->count++;
    }
#endif
}
