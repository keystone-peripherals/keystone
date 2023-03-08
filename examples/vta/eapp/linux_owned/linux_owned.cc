#include <stdio.h>

// Keystone includes
#include <app/syscall.h>
#include <edge/edge_common.h>

// Internal includes
#include "interface.h"
#include "images.h"
#include "util.h"

#include "../../util/util.h"

extern unsigned char graph_json[];
extern unsigned int graph_json_len;

extern unsigned char params_bin[];
extern unsigned int params_bin_len;

extern unsigned char operator_lib[];
extern unsigned int operator_lib_len;

static float curr_image[3 * 224 * 224];

#define OUTPUT_LEN  1000
static float output[OUTPUT_LEN];

struct argmax {
    int i;
    float val;
};
struct argmax output_argmax[OUTPUT_LEN];

int argmax_compare(const void *a, const void *b) {
    struct argmax *arg_a = (struct argmax *) a,
            *arg_b = (struct argmax *) b;

    if(arg_a->val < arg_b->val) {
        return 1;
    } else {
        return -1;
    }
}

struct sharedmem_info *sm = nullptr;
struct sharedmem_info *get_sm() {
    return sm;
}

int setup() {
    size_t sizes[] = {graph_json_len, operator_lib_len, params_bin_len,
                      sizeof(curr_image), sizeof(output)};

    // Transfer data to the host
    ocall(CMD_METADATA, sizes, sizeof(sizes), nullptr, 0);

    INSTRUMENT(PHASE_XFER_GRAPH, {
        ocall(CMD_GRAPH, graph_json, graph_json_len, nullptr, 0);
    })

    INSTRUMENT(PHASE_XFER_OPS, {
        ocall(CMD_OPERATOR, operator_lib, operator_lib_len, nullptr, 0);
    })

    INSTRUMENT(PHASE_XFER_PARAMS, {
        ocall(CMD_PARAMS, params_bin, params_bin_len, nullptr, 0);
    })

    return 0;
}

int inference() {
    int i, img = get_next_image(curr_image, sizeof(curr_image));
    if(img < 0) {
        printf(ERROR_PREFIX "Couldn't get next image\n");
        return -1;
    }

    // Send decompressed image over
    INSTRUMENT(PHASE_XFER_IMAGE, {
        ocall(CMD_IMAGE, curr_image, sizeof(curr_image), nullptr, 0);
    })

    // Get output. This is synchronous, so stalls until eval done
    INSTRUMENT(PHASE_XFER_OUTPUT, {
        ocall(CMD_RESULT, nullptr, 0, output, sizeof(output));
    })

    for(i = 0; i < OUTPUT_LEN; i++) {
        output_argmax[i].i = i;
        output_argmax[i].val = output[i];
    }

    printf("\nImage %i\n", img);
    qsort(output_argmax, OUTPUT_LEN, sizeof(struct argmax), argmax_compare);
    for(i = 0; i < 5; i++) {
        printf("%i\t" , output_argmax[i].i);
    }
    printf("\n");
    return 0;
}

int teardown() {
    ocall(CMD_RESET, nullptr, 0, nullptr, 0);
    return 0;
}

int main() {
    printf("Starting\n");

    // Initialize shared memory
    sm = (struct sharedmem_info *) ((uint8_t *) map_shared_buf() + LINUX_OWNED_UNTRUSTED_SIZE);
    sm->count = 0;

    FILE *f = init_data_file();

#ifndef SETUP_TEARDOWN_ONLY
    if(setup() < 0) {
        printf(ERROR_PREFIX "setup failed\n");
        return -1;
    }
#endif

    while(true) {
#ifdef SETUP_TEARDOWN_ONLY
        if(setup() < 0) {
            printf(ERROR_PREFIX "setup failed\n");
            return -1;
        }

        if(teardown() < 0) {
            printf(ERROR_PREFIX "teardown failed\n");
            return -1;
        }
#else
        if(inference() < 0) {
            printf(ERROR_PREFIX "inference failed\n");
            return -1;
        }
#endif
        flush_sample_buffers(f);
        printf("Iteration %li\n", sm->count);
        sm->count++;
    }

    return 0;
}
