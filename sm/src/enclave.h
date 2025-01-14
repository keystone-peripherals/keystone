//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#ifndef _ENCLAVE_H_
#define _ENCLAVE_H_

#ifndef TARGET_PLATFORM_HEADER
#error "SM requires a defined platform to build"
#endif

#include "sm.h"
#include "pmp.h"
#include "thread.h"
#include <crypto.h>

// Special target platform header, set by configure script
#include TARGET_PLATFORM_HEADER
#include "sbi/riscv_locks.h"

#define ATTEST_DATA_MAXLEN  1024
#define MAX_ENCL_THREADS 1

typedef enum {
  INVALID = -1,
  DESTROYING = 0,
  ALLOCATED,
  FRESH,
  STOPPED,
  RUNNING,
} enclave_state;

/* For now, eid's are a simple unsigned int */
typedef unsigned int enclave_id;

/* Metadata around memory regions associate with this enclave
 * EPM is the 'home' for the enclave, contains runtime code/etc
 * UTM is the untrusted shared pages
 * MMIO is secure device space managed by the SM
 * OTHER is managed by some other component (e.g. platform_)
 * INVALID is an unused index
 */
enum enclave_region_type{
  REGION_INVALID = 0,
  REGION_EPM,
  REGION_UTM,
  REGION_MMIO,
  REGION_EXPORTED,
  REGION_OTHER,
};

struct enclave_region
{
  region_id pmp_rid;
  enum enclave_region_type type;
};

typedef enum {
    CALL_NONE,
    CALL_HOST,
    CALL_ENCLAVE
} call_source;

struct enclave_call
{
    // Tracking information
    call_source source;
    enclave_id from_encl, to_encl;

    // Stashed caller state
    struct thread_state stashed_state;
};

/* enclave metadata */
struct enclave
{
  //spinlock_t lock; //local enclave lock. we don't need this until we have multithreaded enclave
  enclave_id eid; //enclave id
  unsigned long encl_satp; // enclave's page table base
  enclave_state state; // global state of the enclave

  /* Physical memory regions associate with this enclave */
  struct enclave_region regions[ENCLAVE_REGIONS_MAX];

  /* Caller infrastructure */
  int call_depth[MAX_ENCL_THREADS];
  struct enclave_call call_stack[MAX_ENCL_THREADS][ENCLAVE_CALLS_MAX];

  /* Callee infrastructure */
  struct csrs handler_csrs;
  uintptr_t handler;

  /* measurement */
  byte hash[MDSIZE];
  byte sign[SIGNATURE_SIZE];

  /* parameters */
  struct runtime_va_params_t params;
  struct runtime_pa_params_t pa_params;
  struct runtime_misc_params_t misc_params;

  /* enclave execution context */
  unsigned int n_thread;
  struct thread_state threads[MAX_ENCL_THREADS];

  struct platform_enclave_data ped;
};

/* attestation reports */
struct enclave_report
{
  byte hash[MDSIZE];
  uint64_t data_len;
  byte data[ATTEST_DATA_MAXLEN];
  byte signature[SIGNATURE_SIZE];
};
struct sm_report
{
  byte hash[MDSIZE];
  byte public_key[PUBLIC_KEY_SIZE];
  byte signature[SIGNATURE_SIZE];
};
struct report
{
  struct enclave_report enclave;
  struct sm_report sm;
  byte dev_public_key[PUBLIC_KEY_SIZE];
};

/* sealing key structure */
#define SEALING_KEY_SIZE 128
struct sealing_key
{
  uint8_t key[SEALING_KEY_SIZE];
  uint8_t signature[SIGNATURE_SIZE];
};

/*** SBI functions & external functions ***/
// callables from the host
unsigned long create_enclave(unsigned long *eid, struct keystone_sbi_create_t create_args);
unsigned long destroy_enclave(enclave_id eid);
unsigned long run_enclave(struct sbi_trap_regs *regs, enclave_id eid);
unsigned long resume_enclave(struct sbi_trap_regs *regs, enclave_id eid);
// callables from the enclave
unsigned long exit_enclave(struct sbi_trap_regs *regs, enclave_id eid);
unsigned long stop_enclave(struct sbi_trap_regs *regs, uint64_t request, enclave_id eid);
unsigned long attest_enclave(uintptr_t report, uintptr_t data, uintptr_t size, enclave_id eid);
/* attestation and virtual mapping validation */
unsigned long validate_and_hash_enclave(struct enclave* enclave);
// TODO: These functions are supposed to be internal functions.
void enclave_init_metadata(void);
unsigned long copy_enclave_create_args(uintptr_t src, struct keystone_sbi_create_t* dest);
int get_enclave_region_index(enclave_id eid, enum enclave_region_type type);
uintptr_t get_enclave_region_base(enclave_id eid, int memid);
uintptr_t get_enclave_region_size(enclave_id eid, int memid);
unsigned long get_sealing_key(uintptr_t seal_key, uintptr_t key_ident, size_t key_ident_size, enclave_id eid);
unsigned long claim_mmio(uintptr_t dev_string, enclave_id eid);
unsigned long release_mmio(uintptr_t dev_string, enclave_id eid);
unsigned long call_enclave(struct sbi_trap_regs *regs, enclave_id from, enclave_id to, int type);
unsigned long ret_enclave(struct sbi_trap_regs *regs);
unsigned long register_handler(uintptr_t handler, enclave_id eid);
unsigned long share_region(uintptr_t addr, size_t size, enclave_id with, enclave_id eid);
unsigned long unshare_region(uintptr_t addr, enclave_id with, enclave_id eid);
struct runtime_misc_params_t* get_enclave_misc_params(enclave_id eid);

// interrupt handlers
void sbi_trap_handler_keystone_enclave(struct sbi_trap_regs *regs);

#endif
