#include <errno.h>
#include <string.h>
#include <assert.h>
#include "util.h"

// #ifdef __cplusplus
// extern "C" {
// #endif

phase_t connection_tries_phase = -1;

FILE *init_data_file() {
    uint64_t data;
    size_t written;

    FILE *f = fopen("/root/data.bin", "wb+");
    if(!f) {
        printf("Failed to open data file\n");
        return NULL;
    }

    // Write some metadata
    data = NUM_MBS;
    written = fwrite(&data, sizeof(uint64_t), 1, f);
    assert(written == 1);

    data = 1;
    written = fwrite(&data, sizeof(uint64_t), 1, f);
    assert(written == 1);

    return f;
}

void record(phase_t idx, uint64_t sample) {
    if(idx >= NUM_MBS) {
        fprintf(stderr, "Invalid microbenchmark index %i\n", idx);
        return;
    }

    struct sharedmem_info *sm = get_sm();

    if(sample < SAMPLE_BUFFER_ELEM_MAX) {
        sm->samples[idx][sm->count % SAMPLE_BUFFER_ELEMS] = (SAMPLE_BUFFER_ELEM_TYPE) sample;
    } else {
        fprintf(stderr, "Sample %li bigger than SAMPLE_BUFFER_ELEM_SIZE, discarding\n", sample);
        return;
    }
}

int flush_sample_buffers(FILE *f) {
    int i;
    size_t written;

    struct sharedmem_info *sm = get_sm();
    if(!sm) {
        return -1;
    }

    for(i = 0; i < NUM_MBS; i++) {
        written = fwrite(&sm->samples[i][sm->count % SAMPLE_BUFFER_ELEMS], SAMPLE_BUFFER_ELEM_SIZE, 1, f);
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
    return 0;
}

// #ifdef __cplusplus
// }
// #endif
