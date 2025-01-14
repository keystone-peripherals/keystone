//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#ifndef __SYSCALL_H__
#define __SYSCALL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "sealing.h"

#include "shared/eyrie_call.h"

#define SYSCALL(which, arg0, arg1, arg2, arg3, arg4, arg5)     \
  ({                                                           \
    register uintptr_t a0 asm("a0") = (uintptr_t)(arg0);       \
    register uintptr_t a1 asm("a1") = (uintptr_t)(arg1);       \
    register uintptr_t a2 asm("a2") = (uintptr_t)(arg2);       \
    register uintptr_t a3 asm("a3") = (uintptr_t)(arg3);       \
    register uintptr_t a4 asm("a4") = (uintptr_t)(arg4);       \
    register uintptr_t a5 asm("a5") = (uintptr_t)(arg5);       \
    register uintptr_t a7 asm("a7") = (uintptr_t)(which);      \
    asm volatile("ecall"                                       \
                 : "+r"(a0)                                    \
                 : "r"(a1), "r"(a2), "r"(a3), "r"(a4),         \
                   "r"(a5), "r"(a7)                            \
                 : "memory");                                  \
    a0;                                                        \
  })

#define SYSCALL_0(which) SYSCALL(which, 0, 0, 0, 0, 0, 0)
#define SYSCALL_1(which, arg0) SYSCALL(which, arg0, 0, 0, 0, 0, 0)
#define SYSCALL_2(which, arg0, arg1) SYSCALL(which, arg0, arg1, 0, 0, 0, 0)
#define SYSCALL_3(which, arg0, arg1, arg2) \
  SYSCALL(which, arg0, arg1, arg2, 0, 0, 0)
#define SYSCALL_4(which, arg0, arg1, arg2, arg3) \
  SYSCALL(which, arg0, arg1, arg2, arg3, 0, 0)
#define SYSCALL_5(which, arg0, arg1, arg2, arg3, arg4) \
  SYSCALL(which, arg0, arg1, arg2, arg3, arg4, 0)
#define SYSCALL_6(which, arg0, arg1, arg2, arg3, arg4, arg5) \
  SYSCALL(which, arg0, arg1, arg2, arg3, arg4, arg5)

int
copy_from_shared(void* dst, uintptr_t offset, size_t data_len);

int
ocall(
    unsigned long call_id, void* data, size_t data_len, void* return_buffer,
    size_t return_len);
uintptr_t
untrusted_mmap();
int
attest_enclave(void* report, void* data, size_t size);

int
get_sealing_key(
    struct sealing_key* sealing_key_struct, size_t sealing_key_struct_size,
    void* key_ident, size_t key_ident_size);

int claim_mmio(const char *devname, size_t namelen);
int release_mmio(const char *devname, size_t namelen);
int share_region(uintptr_t addr, size_t size, int with);
int unshare_region(uintptr_t addr, int with);
int get_call_args(void *addr, size_t size);

int yield_thread();
uintptr_t translate(uintptr_t va);
uintptr_t map(uintptr_t pa, size_t size, uintptr_t va);
int unmap(uintptr_t va, size_t size);
int get_timing_info(void *start, void *end);
uintptr_t map_shared_buf();

#ifdef __cplusplus
}
#endif

#endif /* syscall.h */
