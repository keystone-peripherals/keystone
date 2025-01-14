//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#include "device.h"
#include "enclave.h"
#include "mprv.h"
#include "pmp.h"
#include "page.h"
#include "cpu.h"
#include "platform-hook.h"
#include "sm_assert.h"
#include <sbi/sbi_string.h>
#include <sbi/riscv_asm.h>
#include <sbi/riscv_locks.h>
#include <sbi/sbi_console.h>

struct enclave enclaves[ENCL_MAX];

// Enclave IDs are unsigned ints, so we do not need to check if eid is
// greater than or equal to 0
#define ENCLAVE_EXISTS(eid) (eid < ENCL_MAX && enclaves[eid].state >= 0)

static spinlock_t encl_lock = SPIN_LOCK_INITIALIZER;

extern void save_host_regs(void);
extern void restore_host_regs(void);
extern byte dev_public_key[PUBLIC_KEY_SIZE];

/****************************
 *
 * Enclave utility functions
 * Internal use by SBI calls
 *
 ****************************/

/* Internal function containing the core of the context switching
 * code to the enclave.
 *
 * Used by resume_enclave and run_enclave.
 *
 * Expects that eid has already been valided, and it is OK to run this enclave
*/
static inline void context_switch_to_enclave(struct sbi_trap_regs* regs,
                                                enclave_id eid,
                                                int load_parameters){
  enclave_id called_eid;
  struct enclave_call *deepest_call;

  /* save host context */
  cpu_enter_enclave_context(eid, regs);

  /* restore thread context */
  pop_prev_state(&enclaves[eid].threads[0], regs);
  pop_prev_mepc(&enclaves[eid].threads[0], regs);
  pop_prev_mstatus(&enclaves[eid].threads[0], regs);

  uintptr_t interrupts = 0;
  csr_write(mideleg, interrupts);

  if(load_parameters) {
    // passing parameters for a first run
    csr_write(sepc, (uintptr_t) enclaves[eid].params.user_entry);
    regs->mepc = (uintptr_t) enclaves[eid].params.runtime_entry - 4; // regs->mepc will be +4 before sbi_ecall_handler return
    regs->mstatus = (1 << MSTATUS_MPP_SHIFT);
    // $a1: (PA) DRAM base,
    regs->a1 = (uintptr_t) enclaves[eid].pa_params.dram_base;
    // $a2: (PA) DRAM size,
    regs->a2 = (uintptr_t) enclaves[eid].pa_params.dram_size;
    // $a3: (PA) kernel location,
    regs->a3 = (uintptr_t) enclaves[eid].pa_params.runtime_base;
    // $a4: (PA) user location,
    regs->a4 = (uintptr_t) enclaves[eid].pa_params.user_base;
    // $a5: (PA) freemem location,
    regs->a5 = (uintptr_t) enclaves[eid].pa_params.free_base;
    // $a6: (VA) utm base,
    regs->a6 = (uintptr_t) enclaves[eid].params.untrusted_ptr;
    // $a7: (size_t) utm size
    regs->a7 = (uintptr_t) enclaves[eid].params.untrusted_size;

    // switch to the initial enclave page table
    csr_write(satp, enclaves[eid].encl_satp);
  }

  switch_vector_enclave();

  // set PMP based on deepest call stack
  deepest_call = &enclaves[eid].call_stack[0][enclaves[eid].call_depth[0] - 1];
  called_eid = deepest_call->to_encl;

  osm_pmp_set(PMP_NO_PERM);
  int memid;
  for(memid=0; memid < ENCLAVE_REGIONS_MAX; memid++) {
    if(enclaves[called_eid].regions[memid].type != REGION_INVALID) {
      pmp_set_keystone(enclaves[called_eid].regions[memid].pmp_rid, PMP_ALL_PERM);
    }

    // Also enable any caller-exported regions
//    if(eid != called_eid) {
//      if(enclaves[eid].regions[memid].type == REGION_EXPORTED) {
//        pmp_set_keystone(enclaves[eid].regions[memid].pmp_rid, PMP_ALL_PERM);
//      }
//    }
  }

  // ensure nonsecure-only devices are unmapped
  device_switch_to_enclave();

  // Setup any platform specific defenses
  platform_switch_to_enclave(&(enclaves[eid]));
}

