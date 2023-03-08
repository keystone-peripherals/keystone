
#include "platform-hook.h"
#include "enclave.h"
#include <mss_sys_services.h>
#include <sbi/riscv_locks.h>
#include <sbi/sbi_string.h>
#include <sm_assert.h>

unsigned long platform_init_global_once(void){
  return SBI_ERR_SM_ENCLAVE_SUCCESS;
}

unsigned long platform_init_global(void){
  return SBI_ERR_SM_ENCLAVE_SUCCESS;
}

void platform_init_enclave(struct enclave* enclave){
  return;
}

void platform_destroy_enclave(struct enclave* enclave){
  return;
}

unsigned long platform_create_enclave(struct enclave* enclave){
  return SBI_ERR_SM_ENCLAVE_SUCCESS;
}

void platform_switch_to_enclave(struct enclave* enclave){
  return;
}

void platform_switch_from_enclave(struct enclave* enclave){
  return;
}

uint64_t platform_random(void){
#pragma message("Platform has no entropy source, this is unsafe. TEST ONLY")
  static uint64_t w = 0, s = 0xb5ad4eceda1ce2a9;

  unsigned long cycles;
  asm volatile ("rdcycle %0" : "=r" (cycles));

  // from Middle Square Weyl Sequence algorithm
  uint64_t x = cycles;
  x *= x;
  x += (w += s);
  return (x>>32) | (x<<32);
}

// Initialization functions
extern byte dev_public_key[PUBLIC_KEY_SIZE];

// Todo secure boot
void sm_copy_key(void)
{
  sbi_memset(sm_hash, 0, MDSIZE);
  sbi_memset(sm_signature, 0, SIGNATURE_SIZE);
  sbi_memset(sm_public_key, 0, PUBLIC_KEY_SIZE);
  sbi_memset(sm_private_key, 0, PRIVATE_KEY_SIZE);
  sbi_memset(dev_public_key, 0, PUBLIC_KEY_SIZE);
}
