//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#include "riscv64.h"
#include <linux/kernel.h>
#include "keystone.h"
#include <linux/dma-mapping.h>

static int encl_mem_alloc(struct encl_mem *mem, unsigned int min_pages) {
  vaddr_t vaddr = 0;
  phys_addr_t paddr = 0;
  unsigned long order, count;

  /* try to allocate contiguous memory */
  mem->is_cma = 0;
  order = ilog2(min_pages - 1) + 1;
  count = 0x1 << order;

  /* prevent kernel from complaining about an invalid argument */
  if (order < MAX_ORDER)
    vaddr = (vaddr_t) __get_free_pages(GFP_HIGHUSER, order);

#ifdef CONFIG_CMA
  /* If buddy allocator fails, we fall back to the CMA */
  if (!vaddr) {
    mem->is_cma = 1;
    count = min_pages;

    vaddr = (vaddr_t) dma_alloc_coherent(keystone_dev.this_device,
      count << PAGE_SHIFT,
      &paddr,
      GFP_KERNEL | __GFP_DMA32);

    if(!paddr) {
        vaddr = 0;
    }
  }
#endif

  if(!vaddr) {
    keystone_err("failed to allocate %lu page(s)\n", count);
    return -ENOMEM;
  }

  /* zero out */
  memset((void*)vaddr, 0, PAGE_SIZE*count);
  mem->root_page_table = (void*)vaddr;
  mem->pa = (mem->is_cma) ? paddr : __pa(vaddr);
  mem->order = order;
  mem->alloc_size = count << PAGE_SHIFT;
  mem->ptr = vaddr;
  return 0;
}

static int encl_mem_free(struct encl_mem *mem) {
  if(!mem->ptr || !mem->alloc_size)
    return 0;

  /* free the EPM hold by the enclave */
  if (mem->is_cma) {
    dma_free_coherent(keystone_dev.this_device,
              mem->alloc_size, (void*) mem->ptr, mem->pa);
  } else {
    free_pages(mem->ptr, mem->order);
  }

  return 0;
}

/* Destroy all memory associated with an EPM */
int epm_destroy(struct encl_mem* epm) {
  epm->root_page_table -= epm->align_pad;
  epm->pa -= epm->align_pad;
  epm->ptr -= epm->align_pad;

  return encl_mem_free(epm);
}

/* Create an EPM and initialize the free list */
int epm_init(struct encl_mem* epm, unsigned int min_pages)
{
  int ret = 0;
  bool need_to_pad = false;

  if(min_pages > 0x200) {
    min_pages += 0x200;
    need_to_pad = true;
  }

  ret = encl_mem_alloc(epm, min_pages);
  if(ret < 0) {
      return ret;
  }

  if(need_to_pad && (epm->ptr % 0x200000 != 0)) {
    epm->align_pad = 0x200000 - (epm->ptr % (0x200000));
  } else {
    epm->align_pad = 0;
  }

  epm->size = min_pages << PAGE_SHIFT;
  epm->root_page_table += epm->align_pad;
  epm->pa += epm->align_pad;
  epm->ptr += epm->align_pad;
  return 0;
}

int utm_destroy(struct encl_mem* utm){
    utm->root_page_table -= utm->align_pad;
    utm->pa -= utm->align_pad;
    utm->ptr -= utm->align_pad;

    return encl_mem_free(utm);
}

int utm_init(struct encl_mem* utm, size_t untrusted_size)
{
  int ret = 0;
  unsigned long req_pages = PAGE_UP(untrusted_size) / PAGE_SIZE;
  unsigned long order = ilog2(req_pages - 1) + 1;
  unsigned long napot;
  bool need_to_pad = false;

  // There's an implicit dependence on the UTM region being NAPOT.
  // If we are allocating a size less than MAX_ORDER, we are all good
  // since __get_free_pages will by default allocate naturally aligned
  // memory. However, we are in trouble if we want to allocate larger
  // UTM regions from CMA -- dma_alloc_coherent makes no such guarantees.
  // As a heuristic for now, we overallocate memory by 2x in these
  // situations. This guarantees that we can manually align ourselves
  // if needed

  if(order >= MAX_ORDER) {
      // This allocation will hit the CMA allocator. Will need to pad.
      need_to_pad = true;
      req_pages *= 2;
  }

  ret = encl_mem_alloc(utm, req_pages);
  if(ret < 0) {
      return ret;
  }

  if(need_to_pad) {
      napot = round_up(utm->pa, (0x1 << order) * PAGE_SIZE);
      pr_err("allocated %lx, napot is %lx, so padding %lx\n", utm->pa, napot, napot - utm->pa);

      // Find next upper NAPOT from our allocation and pad to it
      utm->align_pad = (napot - utm->pa);
      utm->size = (req_pages / 2) << PAGE_SHIFT;
  } else {
      utm->align_pad = 0;
      utm->size = req_pages << PAGE_SHIFT;
  }

  utm->root_page_table += utm->align_pad;
  utm->pa += utm->align_pad;
  utm->ptr += utm->align_pad;
  return 0;
}
