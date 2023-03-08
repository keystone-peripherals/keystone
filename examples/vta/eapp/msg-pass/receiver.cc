
// Standard includes
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// TVM includes
#include <tvm/runtime/crt/module.h>
#include <tvm/runtime/crt/graph_executor.h>
#include <tvm/runtime/crt/crt.h>

// Keystone includes
#include "app/callee.h"
#include "app/syscall.h"

// Internal includes
#include "platform.h"
#include "interface.h"
#include "util.h"


// We need these externs to create the dispatch_table for the receiver operator library

extern "C" {

extern void *VTABufferCPUPtr(VTACommandHandle, void *);
extern int VTADepPop(VTACommandHandle, int, int);
extern int VTADepPush(VTACommandHandle, int, int);
extern void VTALoadBuffer2D(VTACommandHandle, void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                            uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int VTAPushALUOp(void **, int (*)(void *), void *, int);
extern int VTAPushGEMMOp(void **, int (*)(void *), void *, int);
extern void VTAStoreBuffer2D(VTACommandHandle, uint32_t, uint32_t, void *, uint32_t, uint32_t, uint32_t, uint32_t);
extern void VTASynchronize(VTACommandHandle, uint32_t);
extern VTACommandHandle VTATLSCommandHandle(void);
extern void VTAUopLoopBegin(uint32_t, uint32_t, uint32_t, uint32_t);
extern void VTAUopLoopEnd(void);
extern void VTAUopPush(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

static const struct cmd_table dispatch_table{
        .TVMAPISetLastError = TVMAPISetLastError,
        .TVMBackendAllocWorkspace = TVMBackendAllocWorkspace,
        .TVMBackendFreeWorkspace = TVMBackendFreeWorkspace,
        .TVMBackendGetFuncFromEnv = TVMBackendGetFuncFromEnv,
        .TVMBackendParallelLaunch = TVMBackendParallelLaunch,
        .TVMFuncCall = TVMFuncCall,

        .VTABufferCPUPtr = VTABufferCPUPtr,
        .VTADepPop = VTADepPop,
        .VTADepPush = VTADepPush,
        .VTALoadBuffer2D = VTALoadBuffer2D,
        .VTAPushALUOp = VTAPushALUOp,
        .VTAPushGEMMOp = VTAPushGEMMOp,
        .VTAStoreBuffer2D = VTAStoreBuffer2D,
        .VTASynchronize = VTASynchronize,
        .VTATLSCommandHandle = VTATLSCommandHandle,
        .VTAUopLoopBegin = VTAUopLoopBegin,
        .VTAUopLoopEnd = VTAUopLoopEnd,
        .VTAUopPush = VTAUopPush,
};

};

/* Machine learning model components (to be received) */

// operators, graph, params, and image are allocated in the receive handler
// result is allocated in the main thread
#define OPERATORS_SZ 0x80000
__attribute__((aligned(0x1000)))
static uint8_t operators[OPERATORS_SZ];
static uint8_t *graph = nullptr, *params = nullptr, *image = nullptr, *result = nullptr;
static size_t operators_sz, graph_sz, params_sz, image_sz, result_sz;

static TVMGraphExecutor *executor = nullptr;

const TVMModule* TVMSystemLibEntryPoint(void) {
    initialize_comms_fn initialize_comms = (initialize_comms_fn) map(translate((uintptr_t) &operators[0]),
                                                                     ((operators_sz + 0x1000 - 1) / 0x1000) * 0x1000,
                                                                     OPERATOR_LIB_ADDR);
    syslib_entry_fn real_entry_point = initialize_comms(&dispatch_table);
    return real_entry_point();
}

/* Receiver functionality */
static vta_state_t state;
static volatile bool received_graph, received_ops, received_params, received_image;
static size_t bytes_awaiting, bytes_processed;

static inline void alloc_recv_buffer(uint8_t **buf, size_t size, size_t alignment) {
    assert(!*buf);
    if(alignment) {
        *buf = (uint8_t *) std::aligned_alloc(alignment, size);
    } else {
        *buf = (uint8_t *) malloc(size);
    }
    assert(buf);
}

#if USE_MAPPED
static void recv_data(uint8_t *buf, uint64_t *args, volatile bool *done) {
    // Map physical -> virtual directly
    uintptr_t mapped_buffer = map(args[0], args[1], args[0]);
    assert(mapped_buffer == args[0]);

    memcpy(buf, (const void *) mapped_buffer, bytes_awaiting);
    unmap(args[0], args[1]);
    bytes_processed = bytes_awaiting;
    *done = true;
    state = STATE_READY;
}

static void send_data(uint64_t *args, uint8_t *buf, size_t size) {
    uintptr_t mapped_buffer = map(args[0], args[1], args[0]);
    assert(mapped_buffer == args[0]);

    memcpy((void *) mapped_buffer, buf, size);
    unmap(args[0], args[1]);
    state = STATE_READY;
}

#else
static void recv_data(uint8_t *buf, uint64_t *args, volatile bool *done) {
    size_t copying = std::min((size_t) 32, bytes_awaiting - bytes_processed);
    memcpy(&buf[bytes_processed], args, copying);

    bytes_processed += copying;
    if(bytes_processed == bytes_awaiting) {
        *done = true;
        state = STATE_READY;
    }
}

static void send_data(uint64_t *args_incoming, uint8_t *buf, size_t size) {
    unsigned int copied = 0, copying;
    uint64_t args[4] = {0};

    while(copied < size) {
        copying = std::min(sizeof(args), size - copied);
        memset(args, 0, sizeof(args));
        memcpy(args, &buf[copied], copying);

        call_enclave(EID_CONSUMER_ENCLAVE, CALL_RECEIVER, 4, args[0], args[1], args[2], args[3]);
        copied += copying;
    }
}

#endif

extern "C" {
struct sharedmem_info *sm = nullptr;
struct sharedmem_info *get_sm() {
    return sm;
}
}

DLDevice dev = {
        .device_type = kDLExtDev,
        .device_id = 0
};

int receive_handler(void *arg) {
    uint64_t *args = (uint64_t *) arg;
    vta_cmd_t cmd;
    size_t sz;

    switch (state) {
        case STATE_READY:
            cmd = (vta_cmd_t) args[0];
            sz = args[1];

            bytes_awaiting = sz, bytes_processed = 0;
            switch (cmd) {
                // These do not cause a state transition
                case CMD_SETUP:
                    assert(map(args[1], args[2], args[1]) == args[1]);
                    sm = (struct sharedmem_info *) args[1];
                    bytes_awaiting = 0;
                    break;

                case CMD_RESET:
                    received_graph = false, received_ops = false, received_params = false, received_image = false;

                    TVMGraphExecutor_Release(&executor);
                    executor = nullptr;
                    VTAMemReset();

                    vta_examples_init_runtime();
                    vta_set_debug(0);

                    if(graph) {
                        free(graph); graph = nullptr;
                        graph_sz = 0;
                    }

                    unmap(OPERATOR_LIB_ADDR, operators_sz);
                    operators_sz = 0;

                    if(params) {
                        free(params); params = nullptr;
                        params_sz = 0;
                    }

                    if(image) {
                        free(image); image = nullptr;
                        image_sz = 0;
                    }

                    if(result) {
                        free(result); result = nullptr;
                        result_sz = 0;
                    }

                    bytes_awaiting = 0, bytes_processed = 0;
                    break;

                case CMD_GRAPH:
                    state = STATE_RECV_GRAPH;
                    graph_sz = sz;
                    alloc_recv_buffer(&graph, sz, 0);
                    break;

                case CMD_OPERATOR:
                    state = STATE_RECV_OPERATORS;
                    operators_sz = sz;
                    assert(operators_sz <= OPERATORS_SZ);
                    break;

                case CMD_PARAMS:
                    state = STATE_RECV_PARAMS;
                    params_sz = sz;
                    alloc_recv_buffer(&params, sz, 0);
                    break;

                case CMD_IMAGE:
                    state = STATE_RECV_IMAGE;
                    image_sz = sz;
                    alloc_recv_buffer(&image, sz, 0);
                    break;

                case CMD_RESULT:
                    state = STATE_SEND_RESULT;
                    break;

                default:
                    break;
            }

            break;

        case STATE_RECV_GRAPH:
            recv_data(graph, args, &received_graph);
            break;

        case STATE_RECV_OPERATORS:
            recv_data(operators, args, &received_ops);
            if(received_graph && received_ops) {
                vta_examples_init_executor(&executor, &dev, (char *) graph);
            }
            break;

        case STATE_RECV_PARAMS:
            recv_data(params, args, &received_params);
            if(received_params) {
                vta_examples_load_params(executor, (const char *) params, params_sz);
            }
            break;

        case STATE_RECV_IMAGE:
            recv_data(image, args, &received_image);
            if(received_image) {
                result_sz = 1000 * sizeof(float) + 96;
                result = (uint8_t *) malloc(result_sz);
                memset(result, 0, result_sz);

                vta_examples_run_inference(executor, &dev, (float *) image, (float *) result);
            }
            break;

        case STATE_SEND_RESULT:
            send_data(args, result, result_sz);

            // We've gotten rid of what we need, reset back
            received_image = false;
            free(image); image = nullptr;
            free(result); result = nullptr;
            state = STATE_READY;
            break;

        default:
            break;
    }

    return 0;
}

int main() {
    int r;

    // Reset state
    state = STATE_READY;
    received_graph = false, received_ops = false, received_params = false, received_image = false;
    executor = nullptr;

    // Initialize TVM
    vta_examples_init_runtime();
    vta_set_debug(0);

    r = spawn_callee_handler(receive_handler, CALL_RECEIVER);
    if(r < 0) {
        printf(ERROR_PREFIX "Failed to start callee handler\n");
        return -1;
    }

    while(1) {
        yield_thread();
    }
}
