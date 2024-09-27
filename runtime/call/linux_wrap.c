#ifdef USE_LINUX_SYSCALL

#define _GNU_SOURCE
#include "call/linux_wrap.h"

#include <signal.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <time.h>

#include "mm/freemem.h"
#include "mm/mm.h"
#include "util/rt_util.h"
#include "call/syscall.h"
#include "uaccess.h"
#include "sys/thread.h"
#include <asm/ioctl.h>

#ifdef USE_CALLEE
#include "call/callee.h"
#endif // USE_CALLEE

#define CLOCK_FREQ 1000000000

uint64_t initial_time_since_unix_epoch_s;

//TODO we should check which clock this is
uintptr_t linux_clock_gettime(clockid_t clock, struct timespec *tp){
  print_strace("[runtime] clock_gettime not fully supported (clock %x, assuming)\r\n", clock);
  // // original
  // print_strace("[runtime] clock_gettime not fully supported (clock %x, assuming)\r\n", clock);
  // unsigned long cycles;
  // __asm__ __volatile__("rdcycle %0" : "=r"(cycles));
  // 
  // unsigned long sec = cycles / CLOCK_FREQ;
  // unsigned long nsec = (cycles % CLOCK_FREQ);
  // copy_to_user(&(tp->tv_sec), &sec, sizeof(unsigned long));
  // copy_to_user(&(tp->tv_nsec), &nsec, sizeof(unsigned long));
  // return 0;

  // // time from linux
  // in conf/qemu_riscv64_virt_defconfig
  // uintptr_t ret = -1;
  // struct edge_syscall* edge_syscall = (struct edge_syscall*)edge_call_data_ptr();
  // edge_syscall->syscall_num = SYS_clock_gettime;
  // struct sargs_SYS_clock_gettime* params = (struct sargs_SYS_clock_gettime*)edge_syscall->data;
  // params->clock = clock;
  // size_t totalsize = sizeof(struct edge_syscall) + sizeof(sargs_SYS_clock_gettime);
  // ret = dispatch_edgecall_syscall(edge_syscall, totalsize);
  // copy_to_user(tp, &params->tp, sizeof(struct timespec));
  // print_strace("!!! clock_gettime on clock %i sec: %ld\n", (int)clock, params->tp.tv_sec);
  // return ret;

  // time from initial_time_since_unix_epoch_s + hardware clock since boot
  unsigned long cycles;
  __asm__ __volatile__("rdcycle %0" : "=r"(cycles));

  unsigned long sec = cycles / CLOCK_FREQ;
  unsigned long nsec = (cycles % CLOCK_FREQ);

  copy_to_user(&(tp->tv_sec), &sec, sizeof(unsigned long));
  copy_to_user(&(tp->tv_nsec), &nsec, sizeof(unsigned long));

  struct timespec timespec;
  timespec.tv_sec = initial_time_since_unix_epoch_s + sec;
  timespec.tv_nsec = (initial_time_since_unix_epoch_s * 1000) + nsec;
  copy_to_user(tp, &timespec, sizeof(struct timespec));
  print_strace("!!! clock_gettime on clock %i sec: %ld\n", (int)clock, timespec.tv_sec);
  return 0;
}

uintptr_t linux_set_tid_address(int* tidptr_t){
#ifdef USE_CALLEE
  if(!is_main_thread()) {
    print_strace("[runtime] set_tid_address for callee to %p\r\n", tidptr_t);
    return syscall_set_tid_address(tidptr_t);
  } else
#endif // USE_CALLEE
  {
    //Ignore for now
    print_strace("[runtime] set_tid_address, not setting address (%p), IGNORING\r\n",tidptr_t);
    return 1;
  }
}

uintptr_t linux_rt_sigprocmask(int how, const sigset_t *set, sigset_t *oldset){
  print_strace("[runtime] rt_sigprocmask not supported (how %x), IGNORING\r\n", how);
  return 0;
}

