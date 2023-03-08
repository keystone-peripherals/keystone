#ifndef __ENV_H__
#define __ENV_H__

#include "util/rt_elf.h"

// How many AUX things are we actually defining? Add one for terminator
#define AUXV_COUNT 13

// Size in number-of-words (argc, argv, null_env, auxv, randombytes
#define SIZE_OF_SETUP (1+1+1+(2*AUXV_COUNT) + 2)

void* setup_start(void* _sp, ELF(Ehdr) *hdr);

#endif
