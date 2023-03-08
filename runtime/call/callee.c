#include "call/callee.h"

#ifdef USE_CALLEE

// These defines are necessary so that we can reach
// the definitions of CLONE_* used by libc

#define __USE_GNU
#define __USE_XOPEN2K
#include <sched.h>
#undef __USE_XOPEN2K
#undef __USE_GNU

#include <asm-generic/errno.h>
#include <stdbool.h>

#include "asm/csr.h"
#include "call/sbi.h"
#include "eyrie_call.h"
#include "mm/common.h"
#include "mm/freemem.h"
#include "mm/mm.h"
#include "mm/vm.h"
#include "sys/boot.h"
#include "sys/env.h"
#include "sys/thread.h"
#include "uaccess.h"
#include "util/rt_elf.h"
#include "util/spinlock.h"
#include "util/string.h"

#ifdef USE_CALLEE_PROFILE
#define NUM_MBS 2
#define SAMPLE_BUFFER_ELEMS (1024 * 256)
#include "profile_util.h"
#endif

uint64_t callee_start, callee_end;

/* Low-level thread availability tracking */

typedef enum {
  STATE_EMPTY = 0,
  STATE_READY,
  STATE_RUNNING
} thread_state_t;


static struct threadinfo_t {
  thread_state_t state;
  uintptr_t clear_child_tid;
  uintptr_t mapped_addr, mapped_size;

  int tid;
  bool is_thread;

#ifdef USE_CALLEE_PROFILE
  struct sharedmem_info *sm;
  size_t sm_size;
#endif

} threadinfo[NUM_THREAD_SLOTS];

spin_lock_t threadinfo_lock = 0;

/* Interface to the eapp and SDK */
struct shared_callee_buffer encl_call_buf;
spin_lock_t encl_call_lock = 0;

int get_call_args(void *buf, size_t size) {
  if(size == sizeof(struct shared_callee_buffer)) {
    copy_to_user(buf, &encl_call_buf, size);
    spin_unlock(&encl_call_lock);
    return 0;
  }

  return -EINVAL;
}

int get_timing_info(void *start, void *end) {
  if(start) {
    copy_to_user(start, &callee_start, sizeof(uint64_t));
  }

  if(end) {
    copy_to_user(end, &callee_end, sizeof(uint64_t));
  }

  return 0;
}

/* Call handler, used by callees into this enclave */

int switch_to_thread(struct threadinfo_t *tinfo, struct encl_ctx *regs) {
  struct regs *r = &regs->regs;

  // Copy registers into the buffer, which we have
  // exclusively acquired for this thread only
  memcpy(&encl_call_buf, &r->a0, sizeof(struct shared_callee_buffer));

  // Context switch
  context_switch_to(tinfo->tid, regs);
  return 0;
}

int switch_to_process(struct threadinfo_t *tinfo, struct encl_ctx *regs) {
  void *user_stack;

#define MAX_PHEADERS 10
  uint8_t elfinfo[sizeof(ELF(Ehdr)) + MAX_PHEADERS * sizeof(ELF(Phdr))];
  ELF(Ehdr) *hdr = (ELF(Ehdr) *) &elfinfo[0], *uhdr;
  ELF(Phdr) *phdrs = (ELF(Phdr) *) &elfinfo[sizeof(ELF(Ehdr))], *uphdrs;

  // The first thing we need to do is context switch so that we can
  // access our brand-spankin new page table.
  context_switch_to(tinfo->tid, NULL);

  // Memory init
  set_avail_vpn(vpn(EYRIE_ANON_REGION_START));
  set_program_break(0x0000003000000000);

  // Then, we can map the executable into the user address space. This
  // should be a static-pie executable, so it can go anywhere.
  tinfo->mapped_size = regs->regs.a2;
  tinfo->mapped_addr = map_anywhere_with_dynamic_page_table(regs->regs.a1,
                                                      regs->regs.a2,true);
  uhdr = (void *) tinfo->mapped_addr;

  // Now, we need a stack. In order to correctly set up aux vectors etc
  // we need to grab the ELF header. We do this using copy_from_user instead
  // of ALLOW_USER_ACCESS since the init_user_stack_and_env function itself
  // calls copy_to_user, and it turns out that this clobbers sstatus in
  // unexpected ways
  copy_from_user(hdr, uhdr, sizeof(ELF(Ehdr)));
  if(hdr->e_phnum > MAX_PHEADERS) {
    return -EINVAL;
  }

  uphdrs = (void *) uhdr + hdr->e_phoff;
  copy_from_user(phdrs, uphdrs, hdr->e_phnum * sizeof(ELF(Phdr)));
  hdr->e_phoff = sizeof(ELF(Ehdr));

  user_stack = init_user_stack_and_env(hdr);
  regs->regs.sp = (uintptr_t) user_stack;

  // We don't need to initialize some csrs here (stvec, sstatus, etc) since
  // these were correctly initialized on eyrie_boot, and the SM has copied
  // them into this execution thread. We do, however, need to set sepc to
  // the ELF entry point.
  regs->regs.sepc = (uintptr_t) uhdr + hdr->e_entry;
  return 0;
}

