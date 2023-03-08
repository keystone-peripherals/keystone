
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/user.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <vta/driver.h>

#include <app/syscall.h>

extern bool skip_claim;

#define UDMABUF_POOL "udmabuf-ddr-nc0"
static const char MEM_DEVICE[] = "cma@C0000000";

static volatile bool loop = true;
static bool dma_initialized = false;
uint64_t dma_base;
static uint8_t *dma_mem;
static size_t dma_size;

struct dma_allocation {
  bool allocated;
  uint64_t offs;
  size_t size;

  struct dma_allocation *prev;
  struct dma_allocation *next;
};

struct dma_allocation *dma_list = NULL;

int read_field(const char *field, const char *format, void *data) {
  char filename[256];
  int check;

  snprintf(filename, sizeof(filename),
           "/sys/class/u-dma-buf/" UDMABUF_POOL "/%s", field);
  FILE *fp = fopen(filename, "r");
  if(!fp) {
    fprintf(stderr, "could not find " UDMABUF_POOL " physical address\n");
    return 0;
  }

  check = fscanf(fp, format, data);
  (void) check;
  fclose(fp);
  return 1;
}

int destroy_memory() {
  int ret;
  struct utsname name;
  ret = uname(&name);
  if(ret < 0){
    fprintf(stderr, "Can't call uname\n");
    throw;
  }

  if(strcmp(name.version, "Eyrie") == 0) {
     // Unmap
     return unmap((uintptr_t) dma_mem, dma_size);
  }

  return 0;
}

int init_enclave(uint64_t *base, size_t *size) {
  int ret;
  *base = 0xC0000000;
  *size = 0x10000000;
  uintptr_t mapped;

  if(!skip_claim) {
    ret = claim_mmio(MEM_DEVICE, sizeof(MEM_DEVICE));
    if(ret < 0) {
      throw;
    }
  }

  mapped = map(*base, *size, *base);
  if(mapped != *base) {
    throw;
  }

  dma_mem = (uint8_t *) *base;
  dma_size = *size;
  return 0;
}

int init_linux(uint64_t *base, size_t *size) {
  int fd;

  // Read necessary fields
  if(!read_field("phys_addr", "%lx", base)) {
    fprintf(stderr, "ensure_initialized could not get base\n");
    throw;
  }

  if(!read_field("size", "%lu", size)) {
    fprintf(stderr, "ensure_initialized could not get size\n");
    throw;
  }

  fd = open("/dev/" UDMABUF_POOL, O_RDWR);
  if(fd < 0) {
    fprintf(stderr, "ensure_initialized could not open " UDMABUF_POOL "\n");
    throw;
  }

  dma_mem = (uint8_t *) mmap(nullptr, *size, PROT_READ | PROT_WRITE,
                           MAP_SHARED, fd, 0);
  if(dma_mem == MAP_FAILED) {
    fprintf(stderr, "ensure_initialized could not map " UDMABUF_POOL ": %s\n",
            strerror(errno));
    close(fd);
    throw;
  }

  dma_size = *size;
  return 0;
}

int ensure_initialized() {
  int ret;
  uint64_t base;
  size_t size;

  if(dma_initialized) {
    return 1;
  }

  struct utsname name;
  ret = uname(&name);
  if(ret < 0){
    fprintf(stderr, "Can't call uname\n");
    throw;
  }

  if(strcmp(name.version, "Eyrie") == 0) {
    ret = init_enclave(&base, &size);
  } else {
    ret = init_linux(&base, &size);
  }

  if(ret < 0) {
    fprintf(stderr, "Could not initialze memory\n");
    throw;
  }

  // Seed our allocator with the first chunk
  dma_base = base;

  dma_list = new struct dma_allocation;
  dma_list->allocated = false;
  dma_list->offs = 0;
  dma_list->size = size;
  dma_list->prev = nullptr;
  dma_list->next = nullptr;
  dma_initialized = true;
  return 1;
}

#define __round_mask(x, y) ((__typeof__(x))((y)-1))
#define round_up(x, y) ((((x)-1) | __round_mask(x, y))+1)