static inline void context_switch_to_host(struct sbi_trap_regs *regs,
    enclave_id eid,
    int return_on_resume){

  enclave_id called_eid;
  struct enclave_call *deepest_call;

  // ensure nonsecure-only devices are remapped
  device_switch_to_host();

  // set PMP
  deepest_call = &enclaves[eid].call_stack[0][enclaves[eid].call_depth[0] - 1];
  called_eid = deepest_call->to_encl;

  int memid;
  for(memid=0; memid < ENCLAVE_REGIONS_MAX; memid++) {
    // Disable callee enclave's PMP regions
    if(enclaves[called_eid].regions[memid].type != REGION_INVALID) {
      pmp_set_keystone(enclaves[called_eid].regions[memid].pmp_rid, PMP_NO_PERM);
    }

    // Also disable any caller-exported regions
//    if(eid != called_eid) {
//      if(enclaves[eid].regions[memid].type == REGION_EXPORTED) {
//        pmp_set_keystone(enclaves[eid].regions[memid].pmp_rid, PMP_NO_PERM);
//      }
//    }
  }
  osm_pmp_set(PMP_ALL_PERM);

  uintptr_t interrupts = MIP_SSIP | MIP_STIP | MIP_SEIP;
  csr_write(mideleg, interrupts);

  /* restore host context */
  stash_prev_state(&enclaves[eid].threads[0], regs, return_on_resume);
  stash_prev_mepc(&enclaves[eid].threads[0], regs);
  stash_prev_mstatus(&enclaves[eid].threads[0], regs);
  cpu_exit_enclave_context(regs);

  switch_vector_host();

  uintptr_t pending = csr_read(mip);

  if (pending & MIP_MTIP) {
    csr_clear(mip, MIP_MTIP);
    csr_set(mip, MIP_STIP);
  }
  if (pending & MIP_MSIP) {
    csr_clear(mip, MIP_MSIP);
    csr_set(mip, MIP_SSIP);
  }
  if (pending & MIP_MEIP) {
    csr_clear(mip, MIP_MEIP);
    csr_set(mip, MIP_SEIP);
  }

  // Reconfigure platform specific defenses
  platform_switch_from_enclave(&(enclaves[eid]));
  return;
}


// TODO: This function is externally used.
// refactoring needed
/*
 * Init all metadata as needed for keeping track of enclaves
 * Called once by the SM on startup
 */
void enclave_init_metadata(void){
  enclave_id eid;
  int i=0;

  /* Assumes eids are incrementing values, which they are for now */
  for(eid=0; eid < ENCL_MAX; eid++){
    enclaves[eid].state = INVALID;

    // Clear out regions
    for(i=0; i < ENCLAVE_REGIONS_MAX; i++){
      enclaves[eid].regions[i].type = REGION_INVALID;
    }
    /* Fire all platform specific init for each enclave */
    platform_init_enclave(&(enclaves[eid]));
  }

}

static unsigned long clean_enclave_memory(uintptr_t utbase, uintptr_t utsize)
{

  // This function is quite temporary. See issue #38

  // Zero out the untrusted memory region, since it may be in
  // indeterminate state.
  sbi_memset((void*)utbase, 0, utsize);

  return SBI_ERR_SM_ENCLAVE_SUCCESS;
}

static unsigned long encl_alloc_eid(enclave_id* _eid)
{
  enclave_id eid;

  spin_lock(&encl_lock);

  for(eid=0; eid<ENCL_MAX; eid++)
  {
    if(enclaves[eid].state == INVALID){
      break;
    }
  }
  if(eid != ENCL_MAX)
    enclaves[eid].state = ALLOCATED;

  spin_unlock(&encl_lock);

  if(eid != ENCL_MAX){
    *_eid = eid;
    return SBI_ERR_SM_ENCLAVE_SUCCESS;
  }
  else{
    return SBI_ERR_SM_ENCLAVE_NO_FREE_RESOURCE;
  }
}

static unsigned long encl_free_eid(enclave_id eid)
{
  spin_lock(&encl_lock);
  enclaves[eid].state = INVALID;
  spin_unlock(&encl_lock);
  return SBI_ERR_SM_ENCLAVE_SUCCESS;
}

int get_enclave_region_index(enclave_id eid, enum enclave_region_type type){
  size_t i;
  for(i = 0;i < ENCLAVE_REGIONS_MAX; i++){
    if(enclaves[eid].regions[i].type == type){
      return i;
    }
  }
  // No such region for this enclave
  return -1;
}

uintptr_t get_enclave_region_size(enclave_id eid, int memid)
{
  if (0 <= memid && memid < ENCLAVE_REGIONS_MAX)
    return pmp_region_get_size(enclaves[eid].regions[memid].pmp_rid);

  return 0;
}

uintptr_t get_enclave_region_base(enclave_id eid, int memid)
{
  if (0 <= memid && memid < ENCLAVE_REGIONS_MAX)
    return pmp_region_get_addr(enclaves[eid].regions[memid].pmp_rid);

  return 0;
}

// TODO: This function is externally used by sm-sbi.c.
// Change it to be internal (remove from the enclave.h and make static)
/* Internal function enforcing a copy source is from the untrusted world.
 * Does NOT do verification of dest, assumes caller knows what that is.
 * Dest should be inside the SM memory.
 */