uint64_t timestamp;

int encl_call_handler_c(struct encl_ctx *regs)  {
  int i, ret = 0;

#ifdef USE_CALLEE_PROFILE
  // Immediately return with the timestamp
  timestamp = regs->regs.t4;
#endif

  bool call_is_thread = (regs->regs.a0 == CALL_RECEIVER);

  spin_lock(&threadinfo_lock);
  for(i = 0; i < NUM_THREAD_SLOTS; i++) {
    if(threadinfo[i].state == STATE_READY && threadinfo[i].is_thread == call_is_thread) {
      // We can run this thread!
      threadinfo[i].state = STATE_RUNNING;

      if(call_is_thread) {
#ifdef USE_CALLEE_PROFILE
        regs->regs.a4 = timestamp;
#endif
        ret = switch_to_thread(&threadinfo[i], regs);
      } else {
        ret = switch_to_process(&threadinfo[i], regs);

#ifdef USE_CALLEE_PROFILE
        // Set up the shared region here in kernel space, since that's where
        // we'll end up stashing timing traces.
        threadinfo[i].sm = (void *) map_with_dynamic_page_table(regs->regs.a3, regs->regs.a4, regs->regs.a3, true);
        threadinfo[i].sm_size = regs->regs.a4;
#endif

        // We don't need this for process calls
        spin_unlock(&encl_call_lock);
      }
      break;
    }
  }

  spin_unlock(&threadinfo_lock);
  if(i == NUM_THREAD_SLOTS) {
    return -EINVAL;
  } else {
    return ret;
  }
}

/* Setup infrastructure, used by the main thread in this enclave */

extern void* encl_call_handler;
int callee_init() {
  return sbi_register_handler((uintptr_t) &encl_call_handler);
}

static int initialize_thread(struct threadinfo_t *tinfo, unsigned long flags,
                  uintptr_t newsp, int *parent_tidptr, uintptr_t tls,
                  int *child_tidptr, uintptr_t gp) {
  struct encl_ctx new_regs = {
    .regs.sp = newsp,
    .regs.gp = gp,
    .regs.sepc = csr_read(sepc) + 4
};

  int tid = get_avail_tid(true, NULL);
  if(tid < 0) {
    return -ENOMEM;
  }

  if(flags & CLONE_SETTLS) {
    new_regs.regs.tp = tls;
  }

  or_into_state(tid, &new_regs);
  tinfo->state = STATE_READY;
  tinfo->tid = tid;
  tinfo->is_thread = true;

  if(flags & CLONE_CHILD_CLEARTID) {
    tinfo->clear_child_tid = (uintptr_t) child_tidptr;
  }

  if(flags & CLONE_PARENT_SETTID) {
    copy_to_user(parent_tidptr, &tid, sizeof(int));
  }

  if(flags & CLONE_CHILD_SETTID) {
    copy_to_user(child_tidptr, &tid, sizeof(int));
  }

  return tid;
}

static int initialize_process(struct threadinfo_t *tinfo) {
  size_t idx;

  // First, create a new page table that copies the runtime mappings.
  pte *root = get_current_root(), *pt = (pte *) spa_get_zero();

  // Link the eyrie mappings into this page table.
  idx = RISCV_GET_PT_INDEX(EYRIE_LOAD_START, 1); pt[idx] = root[idx];
  idx = RISCV_GET_PT_INDEX(EYRIE_PAGING_START, 1); pt[idx] = root[idx];
  idx = RISCV_GET_PT_INDEX(EYRIE_UNTRUSTED_START, 1); pt[idx] = root[idx];

  // This one also covers misc mappings (like for drivers)
  idx = RISCV_GET_PT_INDEX(EYRIE_TEXT_START, 1); pt[idx] = root[idx];

  int tid = get_avail_tid(false, pt);
  if(tid < 0) {
    return -ENOMEM;
  }

  // todo cleanup is not quite right here for failure

  tinfo->state = STATE_READY;
  tinfo->tid = tid;
  tinfo->is_thread = false;
  return tid;
}

