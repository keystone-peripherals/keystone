#ifdef USE_CALLEE
#include "call/callee.h"
#endif

#include "mm/vm_defs.h"
#include "sys/thread.h"

OUTPUT_ARCH( "riscv" )

SECTIONS
{
  . = EYRIE_TEXT_START;
  PROVIDE(rt_base = .);
  .text : {
    *(.text._start)
    *(.text.encl_trap_handler)
    *(.text)
  }
  . = ALIGN(RISCV_PAGE_SIZE);
  .rodata :
  {
    *(.rdata)
    *(.rodata)
  }
  .data : { *(.data) }
  .bss : { *(.bss) }
  . = ALIGN(RISCV_PAGE_SIZE);
  .kernel_stacks : {
    PROVIDE(kernel_stacks_start = .);
    . += MAX_THREADS * STACK_PER_THREAD;
    PROVIDE(kernel_stacks_end = .);
  }

#ifdef USE_DRIVERS
  .drivers : {
    PROVIDE(drivers_start = .);
    *(.drivers)
    PROVIDE(drivers_end = .);
  }
#endif

#ifdef USE_CALLEE
  .callee_stack : {
    . += 0x1000;
    PROVIDE(callee_stack_end = .);
  }
#endif // USE_CALLEE

  _end = .;
  ASSERT(_end < EYRIE_MISC_START, "Runtime too big")
}
