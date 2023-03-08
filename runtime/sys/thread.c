#include "sys/thread.h"

#include <stdint.h>

#include "asm/csr.h"
#include "mm/common.h"
#include "mm/vm.h"
#include "util/rt_util.h"
#include "util/string.h"

struct thread {
  /* ORDER SENSITIVE: the following struct members are accessed through
   * assembly code. As such, you SHOULD NOT MOVE THESE unless you know what
   * you are doing. Documentation is included both here, and next to the
   * assembly usage sites for each of these */

  // Context storage, used in entry.S to spill exception register state
  // to memory.
  struct encl_ctx ctx;

  // Kernel stack pointer to use for this thread, also used in entry.S
  void *ksp;

  // Callee lock for this thread

  /* Memory attributes */

  // Root page table
  pte *root_pt;

  // VM allocation
  uintptr_t current_program_break;
  uintptr_t current_avail_vpn;

  /* Metadata */
  bool used;

  bool is_thread;
  int parent_tid;
} threads [MAX_THREADS] = {0};

pte init_root_pt[BIT(RISCV_PT_INDEX_BITS)] __attribute__((aligned(RISCV_PAGE_SIZE))) = {0};

/* The current thread struct, as described by sscratch */
#define current_thread  ((struct thread *) csr_read(sscratch))

/* The thread struct where metadata is sourced from */
#define metadata_thread ({ struct thread *s = current_thread; s->is_thread ? \
                            &threads[s->parent_tid] : s; })

/* The enclave's main thread struct */
#define main_thread (&threads[MAIN_PROCESS])

extern uintptr_t kernel_stacks_start;
extern uintptr_t kernel_stacks_end;

void thread_init() {
  // Main thread gets the last kernel stack
  csr_write(sscratch, main_thread);
  current_thread->ksp = (void *) &kernel_stacks_end;

  // Init main thread memory
  current_thread->root_pt = init_root_pt;
  current_thread->current_avail_vpn = vpn(EYRIE_ANON_REGION_START);
  current_thread->used = true;
}

pte *get_current_root() {
  return metadata_thread->root_pt;
}

void set_current_root(pte *root) {
  metadata_thread->root_pt = root;
}

uintptr_t get_program_break(){
  return metadata_thread->current_program_break;
}

void set_program_break(uintptr_t new_break){
  metadata_thread->current_program_break = new_break;
}

uintptr_t get_avail_vpn() {
  return metadata_thread->current_avail_vpn;
}

void set_avail_vpn(uintptr_t new_avail_vpn) {
  metadata_thread->current_avail_vpn = new_avail_vpn;
}

bool is_main_thread() {
  return current_thread == main_thread;
}

int get_current_tid() {
  return current_thread - threads;
}

int get_parent_tid() {
  if(current_thread->parent_tid) {
    return current_thread->parent_tid;
  } else {
    return MAIN_PROCESS;
  }
}

int get_avail_tid(bool is_thread, void *root_pt) {
  int i;

  for(i = 0; i < MAX_THREADS; i++) {
    if(!threads[i].used) {
      memset(&threads[i].ctx, 0, sizeof(struct encl_ctx));
      threads[i].ksp = ((void *) &kernel_stacks_end) - STACK_PER_THREAD * i;

      threads[i].used = true;
      threads[i].is_thread = is_thread;

      if(is_thread) {
        threads[i].parent_tid = (int) (metadata_thread - threads);
      } else {
        threads[i].root_pt = root_pt;
        threads[i].current_avail_vpn = vpn(EYRIE_ANON_REGION_START);
      }

      return i;
    }
  }

  return -1;
}

void free_tid(int tid) {
  if(tid < MAX_THREADS) {
    memset(&threads[tid], 0, sizeof(struct thread));
    threads[tid].used = false;
  }
}

void or_into_state(int tid, struct encl_ctx *ctx) {
  int i;
  uintptr_t *src, *dst;

  if(threads[tid].used) {
    for(src = (uintptr_t *) ctx, dst = (uintptr_t *) &threads[tid].ctx, i = 0;
         i < sizeof(struct encl_ctx); i += sizeof(uintptr_t), src++, dst++) {
      *dst |= *src;
    }
  }
}

void context_switch_to(int tid, struct encl_ctx *regs) {
  // Set the registers if necessary
  if(regs) {
    memcpy(regs, &threads[tid].ctx, sizeof(struct encl_ctx));
  }

  // Set the thread id
  csr_write(sscratch, &threads[tid]);

  // Set the new page table
  if(metadata_thread == main_thread) {
    // This is compiled-in static space
    csr_write(satp, satp_new(kernel_va_to_pa(get_current_root())));
  } else {
    // This is dynamically allocated via spa_get
    csr_write(satp, satp_new(__pa((uintptr_t) get_current_root())));
  }
  tlb_flush();
}