unsigned long copy_enclave_create_args(uintptr_t src, struct keystone_sbi_create_t* dest){

  int region_overlap = copy_to_sm(dest, src, sizeof(struct keystone_sbi_create_t));

  if (region_overlap)
    return SBI_ERR_SM_ENCLAVE_REGION_OVERLAPS;
  else
    return SBI_ERR_SM_ENCLAVE_SUCCESS;
}

/* copies data from enclave, source must be inside EPM */
static unsigned long copy_enclave_data(struct enclave* enclave,
                                          void* dest, uintptr_t source, size_t size) {

  int illegal = copy_to_sm(dest, source, size);

  if(illegal)
    return SBI_ERR_SM_ENCLAVE_ILLEGAL_ARGUMENT;
  else
    return SBI_ERR_SM_ENCLAVE_SUCCESS;
}

/* copies data into enclave, destination must be inside EPM */
static unsigned long copy_enclave_report(struct enclave* enclave,
                                            uintptr_t dest, struct report* source) {

  int illegal = copy_from_sm(dest, source, sizeof(struct report));

  if(illegal)
    return SBI_ERR_SM_ENCLAVE_ILLEGAL_ARGUMENT;
  else
    return SBI_ERR_SM_ENCLAVE_SUCCESS;
}

static int is_create_args_valid(struct keystone_sbi_create_t* args)
{
  uintptr_t epm_start, epm_end;

  /* printm("[create args info]: \r\n\tepm_addr: %llx\r\n\tepmsize: %llx\r\n\tutm_addr: %llx\r\n\tutmsize: %llx\r\n\truntime_addr: %llx\r\n\tuser_addr: %llx\r\n\tfree_addr: %llx\r\n", */
  /*        args->epm_region.paddr, */
  /*        args->epm_region.size, */
  /*        args->utm_region.paddr, */
  /*        args->utm_region.size, */
  /*        args->runtime_paddr, */
  /*        args->user_paddr, */
  /*        args->free_paddr); */

  // check if physical addresses are valid
  if (args->epm_region.size <= 0)
    return 0;

  // check if overflow
  if (args->epm_region.paddr >=
      args->epm_region.paddr + args->epm_region.size)
    return 0;
  if (args->utm_region.paddr >=
      args->utm_region.paddr + args->utm_region.size)
    return 0;

  epm_start = args->epm_region.paddr;
  epm_end = args->epm_region.paddr + args->epm_region.size;

  // check if physical addresses are in the range
  if (args->runtime_paddr < epm_start ||
      args->runtime_paddr >= epm_end)
    return 0;
  if (args->user_paddr < epm_start ||
      args->user_paddr >= epm_end)
    return 0;
  if (args->free_paddr < epm_start ||
      args->free_paddr > epm_end)
      // note: free_paddr == epm_end if there's no free memory
    return 0;

  // check the order of physical addresses
  if (args->runtime_paddr > args->user_paddr)
    return 0;
  if (args->user_paddr > args->free_paddr)
    return 0;

  return 1;
}

/*********************************
 *
 * Enclave SBI functions
 * These are exposed to S-mode via the sm-sbi interface
 *
 *********************************/


/* This handles creation of a new enclave, based on arguments provided
 * by the untrusted host.
 *
 * This may fail if: it cannot allocate PMP regions, EIDs, etc
 */
