#include <stdio.h>
#include <stdlib.h>
#include "util.h"

void record(struct sharedmem_info *sm, int idx, uint64_t sample) {
    if(idx >= NUM_MBS) {
        fprintf(stderr, "Invalid microbenchmark index %i\n", idx);
        return;
    }

    if(sample < UINT32_MAX) {
        sm->samples[idx][sm->count % SAMPLE_BUFFER_ELEMS] = (uint32_t) sample;
    } else {
        fprintf(stderr, "Sample %li bigger than uint32_t, discarding\n", sample);
        return;
    }
}