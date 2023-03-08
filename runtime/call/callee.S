#include <asm/csr.h>
#include "util/asm_helpers.h"

#ifdef USE_CALLEE
.text

// Here, the only registers of value are a0 through a7 -- we can
// clobber everything else.
encl_call_handler:
    .global encl_call_handler
#ifdef USE_CALLEE_PROFILE
    la t3, callee_start
    rdcycle t4
    STORE t4, 0(t3)
#endif
    mv t3, a0
    la a0, encl_call_lock
    beqz a0, .L_failed_ret

    jal spin_lock
    mv a0, t3

    // We can now claim the callee stack
    la sp, callee_stack_end
    addi sp, sp, -ENCL_CONTEXT_SIZE

    // Store registers
    STORE a0, 10*REGBYTES(sp)
    STORE a1, 11*REGBYTES(sp)
    STORE a2, 12*REGBYTES(sp)
    STORE a3, 13*REGBYTES(sp)
    STORE a4, 14*REGBYTES(sp)
    STORE a5, 15*REGBYTES(sp)
    STORE a6, 16*REGBYTES(sp)
    STORE a7, 17*REGBYTES(sp)
    STORE t4, 29*REGBYTES(sp)

    mv a0, sp
    jal encl_call_handler_c
    bltz a0, .L_failed_unlock

    // We don't need to pop any other registers here,
    // everything necessary has been copied to userspace
    LOAD a0, (sp)
    csrw sepc, a0
    mv a0, x0

    LOAD gp, 3*REGBYTES(sp)
    LOAD tp, 4*REGBYTES(sp)
    LOAD sp, 2*REGBYTES(sp)

    // Return to glibc
#ifdef USE_CALLEE_PROFILE
    la t3, callee_end
    rdcycle t4
    STORE t4, 0(t3)
    mv t4, x0
    mv t3, x0
#endif
    sret

// Unlock and return to SM
.L_failed_unlock:
    la a0, encl_call_lock
    jal spin_unlock
.L_failed_ret:
    addi a0, x0, -1
    j sbi_ret_enclave
#endif // USE_CALLEE