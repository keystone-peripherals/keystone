//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#include "syscall.h"

/* this implementes basic system calls for the enclave */

int
ocall(
    unsigned long call_id, void* data, size_t data_len, void* return_buffer,
    size_t return_len) {
  return SYSCALL_5(RUNTIME_SYSCALL_OCALL,
      call_id, data, data_len, return_buffer, return_len);
}

int
copy_from_shared(void* dst, uintptr_t offset, size_t data_len) {
  return SYSCALL_3(RUNTIME_SYSCALL_SHAREDCOPY, dst, offset, data_len);
}

int
attest_enclave(void* report, void* data, size_t size) {
  return SYSCALL_3(RUNTIME_SYSCALL_ATTEST_ENCLAVE, report, data, size);
}

/* returns sealing key */
int
get_sealing_key(
    struct sealing_key* sealing_key_struct, size_t sealing_key_struct_size,
    void* key_ident, size_t key_ident_size) {
  return SYSCALL_4(RUNTIME_SYSCALL_GET_SEALING_KEY,
      sealing_key_struct, sealing_key_struct_size,
      key_ident, key_ident_size);
}

int claim_mmio(const char *devname, size_t namelen) {
  return SYSCALL_2(RUNTIME_SYSCALL_CLAIM_MMIO, devname, namelen);
}

int release_mmio(const char *devname, size_t namelen) {
  return SYSCALL_2(RUNTIME_SYSCALL_RELEASE_MMIO, devname, namelen);
}

int get_call_args(void * addr, size_t size) {
  return SYSCALL_2(RUNTIME_SYSCALL_GET_CALL_ARGS, (uintptr_t) addr, size);
}

int share_region(uintptr_t addr, size_t size, int with) {
  return SYSCALL_3(RUNTIME_SYSCALL_SHARE_REGION, addr, size, with);
}

int unshare_region(uintptr_t addr, int with) {
  return SYSCALL_2(RUNTIME_SYSCALL_UNSHARE_REGION, addr, with);
}

int yield_thread() {
  return SYSCALL_0(RUNTIME_SYSCALL_YIELD_MAIN_THREAD);
}

uintptr_t translate(uintptr_t va) {
  return SYSCALL_1(RUNTIME_SYSCALL_TRANSLATE, va);
}

uintptr_t map(uintptr_t pa, size_t size, uintptr_t va) {
  return SYSCALL_3(RUNTIME_SYSCALL_MAP, pa, size, va);
}

int unmap(uintptr_t va, size_t size) {
  return SYSCALL_2(RUNTIME_SYSCALL_UNMAP, va, size);
}

int get_timing_info(void *start, void *end) {
  return SYSCALL_2(RUNTIME_SYSCALL_GET_TIMING_INFO, start, end);
}

uintptr_t map_shared_buf() {
  return SYSCALL_0(RUNTIME_SYSCALL_MAP_SHARED_BUF);
}