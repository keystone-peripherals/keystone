

#ifndef __CALLEE_H__
#define __CALLEE_H__

#include <pthread.h>
#include <stdint.h>

#include "shared/eyrie_call.h"

#ifdef __cplusplus
extern "C" {
#endif

extern uint64_t pthread_create_start, pthread_create_end;

int spawn_callee_handler(int (*fn) (void *), call_type_t type);

uint64_t call_enclave(int eid, call_type_t type, int nargs, ...);
__attribute__((noreturn)) void ret_enclave(int ret);

#ifdef __cplusplus
}
#endif

#endif  // __CALLEE_H__
