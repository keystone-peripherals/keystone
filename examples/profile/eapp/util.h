#ifndef KEYSTONE_EXAMPLES_UTIL_H
#define KEYSTONE_EXAMPLES_UTIL_H

#include <stdint.h>
#include <stdbool.h>

#define USE_EXPORT_FUNC

#ifdef USE_EXPORT_FUNC
#define NUM_MBS                     2
#define SAMPLE_BUFFER_ELEMS         (1024 * 256)
#else
#define NUM_MBS                     3
#define SAMPLE_BUFFER_ELEMS         (1024 * 1024)
#endif

#define SAMPLE_BUFFER_ELEM_SIZE     sizeof(uint32_t)
#define SAMPLE_BUFFER_SIZE          (SAMPLE_BUFFER_ELEMS * SAMPLE_BUFFER_ELEM_SIZE)

#define SHAREDMEM_SIZE  0x2000000

#include "shared/profile_util.h"

_Static_assert((sizeof(struct sharedmem_info) < SHAREDMEM_SIZE),
        "struct sharedmem_info too big");

_Static_assert((SAMPLE_BUFFER_SIZE % (1024 * 1024)) == 0,
        "sample memory should be megabyte-multiple");

void record(struct sharedmem_info *sm, int idx, uint64_t sample);

#endif //KEYSTONE_EXAMPLES_UTIL_H
