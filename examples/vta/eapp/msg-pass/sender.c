
// Standard includes
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// Keystone includes
#include "app/callee.h"
#include "app/syscall.h"

// Internal includes
#include "interface.h"
#include "images.h"
#include "util.h"

extern unsigned char graph_json_pad[];
extern unsigned int graph_json_pad_len;

extern unsigned char params_bin_pad[];
extern unsigned int params_bin_pad_len;

__attribute__((aligned(0x1000)))
extern unsigned char operator_lib[];
extern unsigned int operator_lib_len;

__attribute__((aligned(0x1000)))
static float curr_image[3 * 224 * 224];

#define OUTPUT_LEN 1000

__attribute__((aligned(0x1000)))
static float output[OUTPUT_LEN + 24];
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

#if USE_MAPPED

static void send_data(unsigned char *buf, unsigned int size) {
    int r;
    r = share_region((uintptr_t) buf, size, EID_DRIVER_ENCLAVE);
    if(r < 0) {
        printf(ERROR_PREFIX "Failed to share region\n");
        exit(-1);
    }

    call_enclave(EID_DRIVER_ENCLAVE, CALL_RECEIVER, 2, translate((uintptr_t) buf), size);

    r = unshare_region((uintptr_t) buf, EID_DRIVER_ENCLAVE);
    if(r < 0) {
        printf(ERROR_PREFIX "Failed to unshare region\n");
        exit(-1);
    }
}

static void recv_data(unsigned char *buf, unsigned int size) {
    // Looks dumb, but actually this is the exact same flow as sending. The key difference
    // is the different context implied by CMD_RESULT earlier.
    return send_data(buf, size);
}

#else

static inline unsigned int min(unsigned int a, unsigned int b) {
    if(b < a)
        return b;
    else
        return a;
}

static void send_data(unsigned char *buf, unsigned int size) {
    unsigned int copied = 0, copying;
    uint64_t args[4] = {0};

    while(copied < size) {
        copying = min(sizeof(args), size - copied);
        memset(args, 0, sizeof(args));
        memcpy(args, &buf[copied], copying);

        call_enclave(EID_DRIVER_ENCLAVE, CALL_RECEIVER, 4, args[0], args[1], args[2], args[3]);
        copied += copying;
    }
}

static volatile bool receiving = false;
static size_t bytes_awaiting, bytes_processed = 0;
static unsigned char *recv_buf;

static void recv_data(unsigned char *buf, unsigned int size) {
    // We need to just dummy call once in order to land in the STATE_SEND_RESULT
    // state in the receiver's receive_handler.
    receiving = true;
    bytes_awaiting = size;
    bytes_processed = 0;
    recv_buf = buf;

    // By the end of this call, we should have everything
    call_enclave(EID_DRIVER_ENCLAVE, CALL_RECEIVER, 0);
    receiving = false;
}

int receive_handler(void *arg) {
    size_t copying;
    uint64_t *args = (uint64_t *) arg;

    if(receiving) {
        copying = min(32, bytes_awaiting - bytes_processed);
        memcpy(&recv_buf[bytes_processed], args, copying);
        bytes_processed += copying;
        return 0;
    } else return -1;
}

#endif

__attribute__ ((aligned(0x1000)))
uint8_t sharedmem[SHAREDMEM_SIZE] = {0};
struct sharedmem_info *sm = (void *) sharedmem;

struct sharedmem_info *get_sm() {
    return sm;
}

int setup() {
    call_enclave(EID_DRIVER_ENCLAVE, CALL_RECEIVER, 2, CMD_GRAPH, graph_json_pad_len);
    INSTRUMENT(PHASE_XFER_GRAPH, {
        send_data(graph_json_pad, graph_json_pad_len);
    })

    call_enclave(EID_DRIVER_ENCLAVE, CALL_RECEIVER, 2, CMD_OPERATOR, operator_lib_len);
    INSTRUMENT(PHASE_XFER_OPS, {
        send_data(operator_lib, operator_lib_len);
    })

    call_enclave(EID_DRIVER_ENCLAVE, CALL_RECEIVER, 2, CMD_PARAMS, params_bin_pad_len);
    INSTRUMENT(PHASE_XFER_PARAMS, {
        send_data(params_bin_pad, params_bin_pad_len);
    })

    return 0;
}

int inference() {
    int i, img = get_next_image(curr_image, sizeof(curr_image));
    if(img < 0) {
        printf(ERROR_PREFIX "Couldn't get next image\n");
        return -1;
    }

    // Send decompressed image over. Evaluation automatically begins (in the top half)
    // once image is fully received.
    call_enclave(EID_DRIVER_ENCLAVE, CALL_RECEIVER, 2, CMD_IMAGE, sizeof(curr_image));
    INSTRUMENT(PHASE_XFER_IMAGE, {
        send_data((unsigned char *) curr_image, sizeof(curr_image));
    })

    // Get output. This is synchronous, so stalls until eval done
    call_enclave(EID_DRIVER_ENCLAVE, CALL_RECEIVER, 2, CMD_RESULT, sizeof(output));
    INSTRUMENT(PHASE_XFER_OUTPUT, {
        recv_data((unsigned char *) output, sizeof(output));
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
    call_enclave(EID_DRIVER_ENCLAVE, CALL_RECEIVER, 1, CMD_RESET);
    return 0;
}

int main() {
    printf("Starting\n");

    // Do setup
    FILE *f;
    int ret = share_region((uintptr_t) sharedmem, sizeof(sharedmem), 0);
    if(ret < 0) {
        printf(ERROR_PREFIX "failed to share region\n");
        return ret;
    }

    call_enclave(EID_DRIVER_ENCLAVE, CALL_RECEIVER, 3,
                 CMD_SETUP, translate((uintptr_t) sm), SHAREDMEM_SIZE);

    f = init_data_file();

#if !USE_MAPPED
    int r;

    r = spawn_callee_handler(receive_handler, CALL_RECEIVER);
    if(r < 0) {
        printf(ERROR_PREFIX "Failed to start callee handler\n");
        return -1;
    }
#endif

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
