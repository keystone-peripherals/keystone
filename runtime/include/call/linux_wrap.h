#ifdef USE_LINUX_SYSCALL
#ifndef _LINUX_WRAP_H_
#define _LINUX_WRAP_H_

#include <stddef.h>
#include <stdint.h>
#include <bits/types/sigset_t.h>

struct timespec;
struct sysinfo;

uintptr_t linux_uname(void* buf);
uintptr_t linux_clock_gettime(__clockid_t clock, struct timespec *tp);
uintptr_t linux_rt_sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
uintptr_t linux_getrandom(void *buf, size_t buflen, unsigned int flags);
uintptr_t linux_getpid();
uintptr_t linux_set_tid_address(int* tidptr);
uintptr_t linux_sysinfo(struct sysinfo *info);
uintptr_t linux_RET_ZERO_wrap(unsigned long which);
uintptr_t linux_RET_BAD_wrap(unsigned long which);
uintptr_t syscall_munmap(void *addr, size_t length);
uintptr_t syscall_mmap(void *addr, size_t length, int prot, int flags,
                  int fd, __off_t offset);
uintptr_t syscall_mprotect(void *addr, size_t len, int prot);
uintptr_t syscall_brk(void* addr);
uintptr_t syscall_ioctl(int fd, unsigned long req, uintptr_t ptr);
#endif /* _LINUX_WRAP_H_ */
#endif /* USE_LINUX_SYSCALL */
