#ifndef _CALLEE_H_
#define _CALLEE_H_

#ifdef USE_CALLEE

#define NUM_THREAD_SLOTS 8

#ifndef __PREPROCESSING__
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

extern uint64_t timestamp;

int callee_init();
int free_thread_entry(bool exit_proc, uint64_t arg);
int get_call_args(void *buf, size_t size);
int get_timing_info(void *start, void *end);

int syscall_clone(unsigned long flags, uintptr_t newsp,
              int *parent_tidptr, uintptr_t tls, int *child_tidptr,
              uintptr_t gp /* needed for switching */);
int syscall_set_tid_address(int *tidptr_t);
#endif // __PREPROCESSING__

#endif // USE_CALLEE

#endif  // _CALLEE_H_
