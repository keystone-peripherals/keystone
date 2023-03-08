#ifndef __EYRIE_CALL_H__
#define __EYRIE_CALL_H__

// Syscalls that get passed down to the SM
#define RUNTIME_SYSCALL_UNKNOWN             1000
#define RUNTIME_SYSCALL_OCALL               1001
#define RUNTIME_SYSCALL_SHAREDCOPY          1002
#define RUNTIME_SYSCALL_ATTEST_ENCLAVE      1003
#define RUNTIME_SYSCALL_GET_SEALING_KEY     1004
#define RUNTIME_SYSCALL_CLAIM_MMIO          1005
#define RUNTIME_SYSCALL_RELEASE_MMIO        1006
#define RUNTIME_SYSCALL_CALL_ENCLAVE        1007
#define RUNTIME_SYSCALL_RET_ENCLAVE         1008
#define RUNTIME_SYSCALL_GET_CALL_ARGS       1009
#define RUNTIME_SYSCALL_SHARE_REGION        1010
#define RUNTIME_SYSCALL_UNSHARE_REGION      1011
#define RUNTIME_SYSCALL_EXIT                1101

// Runtime syscalls
#define RUNTIME_SYSCALL_YIELD_MAIN_THREAD   2000
#define RUNTIME_SYSCALL_TRANSLATE           2001
#define RUNTIME_SYSCALL_MAP                 2002
#define RUNTIME_SYSCALL_UNMAP               2003
#define RUNTIME_SYSCALL_GET_TIMING_INFO     2004
#define RUNTIME_SYSCALL_MAP_SHARED_BUF      2005


// Interface definitions
typedef enum {
  CALL_RECEIVER = 0,
  CALL_MAPPED,
//  CALL_MAPPED_ASYNC
} call_type_t;

#define CALLER_MAX_ARGS                     5
struct shared_callee_buffer {
  // Arguments to the called function
  uint64_t args[CALLER_MAX_ARGS];
};



#endif  // __EYRIE_CALL_H__
