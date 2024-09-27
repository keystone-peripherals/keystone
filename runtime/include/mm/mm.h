#ifndef _MM_H_
#define _MM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mm/vm_defs.h"

uintptr_t translate(uintptr_t va);
pte* pte_of_va(uintptr_t va);
#ifdef USE_FREEMEM
uintptr_t alloc_page(uintptr_t vpn, int flags);
uintptr_t realloc_page(uintptr_t vpn, int flags);
void free_page(uintptr_t vpn);
size_t alloc_pages(uintptr_t vpn, size_t count, int flags);
void free_pages(uintptr_t vpn, size_t count);
size_t test_va_range(uintptr_t vpn, size_t count);
uintptr_t find_va_range(size_t count, bool user);

uintptr_t map_with_dynamic_page_table(uintptr_t base, uintptr_t size, uintptr_t va, bool user);
uintptr_t map_anywhere_with_dynamic_page_table(uintptr_t base, uintptr_t size, bool user);
void map_with_reserved_page_table(uintptr_t base, uintptr_t size, uintptr_t ptr, pte* l2_pt, pte* l3_pt);
void unmap_with_any_page_table(uintptr_t base, uintptr_t size);
void unmap_all_except_eyrie(pte *root);

#endif /* USE_FREEMEM */

#endif /* _MM_H_ */
