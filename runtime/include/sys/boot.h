
#ifndef __BOOT_H__
#define __BOOT_H__

#include "sys/env.h"

void *
init_user_stack_and_env(ELF(Ehdr) *hdr);

#endif  // __BOOT_H__