unsigned long create_enclave(unsigned long *eidptr, struct keystone_sbi_create_t create_args)
{
  /* EPM and UTM parameters */
  uintptr_t base = create_args.epm_region.paddr;
  size_t size = create_args.epm_region.size;
  uintptr_t utbase = create_args.utm_region.paddr;
  size_t utsize = create_args.utm_region.size;

  enclave_id eid;
  unsigned long ret;
  int region, shared_region, i, j;

  /* Runtime parameters */
  if(!is_create_args_valid(&create_args))
    return SBI_ERR_SM_ENCLAVE_ILLEGAL_ARGUMENT;

  /* set va params */
  struct runtime_va_params_t params = create_args.params;
  struct runtime_pa_params_t pa_params;
  pa_params.dram_base = base;
  pa_params.dram_size = size;
  pa_params.runtime_base = create_args.runtime_paddr;
  pa_params.user_base = create_args.user_paddr;
  pa_params.free_base = create_args.free_paddr;

  struct runtime_misc_params_t misc_params = create_args.misc_params;

  // allocate eid
  ret = SBI_ERR_SM_ENCLAVE_NO_FREE_RESOURCE;
  if (encl_alloc_eid(&eid) != SBI_ERR_SM_ENCLAVE_SUCCESS)
    goto error;

  // create a PMP region bound to the enclave
  ret = SBI_ERR_SM_ENCLAVE_PMP_FAILURE;
  if(pmp_region_init_atomic(base, size, PMP_PRI_ANY, &region, 0, create_args.force_tor))
    goto free_encl_idx;

  // create PMP region for shared memory
  if(pmp_region_init_atomic(utbase, utsize, PMP_PRI_BOTTOM, &shared_region, 0, false))
    goto free_region;

  // set pmp registers for private region (not shared)
  if(pmp_set_global(region, PMP_NO_PERM))
    goto free_shared_region;

  // cleanup some memory regions for sanity See issue #38
  clean_enclave_memory(utbase, utsize);


  // initialize enclave metadata
  enclaves[eid].eid = eid;

  enclaves[eid].regions[0].pmp_rid = region;
  enclaves[eid].regions[0].type = REGION_EPM;
  enclaves[eid].regions[1].pmp_rid = shared_region;
  enclaves[eid].regions[1].type = REGION_UTM;
#if __riscv_xlen == 32
  enclaves[eid].encl_satp = ((base >> RISCV_PGSHIFT) | (SATP_MODE_SV32 << HGATP_MODE_SHIFT));
#else
  enclaves[eid].encl_satp = ((base >> RISCV_PGSHIFT) | (SATP_MODE_SV39 << HGATP_MODE_SHIFT));
#endif
  enclaves[eid].n_thread = 0;
  enclaves[eid].params = params;
  enclaves[eid].pa_params = pa_params;
  enclaves[eid].misc_params = misc_params;

  /* Init callable state */
  enclaves[eid].handler = 0;
  for(i = 0; i < MAX_ENCL_THREADS; i++) {
    /* Init enclave state (regs etc) */
    clean_state(&enclaves[eid].threads[i]);
    enclaves[eid].call_depth[i] = 0;
    for(j = 0; j < ENCLAVE_CALLS_MAX; j++) {
        enclaves[eid].call_stack[i][j].source = CALL_NONE;
    }
  }

  /* Platform create happens as the last thing before hashing/etc since
     it may modify the enclave struct */
  ret = platform_create_enclave(&enclaves[eid]);
  if (ret)
    goto unset_region;

  /* Validate memory, prepare hash and signature for attestation */
  spin_lock(&encl_lock); // FIXME This should error for second enter.
  ret = validate_and_hash_enclave(&enclaves[eid]);
  /* The enclave is fresh if it has been validated and hashed but not run yet. */
  if (ret)
    goto unlock;

  enclaves[eid].state = FRESH;
  /* EIDs are unsigned int in size, copy via simple copy */
  *eidptr = eid;

  spin_unlock(&encl_lock);
  return SBI_ERR_SM_ENCLAVE_SUCCESS;

unlock:
  spin_unlock(&encl_lock);
// free_platform:
  platform_destroy_enclave(&enclaves[eid]);
unset_region:
  pmp_unset_global(region);
free_shared_region:
  pmp_region_free_atomic(shared_region);
free_region:
  pmp_region_free_atomic(region);
free_encl_idx:
  encl_free_eid(eid);
error:
  return ret;
}

/*
 * Fully destroys an enclave
 * Deallocates EID, clears epm, etc
 * Fails only if the enclave isn't running.
 */
unsigned long destroy_enclave(enclave_id eid)
{
  int destroyable;

  spin_lock(&encl_lock);
  destroyable = (ENCLAVE_EXISTS(eid)
                 && enclaves[eid].state <= STOPPED);
  /* update the enclave state first so that
   * no SM can run the enclave any longer */
  if(destroyable)
    enclaves[eid].state = DESTROYING;
  spin_unlock(&encl_lock);

  if(!destroyable)
    return SBI_ERR_SM_ENCLAVE_NOT_DESTROYABLE;


  // 0. Let the platform specifics do cleanup/modifications
  platform_destroy_enclave(&enclaves[eid]);


  // 1. clear all the data in the enclave pages
  // requires no lock (single runner)
  int i;
  void* base;
  size_t size;
  region_id rid;
  for(i = 0; i < ENCLAVE_REGIONS_MAX; i++){
    if(enclaves[eid].regions[i].type == REGION_INVALID ||
       enclaves[eid].regions[i].type == REGION_UTM) {
      continue;
    }

    rid = enclaves[eid].regions[i].pmp_rid;
    if(enclaves[eid].regions[i].type == REGION_EPM) {
      //1.a Clear all pages
      base = (void*) pmp_region_get_addr(rid);
      size = (size_t) pmp_region_get_size(rid);
      sbi_memset((void*) base, 0, size);

      //1.b free pmp region
      pmp_unset_global(rid);
      pmp_region_free_atomic(rid);
    } else if (enclaves[eid].regions[i].type == REGION_MMIO) {
      // todo: reset the hardware device so that state is not leaked
      // for now, just release the device
      sm_release_secure_device(device_name_from_region(enclaves[eid].regions[i].pmp_rid));
    }
  }

  // 2. free pmp region for UTM
  rid = get_enclave_region_index(eid, REGION_UTM);
  if(rid != -1)
    pmp_region_free_atomic(enclaves[eid].regions[rid].pmp_rid);

  enclaves[eid].encl_satp = 0;
  enclaves[eid].n_thread = 0;
  enclaves[eid].params = (struct runtime_va_params_t) {0};
  enclaves[eid].pa_params = (struct runtime_pa_params_t) {0};
  for(i=0; i < ENCLAVE_REGIONS_MAX; i++){
    enclaves[eid].regions[i].type = REGION_INVALID;
  }

  // 3. release eid
  encl_free_eid(eid);

  return SBI_ERR_SM_ENCLAVE_SUCCESS;
}


