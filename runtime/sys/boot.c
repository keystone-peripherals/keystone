#include <asm/csr.h>

#include "sys/boot.h"
#include "util/printf.h"
#include "util/rt_util.h"
#include "sys/interrupt.h"
#include "call/syscall.h"
#include "mm/vm.h"
#include "util/string.h"
#include "call/sbi.h"
#include "mm/freemem.h"
#include "mm/mm.h"
#include "sys/env.h"
#include "mm/paging.h"
#include "sys/thread.h"
#include "call/linux_wrap.h"

#ifdef USE_DRIVERS
#include "drivers/drivers.h"
#endif

#ifdef USE_CALLEE
#include "call/callee.h"
#endif

/* defined in vm.h */
extern uintptr_t shared_buffer;
extern uintptr_t shared_buffer_size;


/* defined in entry.S */
extern void* encl_trap_handler;

struct runtime_misc_params_t misc_params;
#ifdef USE_LINUX_SYSCALL
extern uint64_t initial_time_since_unix_epoch_s;
#endif

#ifdef USE_FREEMEM


/* map entire enclave physical memory so that
 * we can access the old page table and free memory */
/* remap runtime kernel to a new root page table */
void
map_physical_memory(uintptr_t dram_base,
                    uintptr_t dram_size)
{
  uintptr_t ptr = EYRIE_LOAD_START;
  /* load address should not override kernel address */
  assert(RISCV_GET_PT_INDEX(ptr, 1) != RISCV_GET_PT_INDEX(runtime_va_start, 1));
  map_with_reserved_page_table(dram_base, dram_size,
      ptr, load_l2_page_table, load_l3_page_table);
}

void
remap_kernel_space(uintptr_t runtime_base,
                   uintptr_t runtime_size)
{
  /* eyrie runtime is supposed to be smaller than a megapage */

  #if __riscv_xlen == 64
  assert(runtime_size <= RISCV_GET_LVL_PGSIZE(2));
  #elif __riscv_xlen == 32
  assert(runtime_size <= RISCV_GET_LVL_PGSIZE(1));
  #endif 

  map_with_reserved_page_table(runtime_base, runtime_size,
     runtime_va_start, kernel_l2_page_table, kernel_l3_page_table);
}

void
copy_root_page_table()
{
  /* the old table lives in the first page */
  pte* old_root_page_table = (pte*) EYRIE_LOAD_START,
      *root_page_table = get_current_root();
  int i;

  /* copy all valid entries of the old root page table */
  for (i = 0; i < BIT(RISCV_PT_INDEX_BITS); i++) {
    if (old_root_page_table[i] & PTE_V &&
        !(root_page_table[i] & PTE_V)) {
      root_page_table[i] = old_root_page_table[i];
    }
  }
}

/* initialize free memory with a simple page allocator*/
void
init_freemem()
{
  spa_init(freemem_va_start, freemem_size);
}

#endif // USE_FREEMEM

/* initialize user stack */
void *
init_user_stack_and_env(ELF(Ehdr) *hdr)
{
  void* user_sp = (void*) EYRIE_USER_STACK_START;

#ifdef USE_FREEMEM
  size_t count;
  uintptr_t stack_end = EYRIE_USER_STACK_END;
  size_t stack_count = EYRIE_USER_STACK_SIZE >> RISCV_PAGE_BITS;


  // allocated stack pages right below the runtime
  count = alloc_pages(vpn(stack_end), stack_count,
      PTE_R | PTE_W | PTE_D | PTE_A | PTE_U);

  assert(count == stack_count);

#endif // USE_FREEMEM

  // setup user stack env/aux
  return setup_start(user_sp, hdr);
}

struct eyrie_boot_args {
      uintptr_t dummy; // $a0 contains the return value from the SBI
      uintptr_t dram_base;
      uintptr_t dram_size;
      uintptr_t runtime_paddr;
      uintptr_t user_paddr;
      uintptr_t free_paddr;
      uintptr_t utm_vaddr;
      uintptr_t utm_size;
};

void *
eyrie_boot(struct eyrie_boot_args *args)
{
  void *user_sp;

  /* set initial values */
  load_pa_start = args->dram_base;
  shared_buffer = args->utm_vaddr;
  shared_buffer_size = args->utm_size;
  runtime_va_start = (uintptr_t) &rt_base;
  kernel_offset = runtime_va_start - args->runtime_paddr;

  debug("UTM : 0x%lx-0x%lx (%u KB)", args->utm_vaddr, args->utm_vaddr+args->utm_size, args->utm_size/1024);
  debug("DRAM: 0x%lx-0x%lx (%u KB)", args->dram_base, args->dram_base + args->dram_size, args->dram_size/1024);

  /* basic system init */
  thread_init();

#ifdef USE_FREEMEM
  freemem_va_start = __va(args->free_paddr);
  freemem_size = args->dram_base + args->dram_size - args->free_paddr;

  debug("FREE: 0x%lx-0x%lx (%u KB), va 0x%lx", args->free_paddr, args->dram_base + args->dram_size, freemem_size/1024, freemem_va_start);

  /* remap kernel VA */
  remap_kernel_space(args->runtime_paddr, args->user_paddr - args->runtime_paddr);
  map_physical_memory(args->dram_base, args->dram_size);

  /* switch to the new page table */
  csr_write(satp, satp_new(kernel_va_to_pa(get_current_root())));
  tlb_flush();

  /* copy valid entries from the old page table */
  copy_root_page_table();

  /* initialize free memory */
  init_freemem();

  //TODO: This should be set by walking the userspace vm and finding
  //highest used addr. Instead we start partway through the anon space
  set_program_break(0x0000003000000000);

  #ifdef USE_PAGING
  init_paging(args->user_paddr, args->free_paddr);
  #endif /* USE_PAGING */
#endif /* USE_FREEMEM */

  /* initialize user stack */
  user_sp = init_user_stack_and_env((ELF(Ehdr) *) __va(args->user_paddr));

  /* set trap vector */
  csr_write(stvec, &encl_trap_handler);

  /* prepare edge & system calls */
  init_edge_internals();

  /* set timer */
  init_timer();
  // get misc_params
  struct runtime_misc_params_t* misc_params_phys_addr = (struct runtime_misc_params_t*)translate((uintptr_t)&misc_params);
  sbi_get_misc_params(misc_params_phys_addr);
#ifdef USE_LINUX_SYSCALL
  initial_time_since_unix_epoch_s = misc_params.time_since_unix_epoch_s;
  // print_strace("!!! Value of passed Unix Time is: %ld\n", misc_params.time_since_unix_epoch_s);
#endif

#ifdef USE_DRIVERS
  /* initialize any drivers included in the .drivers section */
  init_driver_subsystem();
#endif

  /* Enable the FPU */
  csr_write(sstatus, csr_read(sstatus) | 0x6000);

#ifdef USE_CALLEE
  /* This should happen late, so that the values of any CSRs are correctly
   * backed up in the SM, and used for context switches */
  callee_init();
#endif

  debug("eyrie boot finished. drop to the user land ...");
  /* booting all finished, droping to the user land */
  return user_sp;
}