void* VTAMemAlloc(size_t size, int cached) {
  struct dma_allocation *curr, *result = nullptr;

  if(!ensure_initialized())
    return nullptr;

  // Size should be page aligned
  size = round_up(size, 0x1000);

  // Traverse the freelist to see if we can find a chunk
  curr = dma_list;
  while(curr != nullptr) {
    // Make sure our list is integral
    if(curr->next) {
      assert(curr->offs + curr->size == curr->next->offs);
      assert(curr->next->prev == curr);
    }

    if(!curr->allocated) {
      if(size == curr->size) {
        // Our allocation fits in this chunk exactly
        result = curr;
        result->allocated = true;
      } else if (size < curr->size) {
        // Carve out from beginning of this chunk
        result = new dma_allocation;
        result->allocated = true;
        result->offs = curr->offs;
        result->size = size;
        result->prev = curr->prev;
        result->next = curr;

        if(curr->prev) {
          curr->prev->next = result;
        } else {
          dma_list = result;
        }

        curr->prev = result;
        if(curr->next) {
          curr->next->prev = result;
        }

        curr->size -= size;
        curr->offs += size;
      }
    }

    if(result)
      break;

    curr = curr->next;
  }

  if(result)
    return dma_mem + result->offs;
  else
    return nullptr;
}

struct dma_allocation *find_alloc_from_virt(void *buf) {
  struct dma_allocation *curr = dma_list;
  while(curr) {
    if(curr->allocated && ((uint64_t) ((uint8_t *) buf - dma_mem) == curr->offs)) {
      return curr;
    }

    curr = curr->next;
  }

  return nullptr;
}

void VTAMemFree(void* buf) {
  struct dma_allocation *alloc;
  bool need_left = false, need_right = false;

  if(!ensure_initialized())
    return;

  // Search for the allocation
  alloc = find_alloc_from_virt(buf);
  if(!alloc) {
    return;
  }

  // Check if we need to merge the chunk left
  alloc->allocated = false;
  if(alloc->prev) {
    assert(alloc->prev->offs + alloc->prev->size == alloc->offs);
    assert(alloc->prev->next = alloc);
    need_left = !alloc->prev->allocated;
  }

  if(alloc->next) {
    assert(alloc->offs + alloc->size == alloc->next->offs);
    assert(alloc->next->prev = alloc);
    need_right = !alloc->next->allocated;
  }

  if(need_left && need_right) {
    alloc->prev->size += alloc->size + alloc->next->size;
    alloc->prev->next = alloc->next->next;
    if(alloc->prev->next) {
      alloc->prev->next->prev = alloc->prev;
    }

    delete alloc->next;
    delete alloc;
  } else if(need_left) {
    alloc->prev->size += alloc->size;
    if(alloc->prev->next) {
      alloc->prev->next->prev = alloc->prev;
    }

    alloc->prev->next = alloc->next;
    delete alloc;
  } else if(need_right) {
    alloc->size += alloc->next->size;
    if(alloc->next) {
      alloc->next->prev = alloc;
    }

    alloc->next = alloc->next->next;
    delete alloc->next;
  }
}

void VTAMemReset() {
  // Free all elements
  struct dma_allocation *curr = dma_list, *prev;
  while(curr) {
    prev = curr;
    curr = curr->next;
    delete prev;
  }

  dma_list = NULL;

  // Force reinitialization
  dma_list = new struct dma_allocation;
  dma_list->allocated = false;
  dma_list->offs = 0;
  dma_list->size = dma_size;
  dma_list->prev = nullptr;
  dma_list->next = nullptr;
  dma_initialized = true;
}

vta_phy_addr_t VTAMemGetPhyAddr(void* buf) {
  struct dma_allocation *alloc;
  if(!ensure_initialized())
    return 0;

  alloc = find_alloc_from_virt(buf);
  assert(alloc);
  return dma_base + alloc->offs;
}

void VTAMemCopyFromHost(void* dst, const void* src, size_t size) {
  if(!ensure_initialized())
    return;

  memcpy(dst, src, size);
}

void VTAMemCopyToHost(void* dst, const void* src, size_t size) {
  if(!ensure_initialized())
    return;

  memcpy(dst, src, size);
}

void VTAFlushCache(void* vir_addr, vta_phy_addr_t phy_addr, int size) {
  return;
}

void VTAInvalidateCache(void* vir_addr, vta_phy_addr_t phy_addr, int size) {
  return;
}