unsigned long run_enclave(struct sbi_trap_regs *regs, enclave_id eid)
{
  int runable;

  spin_lock(&encl_lock);
  runable = (ENCLAVE_EXISTS(eid)
            && enclaves[eid].state == FRESH);
  if(runable) {
    enclaves[eid].state = RUNNING;
    enclaves[eid].n_thread++;
  }
  spin_unlock(&encl_lock);

  if(!runable) {
    return SBI_ERR_SM_ENCLAVE_NOT_FRESH;
  }

  // Call into this enclave from the host
  enclaves[eid].call_stack[0][0].to_encl = eid;
  enclaves[eid].call_stack[0][0].source = CALL_HOST;
  enclaves[eid].call_depth[0]++;

  // Enclave is OK to run, context switch to it
  context_switch_to_enclave(regs, eid, 1);

  return SBI_ERR_SM_ENCLAVE_SUCCESS;
}

unsigned long exit_enclave(struct sbi_trap_regs *regs, enclave_id eid)
{
  int exitable;

  spin_lock(&encl_lock);
  exitable = enclaves[eid].state == RUNNING;
  if (exitable) {
    enclaves[eid].n_thread--;
    if(enclaves[eid].n_thread == 0)
      enclaves[eid].state = STOPPED;
  }
  spin_unlock(&encl_lock);

  if(!exitable)
    return SBI_ERR_SM_ENCLAVE_NOT_RUNNING;

  context_switch_to_host(regs, eid, 0);

  return SBI_ERR_SM_ENCLAVE_SUCCESS;
}

unsigned long stop_enclave(struct sbi_trap_regs *regs, uint64_t request, enclave_id eid)
{
  int stoppable;

  spin_lock(&encl_lock);
  stoppable = enclaves[eid].state == RUNNING;
  if (stoppable) {
    enclaves[eid].n_thread--;
    if(enclaves[eid].n_thread == 0)
      enclaves[eid].state = STOPPED;
  }
  spin_unlock(&encl_lock);

  if(!stoppable)
    return SBI_ERR_SM_ENCLAVE_NOT_RUNNING;

  context_switch_to_host(regs, eid, request == STOP_EDGE_CALL_HOST);

  switch(request) {
    case(STOP_TIMER_INTERRUPT):
      return SBI_ERR_SM_ENCLAVE_INTERRUPTED;
    case(STOP_EDGE_CALL_HOST):
      return SBI_ERR_SM_ENCLAVE_EDGE_CALL_HOST;
    case(STOP_YIELD_ENCLAVE):
      return SBI_ERR_SM_ENCLAVE_YIELDED;
    default:
      return SBI_ERR_SM_ENCLAVE_UNKNOWN_ERROR;
  }
}

unsigned long resume_enclave(struct sbi_trap_regs *regs, enclave_id eid)
{
  int resumable;

  spin_lock(&encl_lock);
  resumable = (ENCLAVE_EXISTS(eid)
               && (enclaves[eid].state == RUNNING || enclaves[eid].state == STOPPED)
               && enclaves[eid].n_thread < MAX_ENCL_THREADS);

  if(!resumable) {
    spin_unlock(&encl_lock);
    return SBI_ERR_SM_ENCLAVE_NOT_RESUMABLE;
  } else {
    enclaves[eid].n_thread++;
    enclaves[eid].state = RUNNING;
  }
  spin_unlock(&encl_lock);

  // Enclave is OK to resume, context switch to it
  context_switch_to_enclave(regs, eid, 0);

  return SBI_ERR_SM_ENCLAVE_SUCCESS;
}

