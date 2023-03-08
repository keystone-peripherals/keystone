#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>

#include <tvm/runtime/crt/crt.h>
#include <tvm/runtime/crt/graph_executor.h>
#include <tvm/runtime/crt/packed_func.h>
#include <tvm/runtime/crt/page_allocator.h>

#include "platform.h"
#include "images.h"

#include "../util/util.h"

//#define VERBOSE

extern unsigned char graph_json[];
extern unsigned int graph_json_len;

extern unsigned char params_bin[];
extern unsigned int params_bin_len;

extern const char imgs_xz[];
extern unsigned int imgs_xz_len;

#define OUTPUT_LEN 1000

float output_storage[OUTPUT_LEN];
struct argmax {
    int i;
    float val;
};
struct argmax output_argmax[OUTPUT_LEN];
float input_storage[3 * 224 * 224];

// __attribute__ ((aligned(0x1000)))
// uint8_t sharedmem[SHAREDMEM_SIZE] = {0};
// extern struct sharedmem_info* sm;

int argmax_compare(const void *a, const void *b) {
    struct argmax *arg_a = (struct argmax *) a,
            *arg_b = (struct argmax *) b;

    if(arg_a->val < arg_b->val) {
        return 1;
    } else {
        return -1;
    }
}

#define read_csr(reg) __extension__({ unsigned long __tmp; \
  asm volatile ("csrr %0, " #reg : "=r"(__tmp)); \
  __tmp; })

TVMGraphExecutor* graph_executor = NULL;

DLDevice dev = {
        .device_type = kDLExtDev,
        .device_id = 0
};

int setup() {
    vta_examples_init_runtime();
    vta_set_debug(0);
    vta_examples_init_executor(&graph_executor, &dev, (char *) graph_json);
    vta_examples_load_params(graph_executor, (char*)params_bin, params_bin_len);

    return 0;
}

int inference() {
    int img, i;

    img = get_next_image(input_storage, sizeof(input_storage));
    if (img < 0) {
        printf(ERROR_PREFIX "Couldn't get next image\n");
        return 1;
    }

    vta_examples_run_inference(graph_executor, &dev, input_storage, output_storage);

    // Output data is now in output_storage
    printf("\nImage %i\n", img);
    for (i = 0; i < OUTPUT_LEN; i++) {
        output_argmax[i].i = i;
        output_argmax[i].val = output_storage[i];
    }

    qsort(output_argmax, OUTPUT_LEN, sizeof(struct argmax), argmax_compare);

    for (i = 0; i < 5; i++) {
        printf("%i\t", output_argmax[i].i);
    }
    printf("\n");
    return 0;
}

int teardown() {
    TVMGraphExecutor_Release(&graph_executor);
    graph_executor = nullptr;
    VTAMemReset();
    return 0;
}

uint8_t sharedmem[SHAREDMEM_SIZE] = {0};
struct sharedmem_info* sm = (struct sharedmem_info*)sharedmem;

struct sharedmem_info *get_sm() {
    return sm;
}

int main() {
    printf("Starting\n");
    
    FILE *f = init_data_file();
    sm->count = 0;

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
