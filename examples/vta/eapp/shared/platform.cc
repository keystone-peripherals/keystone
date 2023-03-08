#include <tvm/runtime/logging.h>

#include <tvm/runtime/crt/crt.h>
#include <tvm/runtime/crt/graph_executor.h>
#include <tvm/runtime/crt/packed_func.h>
#include <tvm/runtime/crt/page_allocator.h>

#include <string.h>

#include "../../include/platform.h"
#include "../../util/util.h"

namespace tvm {
    namespace runtime {
        namespace detail {
            LogFatal::Entry& LogFatal::GetEntry() {
                static thread_local LogFatal::Entry result;
                return result;
            }
        }

        std::string Backtrace() {
            return nullptr;
        }
    }
}

extern "C" {

int TVMBackendGetFuncFromEnv(void* mod_node, const char* func_name, TVMFunctionHandle* func) {
    return TVMFuncGetGlobal(func_name, func);
}

void __attribute__((format(printf, 1, 2))) TVMLogf(const char* msg, ...) {
    va_list args;
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
}

extern void* VTABufferAlloc(size_t);
extern void VTABufferFree(void*);
extern void VTABufferCopy(const void*, size_t, void*, size_t, size_t, int);

}

tvm_crt_error_t TVMPlatformMemoryAllocate(size_t num_bytes, DLDevice dev, void** out_ptr) {
    void *res = NULL;
    switch (dev.device_type) {
        case kDLCPU:
            res = std::malloc(num_bytes);
            break;

        case kDLExtDev:
            res = VTABufferAlloc(num_bytes);
            break;

        default:
            break;
    }

    if(res) {
        *out_ptr = res;
        return kTvmErrorNoError;
    }

    return kTvmErrorPlatformNoMemory;
}

tvm_crt_error_t TVMPlatformMemoryFree(void* ptr, DLDevice dev) {
    switch(dev.device_type) {
        case kDLCPU:
            std::free(ptr);
            break;

        case kDLExtDev:
            VTABufferFree(ptr);
            break;

        default:
            return kTvmErrorPlatformNoMemory;
    }

    return kTvmErrorNoError;
}

tvm_crt_error_t TVMPlatformMemoryCopy(const void* from, void *to, size_t size,
                                      DLDevice dev_from, DLDevice dev_to) {
    int kind_mask = 0;
    if (dev_from.device_type != kDLCPU) {
        kind_mask |= 2;
    }
    if (dev_to.device_type != kDLCPU) {
        kind_mask |= 1;
    }

    if(kind_mask) {
        VTABufferCopy(from, 0, to, 0, size, kind_mask);
    } else {
        memcpy(to, from, size);
    }

    return kTvmErrorNoError;
}

void TVMPlatformAbort(tvm_crt_error_t error_code) {
    // todo
    exit(-1);
}

tvm_crt_error_t TVMPlatformTimerStart() {
    return kTvmErrorFunctionCallNotImplemented;
}

tvm_crt_error_t TVMPlatformTimerStop(double* elapsed_time_seconds) {
    return kTvmErrorFunctionCallNotImplemented;
}

int VTASetDevice(TVMValue* args, int* type_codes, int num_args,
                 TVMValue* ret_value, int* ret_type_codes) {
    return 0;
}


void vta_examples_init_runtime(void) {
    TVM_CCALL(TVMInitializeRuntime());
    TVMFuncRegisterGlobal("__tvm_set_device", (TVMFunctionHandle) &VTASetDevice, 0);
}

void vta_examples_init_executor(TVMGraphExecutor **executor, DLDevice *dev, char *graph) {
    /* This sequence below ends up calling TVMSystemLibEntryPoint */
    TVMPackedFunc pf;
    INSTRUMENT(PHASE_INIT_EXECUTOR, {
        TVMArgs args = TVMArgs_Create(NULL, NULL, 0);
        TVM_CCALL(TVMPackedFunc_InitGlobalFunc(&pf, "runtime.SystemLib", &args));
        TVM_CCALL(TVMPackedFunc_Call(&pf));

        TVMModuleHandle mod_syslib = TVMArgs_AsModuleHandle(&pf.ret_value, 0);
        TVM_CCALL(TVMGraphExecutor_Create(graph, mod_syslib, dev, executor));
    });
}

void vta_examples_load_params(TVMGraphExecutor *executor, const char *params, uint32_t len) {
    INSTRUMENT(PHASE_LOAD_PARAMS, {
        TVM_CCALL(TVMGraphExecutor_LoadParams(executor, params, len));
    });
}

#define BATCH      1
#define OUTPUT_LEN 1000

void vta_examples_run_inference(TVMGraphExecutor *executor, DLDevice *dev, float *input, float *output) {
    int64_t input_shape[4] = {BATCH, 3, 224, 224};
    DLTensor input_tensor = {
            .data = input,
            .device = *dev,
            .ndim = 4,
            .dtype = {
                    .code = kDLFloat,
                    .bits = 32, .lanes = 1
            },
            .shape = input_shape,
            .strides = NULL,
            .byte_offset = 0

    };

    int64_t output_shape[2] = {BATCH, OUTPUT_LEN};
    DLTensor output_tensor = {
            .data = output,
            .device = {
                    .device_type = kDLCPU,
                    .device_id = 0
            },
            .ndim = 2,
            .dtype = {
                    .code = kDLFloat,
                    .bits = 32, .lanes = 1
            },
            .shape = output_shape,
            .strides = NULL,
            .byte_offset = 0
    };

    TVMGraphExecutor_SetInput(executor, "data", &input_tensor);
    INSTRUMENT(PHASE_INFERENCE, {
        TVMGraphExecutor_Run(executor);
    })
    TVMGraphExecutor_GetOutput(executor, 0, &output_tensor);
}

extern "C" {

typedef void* VTACommandHandle;
extern VTACommandHandle VTATLSCommandHandle();
extern void VTASetDebugMode(VTACommandHandle cmd, int debug_flag);

}

void vta_set_debug(int debug_flag) {
        VTASetDebugMode(VTATLSCommandHandle(), debug_flag);
}