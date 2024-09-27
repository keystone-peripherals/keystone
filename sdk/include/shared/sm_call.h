#ifndef __SM_CALL_H__
#define __SM_CALL_H__

// BKE (Berkeley Keystone Enclave)
#define SBI_EXT_EXPERIMENTAL_KEYSTONE_ENCLAVE 0x08424b45

#define SBI_SET_TIMER 0
#define SBI_CONSOLE_PUTCHAR 1
#define SBI_CONSOLE_GETCHAR 2

/* 0-1999 are not used (deprecated) */
#define FID_RANGE_DEPRECATED      1999
/* 2000-2999 are called by host */
#define SBI_SM_CREATE_ENCLAVE    2001
#define SBI_SM_DESTROY_ENCLAVE   2002
#define SBI_SM_RUN_ENCLAVE       2003
#define SBI_SM_RESUME_ENCLAVE    2005
#define FID_RANGE_HOST           2999

/* 3000-3999 are called by enclave */
#define SBI_SM_RANDOM            3001
#define SBI_SM_ATTEST_ENCLAVE    3002
#define SBI_SM_GET_SEALING_KEY   3003
#define SBI_SM_STOP_ENCLAVE      3004
#define SBI_SM_EXIT_ENCLAVE      3006
#define SBI_SM_CLAIM_MMIO        3007
#define SBI_SM_RELEASE_MMIO      3008
#define SBI_SM_CALL_ENCLAVE      3009
#define SBI_SM_RET_ENCLAVE       3010
#define SBI_SM_REGISTER_HANDLER  3011
#define SBI_SM_SHARE_REGION      3012
#define SBI_SM_UNSHARE_REGION    3013
#define SBI_SM_GET_MISC_PARAMS   3014
#define FID_RANGE_ENCLAVE        3999

/* 4000-4999 are experimental */
#define SBI_SM_CALL_PLUGIN        4000
#define FID_RANGE_CUSTOM          4999

/* Plugin IDs and Call IDs */
#define SM_MULTIMEM_PLUGIN_ID   0x01
#define SM_MULTIMEM_CALL_GET_SIZE 0x01
#define SM_MULTIMEM_CALL_GET_ADDR 0x02

/* Enclave stop reasons requested */
#define STOP_TIMER_INTERRUPT  0
#define STOP_EDGE_CALL_HOST   1
#define STOP_EXIT_ENCLAVE     2
#define STOP_YIELD_ENCLAVE    3

/* Structs for interfacing into the SM */
struct runtime_va_params_t {
  uintptr_t runtime_entry;
  uintptr_t user_entry;
  uintptr_t untrusted_ptr;
  uintptr_t untrusted_size;
};

struct runtime_pa_params_t {
  uintptr_t dram_base;
  uintptr_t dram_size;
  uintptr_t runtime_base;
  uintptr_t user_base;
  uintptr_t free_base;
};

struct runtime_misc_params_t {
  uint64_t time_since_unix_epoch_s;
};

struct keystone_sbi_pregion_t {
  uintptr_t paddr;
  size_t size;
};

struct keystone_sbi_create_t {
  struct keystone_sbi_pregion_t epm_region;
  struct keystone_sbi_pregion_t utm_region;

  uintptr_t runtime_paddr;
  uintptr_t user_paddr;
  uintptr_t free_paddr;

  struct runtime_va_params_t params;
  uint8_t force_tor;
  struct runtime_misc_params_t misc_params;
};

#endif  // __SM_CALL_H__