uintptr_t linux_sysinfo(struct sysinfo *info) {
  struct sysinfo sysinfo = {
    .uptime = 0,
      .loads = {0, 0, 0},
      .totalram = freemem_size / RISCV_PAGE_SIZE,
      .freeram = spa_available(),
      .sharedram = shared_buffer_size / RISCV_PAGE_SIZE,
      .bufferram = 0,
      .totalswap = 0,
      .freeswap = 0,
      .procs = 1,
      .totalhigh = 0,
      .freehigh = 0,
      .mem_unit = RISCV_PAGE_SIZE
  };

  copy_to_user(info, &sysinfo, sizeof(struct sysinfo));
  return 0;
}

uintptr_t linux_RET_ZERO_wrap(unsigned long which){
  print_strace("[runtime] Cannot handle syscall %lu, IGNORING = 0\r\n", which);
  return 0;
}

uintptr_t linux_RET_BAD_wrap(unsigned long which){
  print_strace("[runtime] Cannot handle syscall %lu, FAILING = -1\r\n", which);
  return -1;
}

uintptr_t linux_getpid(){
  uintptr_t fakepid = 2;
  print_strace("[runtime] Faking getpid with %lx\r\n",fakepid);
  return fakepid;
}

uintptr_t linux_getrandom(void *buf, size_t buflen, unsigned int flags){

  uintptr_t ret = rt_util_getrandom(buf, buflen);
  print_strace("[runtime] getrandom IGNORES FLAGS (size %lx), PLATFORM DEPENDENT IF SAFE = ret %lu\r\n", buflen, ret);
  return ret;
}

#define UNAME_SYSNAME "Linux\0"
#define UNAME_NODENAME "Encl\0"
#define UNAME_RELEASE "5.16.0\0"
#define UNAME_VERSION "Eyrie\0"
#define UNAME_MACHINE "NA\0"

uintptr_t linux_uname(void* buf){
  // Here we go

  struct utsname *user_uname = (struct utsname *)buf;
  uintptr_t ret;

  ret = copy_to_user(&user_uname->sysname, UNAME_SYSNAME, sizeof(UNAME_SYSNAME));
  if(ret != 0) goto uname_done;

  ret = copy_to_user(&user_uname->nodename, UNAME_NODENAME, sizeof(UNAME_NODENAME));
  if(ret != 0) goto uname_done;

  ret = copy_to_user(&user_uname->release, UNAME_RELEASE, sizeof(UNAME_RELEASE));
  if(ret != 0) goto uname_done;

  ret = copy_to_user(&user_uname->version, UNAME_VERSION, sizeof(UNAME_VERSION));
  if(ret != 0) goto uname_done;

  ret = copy_to_user(&user_uname->machine, UNAME_MACHINE, sizeof(UNAME_MACHINE));
  if(ret != 0) goto uname_done;



 uname_done:
  print_strace("[runtime] uname = %x\n",ret);
  return ret;
}

uintptr_t syscall_munmap(void *addr, size_t length){
  uintptr_t ret = (uintptr_t)((void*)-1);

  free_pages(vpn((uintptr_t)addr), length/RISCV_PAGE_SIZE);
  ret = 0;
  tlb_flush();
  return ret;
}

uintptr_t syscall_mmap(void *addr, size_t length, int prot, int flags,
                 int fd, __off_t offset){
  int req_pages = 0;
  uintptr_t ret = (uintptr_t)((void*)-1);

  int pte_flags = PTE_U | PTE_A;

  // ignore stack flag
  if((flags & ~MAP_STACK) != (MAP_ANONYMOUS | MAP_PRIVATE) || fd != -1){
    // we don't support mmaping any other way yet
    goto done;
  }

  // Set flags
  if(prot & PROT_READ)
    pte_flags |= PTE_R;
  if(prot & PROT_WRITE)
    pte_flags |= PTE_W | PTE_D;
  if(prot & PROT_EXEC)
    pte_flags |= PTE_X;



  // Find a continuous VA space that will fit the req. size
  req_pages = vpn(PAGE_UP(length));

  // Do we have enough available phys pages?
  if( req_pages > spa_available()){
    goto done;
  }

  // Start looking at EYRIE_ANON_REGION_START for VA space
  uintptr_t vpn = find_va_range(req_pages, true);
  if(vpn && alloc_pages(vpn, req_pages, pte_flags) == req_pages) {
    ret = vpn << RISCV_PAGE_BITS;
  }

 done:
  tlb_flush();
  print_strace("[runtime] [mmap]: addr: 0x%p, length %lu, prot 0x%x, flags 0x%x, fd %i, offset %lu (%li pages %x) = 0x%p\r\n", addr, length, prot, flags, fd, offset, req_pages, pte_flags, ret);

  // If we get here everything went wrong
  return ret;
}