unsigned long attest_enclave(uintptr_t report_ptr, uintptr_t data, uintptr_t size, enclave_id eid)
{
  int attestable;
  struct report report;
  int ret;

  if (size > ATTEST_DATA_MAXLEN)
    return SBI_ERR_SM_ENCLAVE_ILLEGAL_ARGUMENT;

  spin_lock(&encl_lock);
  attestable = (ENCLAVE_EXISTS(eid)
                && (enclaves[eid].state >= FRESH));

  if(!attestable) {
    ret = SBI_ERR_SM_ENCLAVE_NOT_INITIALIZED;
    goto err_unlock;
  }

  /* copy data to be signed */
  ret = copy_enclave_data(&enclaves[eid], report.enclave.data,
      data, size);
  report.enclave.data_len = size;

  if (ret) {
    ret = SBI_ERR_SM_ENCLAVE_NOT_ACCESSIBLE;
    goto err_unlock;
  }

  spin_unlock(&encl_lock); // Don't need to wait while signing, which might take some time

  sbi_memcpy(report.dev_public_key, dev_public_key, PUBLIC_KEY_SIZE);
  sbi_memcpy(report.sm.hash, sm_hash, MDSIZE);
  sbi_memcpy(report.sm.public_key, sm_public_key, PUBLIC_KEY_SIZE);
  sbi_memcpy(report.sm.signature, sm_signature, SIGNATURE_SIZE);
  sbi_memcpy(report.enclave.hash, enclaves[eid].hash, MDSIZE);
  sm_sign(report.enclave.signature,
      &report.enclave,
      sizeof(struct enclave_report)
      - SIGNATURE_SIZE
      - ATTEST_DATA_MAXLEN + size);

  spin_lock(&encl_lock);

  /* copy report to the enclave */
  ret = copy_enclave_report(&enclaves[eid],
      report_ptr,
      &report);

  if (ret) {
    ret = SBI_ERR_SM_ENCLAVE_ILLEGAL_ARGUMENT;
    goto err_unlock;
  }

  ret = SBI_ERR_SM_ENCLAVE_SUCCESS;

err_unlock:
  spin_unlock(&encl_lock);
  return ret;
}

unsigned long get_sealing_key(uintptr_t sealing_key, uintptr_t key_ident,
                                 size_t key_ident_size, enclave_id eid)
{
  struct sealing_key *key_struct = (struct sealing_key *)sealing_key;
  int ret;

  /* derive key */
  ret = sm_derive_sealing_key((unsigned char *)key_struct->key,
                              (const unsigned char *)key_ident, key_ident_size,
                              (const unsigned char *)enclaves[eid].hash);
  if (ret)
    return SBI_ERR_SM_ENCLAVE_UNKNOWN_ERROR;

  /* sign derived key */
  sm_sign((void *)key_struct->signature, (void *)key_struct->key,
          SEALING_KEY_SIZE);

  return SBI_ERR_SM_ENCLAVE_SUCCESS;
}

unsigned long claim_mmio(uintptr_t dev_string, enclave_id eid) {
    int i;
    region_id rid;
    bool claimed = device_is_claimed((const char *)dev_string);

    if(claimed) {
      rid = region_from_device_name((const char *) dev_string);
      if(rid < 0) {
        return rid;
      }

      for(i = 0; i < ENCLAVE_REGIONS_MAX; i++) {
        if (enclaves[eid].regions[i].type == REGION_MMIO &&
            enclaves[eid].regions[i].pmp_rid == rid) {
          // We already own this region
          return 0;
        }
      }
    } else {
      for(i = 0; i < ENCLAVE_REGIONS_MAX; i++) {
        if(enclaves[eid].regions[i].type == REGION_INVALID) {
          // Find the region now that we know we have a slot for it
          rid = sm_claim_secure_device((const char *)dev_string);
          if (rid < 0) {
            return rid;
          }

          // Register this region
          enclaves[eid].regions[i].pmp_rid = rid;
          enclaves[eid].regions[i].type = REGION_MMIO;

          // Enable it
          pmp_set_keystone(rid, PMP_ALL_PERM);
          return 0;
        }
      }
    }

    // No free regions found
    return -1;
}

unsigned long release_mmio(uintptr_t dev_string, enclave_id eid) {
    int i;
    region_id rid = region_from_device_name((const char *) dev_string);
    if(rid < 0) {
        // Device not claimed
        return rid;
    }

    for(i = 0; i < ENCLAVE_REGIONS_MAX; i++) {
        if(enclaves[eid].regions[i].type == REGION_MMIO &&
            enclaves[eid].regions[i].pmp_rid == rid) {
            // This is the correct region to deregister. First, disable it
            pmp_set_keystone(rid, PMP_NO_PERM);

            // Then deregister it
            enclaves[eid].regions[i].pmp_rid = -1;
            enclaves[eid].regions[i].type = REGION_INVALID;

            // Then release it
            sm_release_secure_device((const char *) dev_string);
            return 0;
        }
    }

    // Could not find the region to release
    return -1;
}

