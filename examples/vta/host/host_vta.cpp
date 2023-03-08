
// Standard includes
#include <thread>
#include <sys/mman.h>

// TVM includes
#include <tvm/runtime/crt/module.h>
#include <tvm/runtime/crt/graph_executor.h>
#include <tvm/runtime/crt/crt.h>

// Keystone includes
#include "edge/edge_call.h"
#include "host/keystone.h"

// Internal includes
#include "interface.h"
#include "platform.h"
#include "util.h"

using namespace Keystone;
#define TIME_SINCE_UNIX_EPOCH 1709595745 // == Mon Mar  4 03:42:26 PM PST 2024

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

static uint8_t *operators = nullptr, *graph = nullptr, *params = nullptr, *image = nullptr, *result = nullptr;
static size_t operators_sz = 0, graph_sz = 0, params_sz = 0, image_sz = 0, result_sz = 0;

static TVMGraphExecutor *executor = nullptr;
static DLDevice dev = {
        .device_type = kDLExtDev,
        .device_id = 0
};

const TVMModule* TVMSystemLibEntryPoint(void) {
    initialize_comms_fn initialize_comms = (initialize_comms_fn) operators;
    syslib_entry_fn real_entry_point = initialize_comms(&dispatch_table);
    return real_entry_point();
}

void receive_metadata(void *ptr) {
    size_t *sizes = (size_t *) edge_call_data_ptr();

    graph_sz = sizes[0];
    operators_sz = sizes[1];
    params_sz = sizes[2];
    image_sz = sizes[3];
    result_sz = sizes[4];

    if(graph_sz) {
        graph = (uint8_t *) malloc(graph_sz);
    }

    if(operators_sz) {
        // Allocate some RWX memory!
        operators = (uint8_t *) mmap((void *) OPERATOR_LIB_ADDR, operators_sz,
                                     PROT_READ | PROT_WRITE | PROT_EXEC,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }

    if(params_sz) {
        params = (uint8_t *) malloc(params_sz);
    }

    if(image_sz) {
        if(!image) {
            image = (uint8_t *) malloc(image_sz);
        }
    }

    if(result_sz) {
        result = (uint8_t *) malloc(result_sz);
        memset(result, 0, result_sz);
    }
}

void receive_graph(void *ptr) {
    memcpy(graph, (const void *) edge_call_data_ptr(), graph_sz);
}

void receive_operator(void *ptr) {
    memcpy(operators, (const void *) edge_call_data_ptr(), operators_sz);
    vta_examples_init_executor(&executor, &dev, (char *) graph);
}

void receive_params(void *ptr) {
    memcpy(params, (const void *) edge_call_data_ptr(), params_sz);
    vta_examples_load_params(executor, (const char *) params, params_sz);
}

void receive_image(void *ptr) {
    memcpy(image, (const void *) edge_call_data_ptr(), image_sz);
    vta_examples_run_inference(executor, &dev, (float *) image, (float *) result);
}

void receive_result(void *ptr) {
    memcpy((void *) edge_call_data_ptr(), result, result_sz);
}

void reset(void *ptr) {
    TVMGraphExecutor_Release(&executor);
    executor = nullptr;
    VTAMemReset();

    if(graph) {
        free(graph); graph = nullptr;
        graph_sz = 0;
    }

    if(operators) {
        munmap(operators, operators_sz); operators = nullptr;
        operators_sz = 0;
    }

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
}

Enclave encl;
struct sharedmem_info *get_sm() {
    return (struct sharedmem_info *) ((uint8_t *) encl.getSharedBuffer() + LINUX_OWNED_UNTRUSTED_SIZE);
}

int main(int argc, char **argv) {
    Params params;

    // Typical eapp setup
    params.setFreeMemSize(1024 * 1024 * std::stol(argv[3]));
    params.setUntrustedMem(DEFAULT_UNTRUSTED_PTR, LINUX_OWNED_UNTRUSTED_SIZE + SHAREDMEM_SIZE);
    params.setTimeSinceUnixEpoch(TIME_SINCE_UNIX_EPOCH);

    printf("Loading eapp %s with rt %s\n", argv[1], argv[2]);
    encl.init(argv[1], argv[2], params, 0, true);

    // Register custom edgecalls
    register_call(CMD_METADATA, receive_metadata);
    register_call(CMD_GRAPH, receive_graph);
    register_call(CMD_OPERATOR, receive_operator);
    register_call(CMD_PARAMS, receive_params);
    register_call(CMD_IMAGE, receive_image);
    register_call(CMD_RESULT, receive_result);
    register_call(CMD_RESET, reset);

    // Setup TVM on the host
    vta_examples_init_runtime();
    vta_set_debug(0);

    encl.registerOcallDispatch(incoming_call_dispatch);
    edge_call_init_internals(
            (uintptr_t)encl.getSharedBuffer(), encl.getSharedBufferSize());

    encl.run();
    return 0;
}
