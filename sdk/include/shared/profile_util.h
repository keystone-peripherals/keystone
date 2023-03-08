#ifndef _KEYSTONE_PROFILE_UTIL_H_

#include <stdbool.h>
#include <stdint.h>

#ifndef NUM_MBS
#error "Must define NUM_MBS to use this header"
#endif

#ifndef SAMPLE_BUFFER_ELEMS
#error "Must define SAMPLE_BUFFER_ELEMS to use this header"
#endif

struct sharedmem_info {
  bool first;
  uint64_t count;
  uint32_t samples[NUM_MBS][SAMPLE_BUFFER_ELEMS];
};

#endif