int syscall_clone(unsigned long flags, uintptr_t newsp,
              int *parent_tidptr, uintptr_t tls, int *child_tidptr,
              uintptr_t gp /* needed for switching */ ) {
  int i, ret;

  spin_lock(&threadinfo_lock);
  for(i = 0; i < NUM_THREAD_SLOTS; i++) {
    if(threadinfo[i].state == STATE_EMPTY) {

      if(flags & CLONE_THREAD) {
        // This is a thread
        ret = initialize_thread(&threadinfo[i], flags, newsp, parent_tidptr,
                                tls, child_tidptr, gp);
      } else {
        // This is a "process"
        ret = initialize_process(&threadinfo[i]);
      }

      break;
    }
  }
  spin_unlock(&threadinfo_lock);

  if(i == NUM_THREAD_SLOTS){
    // no free thread slots
    return -ENOMEM;
  } else {
    return ret;
  }
}

int syscall_set_tid_address(int *tidptr_t) {
  int i, ret = -EINVAL, tid = get_current_tid();

  spin_lock(&threadinfo_lock);
  for(i = 0; i < NUM_THREAD_SLOTS; i++) {
    if(threadinfo[i].state == STATE_RUNNING &&
        threadinfo[i].tid == tid) {
      // This is the desired entry
      threadinfo[i].clear_child_tid = (uintptr_t) tidptr_t;
      ret = 0;
    }
  }

  spin_unlock(&threadinfo_lock);
  return ret;
}

int cleanup_thread(struct threadinfo_t *tinfo) {
  int ret = 0;
  // We don't free the stack here, since libc will actually
  // transparently reuse it for us in new threads. If libc
  // decides the stack needs to go, it'll call munmap
  if(tinfo->clear_child_tid) {
    // Use ret as a source value since we conveniently just set it to 0
    copy_to_user((void *) tinfo->clear_child_tid, &ret, sizeof(int));
    tinfo->clear_child_tid = 0;
  }

  return ret;
}

int cleanup_process(struct threadinfo_t *tinfo) {
  // Go ahead and unmap the userspace component
  unmap_with_any_page_table(tinfo->mapped_addr, tinfo->mapped_size);

#ifdef USE_CALLEE_PROFILE
  // Also unmap the shared region
  unmap_with_any_page_table((uintptr_t) tinfo->sm, tinfo->sm_size);
#endif

  // Grab the root page table so we can free it later
  pte *root = get_current_root();

  // Context switch back to the main thread
  context_switch_to(MAIN_PROCESS, NULL);

  // Now, tear down the root page table. This gets rid of all
  // virtual memory mappings and their backing pages, so we
  // don't need to worry about also tearing down the stack etc
  unmap_all_except_eyrie(root);
  return 0;
}

int free_thread_entry(bool exit_proc, uint64_t arg) {
  int i, j, ret = -1, tid = get_current_tid(), parent_tid;

  spin_lock(&threadinfo_lock);
  for(i = 0; i < NUM_THREAD_SLOTS; i++) {
    if(threadinfo[i].state == STATE_RUNNING &&
        threadinfo[i].tid == tid) {

      if(threadinfo[i].is_thread) {
        if(exit_proc) {
          parent_tid = get_parent_tid();
          for(j = 0; j < NUM_THREAD_SLOTS; j++) {
            if(threadinfo[j].tid == parent_tid) {
#ifdef USE_CALLEE_PROFILE
              // Save the provided samples
              struct sharedmem_info *sm = threadinfo[i].sm;
              int idx;
              ALLOW_USER_ACCESS(idx = sm->count % SAMPLE_BUFFER_ELEMS);
              ALLOW_USER_ACCESS(sm->samples[0][idx] = (uint32_t) callee_end - callee_start)
              ALLOW_USER_ACCESS(sm->samples[1][idx] = (uint32_t) arg)
#endif
              ret = cleanup_process(&threadinfo[j]);

              threadinfo[j].state = STATE_EMPTY;
              free_tid(threadinfo[j].tid);
              threadinfo[j].tid = 0;
              threadinfo[j].is_thread = 0;
              break;
              // todo we probably also want to find other child threads here...
            }
          }
        } else {
          ret = cleanup_thread(&threadinfo[i]);
        }
      } else {
        // Main process called exit. Don't currently support
        // orphan threads
        assert(exit_proc);

#ifdef USE_CALLEE_PROFILE
        // Save the provided samples
        struct sharedmem_info *sm = threadinfo[i].sm;
        int idx;
        ALLOW_USER_ACCESS(idx = sm->count % SAMPLE_BUFFER_ELEMS);
        ALLOW_USER_ACCESS(sm->samples[0][idx] = (uint32_t) callee_end - callee_start)
        ALLOW_USER_ACCESS(sm->samples[1][idx] = (uint32_t) arg)
#endif
        ret = cleanup_process(&threadinfo[i]);
      }

      threadinfo[i].state = STATE_EMPTY;
      free_tid(threadinfo[i].tid);
      threadinfo[i].tid = 0;
      threadinfo[i].is_thread = 0;
      break;
    }
  }

  spin_unlock(&threadinfo_lock);
  return ret;
}

#endif // USE_CALLEE
