

#include <stdio.h>
#include <string.h>

#include "images.h"

#include "app/callee.h"
#include "app/export.h"
#include "app/syscall.h"

#include "vta_export.h"

#include "util.h"

#include "../../util/util.h"

extern struct export_function __start_keystone_exported;
extern struct export_function __stop_keystone_exported;

static float curr_image[3 * 224 * 224];

extern unsigned char params_bin_pad[];
extern unsigned int params_bin_pad_len;

extern unsigned char graph_json_pad[];
extern unsigned int graph_json_pad_len;

struct sharedmem_info *get_sm() {
    struct export_function *func = &__start_keystone_exported;
    void *scratch = (void *) (func->addr + func->size - func->scratch_size);
    return (struct sharedmem_info *) ((uint8_t *) scratch + 0x93000);
}

void *export_func_backup = nullptr;

int setup() {
    int ret;
    struct export_function *func = &__start_keystone_exported;
    if(!export_func_backup) {
        export_func_backup = malloc(func->size);
        memcpy(export_func_backup, (void *) func->addr, func->size - func->scratch_size);
    }

    void *scratch = (void *) (func->addr + func->size - func->scratch_size);

    ret = share_region(func->addr, func->size, 0);
    if(ret < 0) {
        printf(ERROR_PREFIX "Failed to share region\n");
        return ret;
    }

    ret = share_region((uintptr_t)params_bin_pad, params_bin_pad_len, 0);
    if (ret < 0) {
        printf(ERROR_PREFIX "!!! Failed to share params_bin\n");
        return ret;
    }

    ret = share_region((uintptr_t)graph_json_pad, graph_json_pad_len, 0);
    if (ret < 0) {
        printf(ERROR_PREFIX "!!! Failed to share graph_json\n");
        return ret;
    }

    ret = call_enclave(0, CALL_MAPPED, 2,
                       translate(func->addr), func->size);
    if (ret < 0) {
        printf(ERROR_PREFIX "!!! call_enclave 1 failed\n");
        return ret;
    }

    ((uint64_t *) scratch)[0] = params_bin_pad_len;
    ((uint64_t *) scratch)[1] = graph_json_pad_len;

    ret = call_enclave(0, CALL_RECEIVER, 4,
                       EXPORT_SETUP,
                       translate((uintptr_t)graph_json_pad),
                       translate((uintptr_t)params_bin_pad),
                       func->size);
    if (ret < 0) {
        printf(ERROR_PREFIX "!!! call_enclave 2 failed\n");
        return ret;
    }

    return 0;
}

#define OUTPUT_LEN 1000
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

int inference() {
    struct export_function *func = &__start_keystone_exported;
    void *scratch = (void *) (func->addr + func->size - func->scratch_size);
    float *output = (float *) scratch;

    int ret, i, img = get_next_image(curr_image, sizeof(curr_image));
    if (img < 0) {
        printf(ERROR_PREFIX "Couldn't get next image\n");
        return -1;
    }

    memcpy(scratch, curr_image, sizeof(curr_image));
    ret = call_enclave(0, CALL_RECEIVER, 3,
                       EXPORT_INFER,
                       func->size, func->scratch_size);

    if (ret < 0) {
        printf(ERROR_PREFIX "Failed to call enclave\n");
        return ret;
    }

    printf("\nImage %i\n", img);
    for (i = 0; i < OUTPUT_LEN; i++) {
        output_argmax[i].i = i;
        output_argmax[i].val = output[i];
    }

    qsort(output_argmax, OUTPUT_LEN, sizeof(struct argmax), argmax_compare);

    for (i = 0; i < 5; i++) {
        printf("%i\t", output_argmax[i].i);
    }
    printf("\n");
    return 0;
}

int teardown() {
    int ret;
    call_enclave(0, CALL_RECEIVER, 1, EXPORT_DIE);
    struct export_function *func = &__start_keystone_exported;

    ret = unshare_region(func->addr, 0);
    if(ret < 0) {
        printf(ERROR_PREFIX "Failed to unshare func region\n");
        return ret;
    }

    ret = unshare_region((uintptr_t) params_bin_pad, 0);
    if(ret < 0) {
        printf(ERROR_PREFIX "Failed to unshare params region\n");
        return ret;
    }

    ret = unshare_region((uintptr_t) graph_json_pad, 0);
    if(ret < 0) {
        printf(ERROR_PREFIX "Failed to unshare graph region\n");
        return ret;
    }

    memcpy((void *) func->addr, export_func_backup, func->size - func->scratch_size);
    return 0;
}


int main() {
    printf("Starting\n");

    struct sharedmem_info *sm = get_sm();
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