unsigned long call_enclave(struct sbi_trap_regs *regs, enclave_id from, enclave_id to, int type) {
  int depth, memid;
  struct enclave_call *entry;

  spin_lock(&encl_lock);

  // Check that the target enclave has a handler registered
  if(!enclaves[to].handler) {
    spin_unlock(&encl_lock);
    return SBI_ERR_SM_ENCLAVE_ILLEGAL_ARGUMENT;
  }

  depth = enclaves[from].call_depth[0];
  if(depth == ENCLAVE_CALLS_MAX) {
    spin_unlock(&encl_lock);
    return SBI_ERR_SM_ENCLAVE_NO_FREE_RESOURCE;
  }

  // Register a call in the enclave struct
  entry = &enclaves[from].call_stack[0][depth];
  entry->source = CALL_ENCLAVE;
  entry->from_encl = from;
  entry->to_encl = to;

  // Stash the existing state
  stash_prev_state(&entry->stashed_state, regs, 0);
  stash_prev_mepc(&entry->stashed_state, regs);
  stash_prev_mstatus(&entry->stashed_state, regs);

  // Apply the necessary call state
  regs->mepc = enclaves[to].handler - 4;
  pop_prev_smode_csrs(&enclaves[to].handler_csrs);
  enclaves[from].call_depth[0]++;
  spin_unlock(&encl_lock);

  // Clean the register state, and copy out the argument registers.
  sbi_memset(regs, 0, offsetof(struct sbi_trap_regs, mepc));
  regs->a0 = type;
  sbi_memcpy(&regs->a1, &entry->stashed_state.prev_state.a2, 4 * sizeof(uintptr_t));

  // Switch out PMP regions
  for(memid = 0; memid < ENCLAVE_REGIONS_MAX; memid++) {
    if(enclaves[from].regions[memid].type != REGION_INVALID &&
        enclaves[from].regions[memid].type != REGION_EXPORTED) {
      pmp_set_keystone(enclaves[from].regions[memid].pmp_rid, PMP_NO_PERM);
    }
  }

  for(memid = 0; memid < ENCLAVE_REGIONS_MAX; memid++) {
    if(enclaves[to].regions[memid].type != REGION_INVALID) {
      pmp_set_keystone(enclaves[to].regions[memid].pmp_rid, PMP_ALL_PERM);
    }

    if(enclaves[from].regions[memid].type == REGION_EXPORTED) {
      pmp_set_keystone(enclaves[from].regions[memid].pmp_rid, PMP_ALL_PERM);
    }
  }

  return SBI_ERR_SM_ENCLAVE_SUCCESS;
}

unsigned long ret_enclave(struct sbi_trap_regs *regs)
{
  int depth, memid;
  unsigned long retval;
  enclave_id from, to;
  struct enclave_call *entry;

  // Figure out which enclave we are returning to
  spin_lock(&encl_lock);
  from = cpu_get_enclave_id();
  depth = enclaves[from].call_depth[0] - 1;
  entry = &enclaves[from].call_stack[0][depth];
  sm_assert(entry->from_encl == from);

  // Pop the execution state back to expected
  retval = regs->a0;
  pop_prev_state(&entry->stashed_state, regs);
  pop_prev_mepc(&entry->stashed_state, regs);
  pop_prev_mstatus(&entry->stashed_state, regs);

  // Delete this last entry in the call stack
  to = entry->to_encl;
  sbi_memset(entry, 0, sizeof(struct enclave_call));
  enclaves[from].call_depth[0]--;
  spin_unlock(&encl_lock);

  // Switch out pmp regions
  for(memid = 0; memid < ENCLAVE_REGIONS_MAX; memid++) {
    if(enclaves[to].regions[memid].type != REGION_INVALID) {
      pmp_set_keystone(enclaves[to].regions[memid].pmp_rid, PMP_NO_PERM);
    }
  }

  for(memid = 0; memid < ENCLAVE_REGIONS_MAX; memid++) {
    if(enclaves[from].regions[memid].type != REGION_INVALID) {
      pmp_set_keystone(enclaves[from].regions[memid].pmp_rid, PMP_ALL_PERM);
    }
  }

  return retval;
}

unsigned long register_handler(uintptr_t handler, enclave_id eid)
{
  int ret = SBI_ERR_SM_ENCLAVE_NO_FREE_RESOURCE;
  spin_lock(&encl_lock);

  // Don't allow registering a duplicate handler
  if(!enclaves[eid].handler)
  {
    enclaves[eid].handler = handler;

    // Back up a copy of the smode registers. These will be used
    // as the base registers when creating new threads calling
    // this handler.
    stash_prev_smode_csrs(&enclaves[eid].handler_csrs);
    ret = SBI_ERR_SM_ENCLAVE_SUCCESS;
  }

  spin_unlock(&encl_lock);
  return ret;
}

