#ifndef __THREAD_H__
#define __THREAD_H__

#include "mm/vm_defs.h"

#define LOG_MAX_THREADS       (3)
#define LOG_STACK_PER_THREAD  (3 + RISCV_PAGE_BITS)

#define MAX_THREADS           (1 << LOG_MAX_THREADS)
#define STACK_PER_THREAD      (1 << LOG_STACK_PER_THREAD)

#define MAIN_PROCESS          0

#ifndef __PREPROCESSING__
#include <stdbool.h>
#include "util/regs.h"

void thread_init();

pte *get_current_root();
void set_current_root(pte *root) ;
uintptr_t get_program_break();
void set_program_break(uintptr_t new_break);
uintptr_t get_avail_vpn();
void set_avail_vpn(uintptr_t new_avail_vpn);

bool is_main_thread();
int get_current_tid();
int get_parent_tid();
int get_avail_tid(bool is_thread, void *root_pt);
void free_tid(int tid);

void or_into_state(int tid, struct encl_ctx *ctx);
void context_switch_to(int tid, struct encl_ctx *regs);

#endif

#endif  // __THREAD_H__