uintptr_t syscall_mprotect(void *addr, size_t len, int prot) {
  int i, ret;
  size_t pages = len / RISCV_PAGE_SIZE;

  int pte_flags = PTE_U | PTE_A;
  if(prot & PROT_READ)
    pte_flags |= PTE_R;
  if(prot & PROT_WRITE)
    pte_flags |= (PTE_W | PTE_D);
  if(prot & PROT_EXEC)
    pte_flags |= PTE_X;

  for(i = 0; i < pages; i++) {
    ret = realloc_page(vpn((uintptr_t) addr) + i, pte_flags);
    if(!ret)
      return -1;
  }

  return 0;
}

uintptr_t syscall_brk(void* addr){
  // Two possible valid calls to brk we handle:
  // NULL -> give current break
  // ADDR -> give more pages up to ADDR if possible

  uintptr_t req_break = (uintptr_t)addr;

  uintptr_t current_break = get_program_break();
  uintptr_t ret = -1;
  int req_page_count = 0;

  // Return current break if null or current break
  if (req_break == 0) {
    ret = current_break;
    goto done;
  }

  if(req_break <= current_break){
    ret = req_break;
    goto done;
  }

  // Otherwise try to allocate pages

  // Can we allocate enough phys pages?
  req_page_count = (PAGE_UP(req_break) - current_break) / RISCV_PAGE_SIZE;
  if( spa_available() < req_page_count){
    goto done;
  }

  // Check if this memory is allocated
  if(test_va_range(vpn(current_break), req_page_count) != req_page_count) {
    goto done;
  }

  // Allocate pages
  // TODO free pages on failure
  if( alloc_pages(vpn(current_break),
                  req_page_count,
                  PTE_W | PTE_R | PTE_D | PTE_U | PTE_A)
      != req_page_count){
    goto done;
  }

  // Success
  set_program_break(PAGE_UP(req_break));
  ret = req_break;


 done:
  tlb_flush();
  print_strace("[runtime] brk (0x%p) (req pages %i) = 0x%p\r\n",req_break, req_page_count, ret);
  return ret;

}

uintptr_t syscall_ioctl(int fd, unsigned long req, uintptr_t ptr) {
  uintptr_t ret = -1;
  struct edge_syscall* edge_syscall = (struct edge_syscall*)edge_call_data_ptr();
  sargs_SYS_ioctl* args = (sargs_SYS_ioctl*) edge_syscall->data;

  edge_syscall->syscall_num = SYS_ioctl;
  args->fd = fd;
  args->request = req;

  // If this is an output call, copy data into the buffer
  if(_IOC_DIR(req) & IOC_OUT) {
    if(edge_call_check_ptr_valid((uintptr_t) (args + 1), _IOC_SIZE(req)) != 0) {
      goto done;
    }

    copy_from_user(args + 1, (void *) ptr, _IOC_SIZE(req));
  }

  size_t totalsize = sizeof(struct edge_syscall) + sizeof(sargs_SYS_ioctl) + _IOC_SIZE(req);
  ret = dispatch_edgecall_syscall(edge_syscall, totalsize);

  // If this is an input call, copy data out of the buffer
  if(_IOC_DIR(req) & IOC_IN) {
    copy_to_user((void *) ptr, args + 1, _IOC_SIZE(req));
  }

done:
  print_strace("[runtime] ioctl on %i with %x = %i\r\n", fd, req, ret);
  return ret;
}

#endif /* USE_LINUX_SYSCALL */
