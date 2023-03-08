
#include <tvm/runtime/crt/module.h>
#include "interface.h"

 // We want some functions to be linked into the receiver, since this simplifies
 // various system behaviors ranging from consistent memory allocations to global
 // constructors etc etc. However, these functions still need to be defined in
 // order to avoid linker errors. Therefore, we define short thunk functions here
 // (in the sender technically) which will dispatch out to the real implementations
 // in the receiver. The initialize_comms function below is linked to offset 0 in
 // the operator library binary, such that it can easily be called by the receiver
 // when initializing the operator library. The receiver will pass a reference to
 // a struct cmd_table holding the addresses of the concrete implementations, and
 // will receive a pointer to TVMSystemLibEntryPoint which will allow the receiver
 // to initialize the operator library as a C module.

 const struct cmd_table *dispatch_table = NULL;

__attribute__((section(".keystone")))
syslib_entry_fn initialize_comms(const struct cmd_table *table) {
    dispatch_table = table;
    return TVMSystemLibEntryPoint;
}


void TVMAPISetLastError(const char* msg) {
    return dispatch_table->TVMAPISetLastError(msg);
}

void* TVMBackendAllocWorkspace(int device_type, int device_id, uint64_t nbytes, int dtype_code_hint,
                               int dtype_bits_hint) {
    return dispatch_table->TVMBackendAllocWorkspace(device_type, device_id, nbytes, dtype_code_hint, dtype_bits_hint);
}

int TVMBackendFreeWorkspace(int device_type, int device_id, void* ptr) {
    return dispatch_table->TVMBackendFreeWorkspace(device_type, device_id, ptr);
}

int TVMBackendGetFuncFromEnv(void* mod_node, const char* func_name, TVMFunctionHandle* func) {
    return dispatch_table->TVMBackendGetFuncFromEnv(mod_node, func_name, func);
}

int TVMBackendParallelLaunch(FTVMParallelLambda flambda, void* cdata, int num_task) {
    return dispatch_table->TVMBackendParallelLaunch(flambda, cdata, num_task);
}

int TVMFuncCall(TVMFunctionHandle func, TVMValue* args, int* arg_type_codes, int num_args,
                TVMValue* ret_val, int* ret_type_code) {
    return dispatch_table->TVMFuncCall(func, args, arg_type_codes, num_args, ret_val, ret_type_code);
}

void* VTABufferCPUPtr(VTACommandHandle cmd, void* buffer) {
    return dispatch_table->VTABufferCPUPtr(cmd, buffer);
}

int VTADepPop(VTACommandHandle cmd, int from_qid, int to_qid) {
    return dispatch_table->VTADepPop(cmd, from_qid, to_qid);
}

int VTADepPush(VTACommandHandle cmd, int from_qid, int to_qid) {
    return dispatch_table->VTADepPush(cmd, from_qid, to_qid);
}

void VTALoadBuffer2D(VTACommandHandle cmd, void* src_dram_addr, uint32_t src_elem_offset, uint32_t x_size, uint32_t y_size,
                  uint32_t x_stride, uint32_t x_pad_before, uint32_t y_pad_before,
                  uint32_t x_pad_after, uint32_t y_pad_after, uint32_t dst_sram_index,
                  uint32_t dst_memory_type) {
    return dispatch_table->VTALoadBuffer2D(cmd, src_dram_addr, src_elem_offset, x_size, y_size, x_stride,
                                           x_pad_before, y_pad_before, x_pad_after, y_pad_after, dst_sram_index, dst_memory_type);
}

int VTAPushALUOp(void** uop_handle, int (*finit)(void*), void* signature, int nbytes) {
    return dispatch_table->VTAPushALUOp(uop_handle, finit, signature, nbytes);
}

int VTAPushGEMMOp(void** uop_handle, int (*finit)(void*), void* signature, int nbytes) {
    return dispatch_table->VTAPushGEMMOp(uop_handle, finit, signature, nbytes);
}

void VTAStoreBuffer2D(VTACommandHandle cmd, uint32_t src_sram_index, uint32_t src_memory_type, void* dst_dram_addr,
                   uint32_t dst_elem_offset, uint32_t x_size, uint32_t y_size,
                   uint32_t x_stride) {
    return dispatch_table->VTAStoreBuffer2D(cmd, src_sram_index, src_memory_type, dst_dram_addr, dst_elem_offset, x_size, y_size, x_stride);
}

void VTASynchronize(VTACommandHandle cmd, uint32_t wait_cycles) {
    return dispatch_table->VTASynchronize(cmd, wait_cycles);
}

VTACommandHandle VTATLSCommandHandle(void) {
    return dispatch_table->VTATLSCommandHandle();
}

void VTAUopLoopBegin(uint32_t extent, uint32_t dst_factor, uint32_t src_factor,
                     uint32_t wgt_factor) {
    return dispatch_table->VTAUopLoopBegin(extent, dst_factor, src_factor, wgt_factor);
}

void VTAUopLoopEnd(void) {
    return dispatch_table->VTAUopLoopEnd();
}

void VTAUopPush(uint32_t mode, uint32_t reset_out, uint32_t dst_index, uint32_t src_index,
                uint32_t wgt_index, uint32_t opcode, uint32_t use_imm, int32_t imm_val) {
    return dispatch_table->VTAUopPush(mode, reset_out, dst_index, src_index, wgt_index, opcode, use_imm, imm_val);
}

void *memset(void *ptr, int value, size_t num) {
    int i;
    for(i = 0; i < num; i++) {
        ((unsigned char *) ptr)[i] = (unsigned char) value;
    }

    return ptr;
}