unsigned long share_region(uintptr_t addr, size_t size, enclave_id with, enclave_id eid)
{
  bool done = false;
  int i, ret = SBI_ERR_SM_ENCLAVE_NO_FREE_RESOURCE,
         source_free = -1, target_free = -1, epm = -1;
  region_id rid;

  // Identify the EPM region, as well as a free region slot to hold
  // the new subregion to create. We'll create this shared region as
  // a PMP subregion of the EPM.

  spin_lock(&encl_lock);
  for(i = 0; i < ENCLAVE_REGIONS_MAX; i++) {
    if(enclaves[eid].regions[i].type == REGION_EPM) {
      epm = i;
    }

    if(enclaves[eid].regions[i].type == REGION_INVALID) {
      source_free = i;
    }

    if(enclaves[with].regions[i].type == REGION_INVALID) {
      target_free = i;
    }

    if(epm >= 0 && source_free >= 0 && target_free >= 0)
      break;
  }

  if(epm < 0 || source_free < 0 || target_free < 0) {
    done = true;
  }

  spin_unlock(&encl_lock);

  // Kind of a scary thing to do unlocked. Rationale: pmp_region_subregion_atomic will
  // only ever touch PMP regions owned by this current enclave that we are currently
  // working on. However, this only works with the following assumptions:
  //
  // 1. Subregions cannot themselves have further subregions
  // 2. There are no cross-enclave subregions
  //
  // The reason we have to unlock the enclave lock here is that we may get caught in
  // a situation where core A has to move a PMP region by sending an IPI, but core B
  // is currently in the middle of processing an SM call. If we are holding the enclave
  // lock here, core B will try to acquire it with interrupts disabled and will never
  // receive our synchronous IPI, and core A will stall because core B never acks the
  // synchronous IPI.

  if(!done) {
    ret = pmp_region_subregion_atomic(addr, size,
                                      enclaves[eid].regions[epm].pmp_rid, &rid);
    if(!ret) {
      spin_lock(&encl_lock);
      enclaves[with].regions[target_free].type = REGION_EXPORTED;
      enclaves[with].regions[target_free].pmp_rid = rid;
      enclaves[eid].regions[source_free].type = REGION_EXPORTED;
      enclaves[eid].regions[source_free].pmp_rid = rid;
      spin_unlock(&encl_lock);

      // Disable this new region globally
      pmp_set_global(rid, PMP_NO_PERM);
      pmp_set_keystone(rid, PMP_ALL_PERM);
      ret = SBI_ERR_SM_ENCLAVE_SUCCESS;
    }
  }

  return ret;
}

unsigned long unshare_region(uintptr_t addr, enclave_id with, enclave_id eid) {
    int i, ret = SBI_ERR_SM_ENCLAVE_ILLEGAL_ARGUMENT,
        source_idx = -1, target_idx = -1, rid = -1, tmp_rid;
    bool done = false, source_found = false, target_found = false;

    spin_lock(&encl_lock);

    // Find the region to unshare
    for(i = 0; i < ENCLAVE_REGIONS_MAX; i++) {
        if(enclaves[eid].regions[i].type == REGION_EXPORTED && !source_found) {
            tmp_rid = enclaves[eid].regions[i].pmp_rid;
            if(pmp_region_get_addr(tmp_rid) == addr) {
                source_found = true;
                source_idx = i;

                if(rid == -1) {
                    rid = tmp_rid;
                } else sm_assert(rid == tmp_rid);
            }
        }

        if(enclaves[with].regions[i].type == REGION_EXPORTED && !target_found)  {
            tmp_rid = enclaves[with].regions[i].pmp_rid;
            if(pmp_region_get_addr(tmp_rid) == addr) {
                target_found = true;
                target_idx = i;

                if(rid == -1) {
                    rid = tmp_rid;
                } else sm_assert(rid == tmp_rid);
            }
        }

        if(source_found && target_found) {
            break;
        }
    }

    // Go ahead and deregister this from the callee enclave
    if(!source_found || !target_found) {
        done = true;
    } else {
        enclaves[eid].regions[source_idx].type = REGION_INVALID;
        enclaves[eid].regions[source_idx].pmp_rid = 0;
        enclaves[with].regions[target_idx].type = REGION_INVALID;
        enclaves[with].regions[target_idx].pmp_rid = 0;
    }

    spin_unlock(&encl_lock);
    if(!done) {
        // Disable access to this region everywhere
        pmp_set_global(rid, PMP_NO_PERM);

        if(pmp_region_subregion_free_atomic(rid)) {
            ret = SBI_ERR_SM_ENCLAVE_PMP_FAILURE;
        } else {
            ret = SBI_ERR_SM_ENCLAVE_SUCCESS;
        }
    }

    return ret;
}

struct runtime_misc_params_t* get_enclave_misc_params(enclave_id eid) {
  return &enclaves[eid].misc_params;
}
