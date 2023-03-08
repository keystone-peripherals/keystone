#ifndef KEYSTONE_EXAMPLES_UTIL_H
#define KEYSTONE_EXAMPLES_UTIL_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define ERROR_PREFIX "¯\\_(ツ)_/¯ "

/* Different phases we want to measure for macrobenchmarks */

typedef enum {
    // Execution phases
    PHASE_DOWNLOAD_GRAPH_JSON = 0,
    PHASE_DOWNLOAD_PARAMS_BIN,
    PHASE_CONNECT_TRIES_GRAPH_JSON,
    PHASE_CONNECT_TRIES_PARAMS_BIN,
    PHASE_INIT_EXECUTOR, //
    PHASE_LOAD_PARAMS, //
    PHASE_INFERENCE, //

    // Transfer phases
    PHASE_XFER_GRAPH,
    PHASE_XFER_OPS,
    PHASE_XFER_PARAMS,
    PHASE_XFER_IMAGE,
    PHASE_XFER_OUTPUT,

    PHASE_LAST
} phase_t;

extern phase_t connection_tries_phase;

/* Storage data structure */

#define NUM_MBS                     (PHASE_LAST)
#define SAMPLE_BUFFER_ELEMS         (1024)

#define SAMPLE_BUFFER_ELEM_TYPE     uint64_t
#define SAMPLE_BUFFER_ELEM_SIZE     sizeof(SAMPLE_BUFFER_ELEM_TYPE)
#define SAMPLE_BUFFER_ELEM_MAX      UINT64_MAX
#define SAMPLE_BUFFER_SIZE          (SAMPLE_BUFFER_ELEMS * SAMPLE_BUFFER_ELEM_SIZE)

#define SHAREDMEM_SIZE  0x100000
#define LINUX_OWNED_UNTRUSTED_SIZE  (64 * 1024 * 1024 - SHAREDMEM_SIZE)

struct sharedmem_info {
    uint64_t count;
    SAMPLE_BUFFER_ELEM_TYPE samples[NUM_MBS][SAMPLE_BUFFER_ELEMS];
};

#ifdef __cplusplus
static_assert((sizeof(struct sharedmem_info) < SHAREDMEM_SIZE),
        "struct sharedmem_info too big");
#else
_Static_assert((sizeof(struct sharedmem_info) < SHAREDMEM_SIZE),
        "struct sharedmem_info too big");
#endif

/* Data management functions */

#ifdef __cplusplus
extern "C" {
#endif

struct sharedmem_info *get_sm();

FILE *init_data_file();

void record(phase_t idx, uint64_t sample);

int flush_sample_buffers(FILE *f);

#ifdef __cplusplus
}
#endif

/* Useful macros */

#define INSTRUMENT(phase, code) \
    { uint64_t start, end; \
    __asm__ volatile ("rdcycle %0" : "=r"(start)); \
    code \
    __asm__ volatile ("rdcycle %0" : "=r"(end));   \
    record(phase, end - start); }

#endif //KEYSTONE_EXAMPLES_UTIL_H
