#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>

#include <tvm/runtime/crt/module.h>
#include <tvm/runtime/crt/graph_executor.h>
#include <tvm/runtime/crt/crt.h>
#include <cstring>
#include "app/callee.h"

#include "vta_export.h"
#include "util.h"

#define MEMORY_DEFINES_ONLY
#include "host/Memory.hpp"

#include "platform.h"

#include "app/syscall.h"
char* graph_json;
int graph_json_len;

char* params_bin;
int params_bin_len;

// Necessary variables for reentrancy
static TVMGraphExecutor *executor = nullptr;

extern void set_skip_claim(bool val);

static DLDevice dev = {
        .device_type = kDLExtDev,
        .device_id = 0
};

extern "C" {

// Needs to have C linkage!
extern void _start();

void *get_scratch(size_t size, size_t scratch_size) {
    uintptr_t start_addr;

    // ASSUMPTION: _start is on the first page of the exported function.
    // Partially enforced in the export func linker script.
    start_addr = ((uintptr_t) &_start) & ~((1ul << RISCV_PGSHIFT) - 1);
    return (void *) (start_addr + size - scratch_size);
}

int run_inference(size_t size, size_t scratch_size) {
    size_t result_sz;
    void *result, *scratch = get_scratch(size, scratch_size);

    result_sz = 1000 * sizeof(float);
    result = (uint8_t *) malloc(result_sz);
    memset(result, 0, result_sz);

    vta_examples_run_inference(executor, &dev, (float *) scratch, (float *) result);

    memcpy(scratch, (const void *) result, result_sz);
    free(result);
    return 0;
}

size_t funcsize = -1;
struct sharedmem_info *get_sm() {
    return (struct sharedmem_info *) ((uint8_t *) get_scratch(funcsize, 0x193000) + 0x93000);
}

int setup(uintptr_t graph_json_addr, uintptr_t params_bin_addr, size_t size) {
    funcsize = size;
    uint64_t *scratch = (uint64_t *) get_scratch(funcsize, 0x193000);
    params_bin_len = scratch[0];
    graph_json_len = scratch[1];

    graph_json = (char*)map(graph_json_addr, graph_json_len, 0xA455000);
    params_bin = (char*)map(params_bin_addr, params_bin_len, 0x10F2C000);
    vta_examples_init_executor(&executor, &dev, (char *) graph_json);
    vta_examples_load_params(executor, params_bin, params_bin_len);

    return 0;
}

void teardown() {
    int ret;
    unmap((uintptr_t) graph_json, graph_json_len);
    unmap((uintptr_t) params_bin, params_bin_len);

    // Self-propagate
    ret = spawn_callee_handler(NULL, CALL_MAPPED);
    if(ret < 0) {
        printf(ERROR_PREFIX "Couldn't spawn callee handler CALL_MAPPED\n");
        return;
    }
}

int inference(void *arg) {
    uint64_t *args = (uint64_t *) arg;
    int cmd = args[0];

    switch(cmd) {
        case EXPORT_SETUP:
            return setup(args[1], args[2], args[3]);

        case EXPORT_INFER:
            return run_inference(args[1], args[2]);

        case EXPORT_DIE:
            teardown();
            exit(0);

        default:
            return -1;
    }
}

int main() {
    int r;

    // This should happen first since the calls below may unexpectedly lead to claims
    set_skip_claim(true);

    vta_examples_init_runtime();
    vta_set_debug(0);

    // We are now ready to receive message passes
    r = spawn_callee_handler(inference, CALL_RECEIVER);
    if(r < 0) {
        printf(ERROR_PREFIX "Failed to start callee handler\n");
        return -1;
    }

    // Return here, since we don't want to run destructors but
    // also don't want to tear down this process yet.
    ret_enclave(0);
}

}
