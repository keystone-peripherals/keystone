#include "callee.h"

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "syscall.h"

/*******************************/
/** Callee-side functionality **/
/*******************************/

void default_callee_attrs(pthread_attr_t *attrs) {
  // Need to limit stack size since the default is kinda
  // huge for an embedded platform such as keystone.
  pthread_attr_setstacksize(attrs, 0x8000);

  // Create callee threads detached, since we won't explicitly join
  // with them later and thread resources (notably stacks) must be reclaimed
  pthread_attr_setdetachstate(attrs, PTHREAD_CREATE_DETACHED);
}

#define MAX_CALLEE_THREADS  8
typedef int (called_function_t)(void *);
static struct tinfo {
  bool used;
  pthread_t handle;
  called_function_t*fn;
} callee_threadinfo[MAX_CALLEE_THREADS];

uint64_t pthread_create_start, pthread_create_end;

void *dispatch_callee_handler(void *arg) {
  int fn_ret;
  struct shared_callee_buffer call_args;
  static pthread_attr_t callee_attrs;
  struct tinfo *tinfo = arg;
  called_function_t*fn = tinfo->fn;

  // Pull arguments from stack
  get_call_args(&call_args, sizeof(struct shared_callee_buffer));

  // Call the function
  fn_ret = fn(&call_args.args[1]);

  // The handler has finished! Create another thread to handle
  // another call, so we keep propagating this slot. Note that if
  // this call fails, future callee handlers in the runtime will
  // start to die as they are not replaced. This will lead to errors
  // in the sender, and leaving warnings about this in the receiver
  // (here) is left as a TODO
  default_callee_attrs(&callee_attrs);

  __asm__ volatile("rdcycle %0" : "=r"(pthread_create_start));
  pthread_create(&tinfo->handle, &callee_attrs,
                       dispatch_callee_handler, tinfo);
  __asm__ volatile("rdcycle %0" : "=r"(pthread_create_end));

  return (void *) (uintptr_t) fn_ret;
}

int spawn_callee_handler(called_function_t*fn, call_type_t type) {
  int i, ret = -1;
  static pthread_attr_t callee_attrs;
  default_callee_attrs(&callee_attrs);

  if(type == CALL_RECEIVER && fn) {
    // For functions called in the receiver, we need to ensure that
    // a function to dispatch to actually exists.
    for(i = 0; i < MAX_CALLEE_THREADS; i++) {
      if(!callee_threadinfo[i].used) {
        callee_threadinfo[i].used = true;
        callee_threadinfo[i].fn = fn;

        __asm__ volatile("rdcycle %0" : "=r"(pthread_create_start));
        ret = pthread_create(&callee_threadinfo[i].handle,
                              &callee_attrs,
                              dispatch_callee_handler,
                              &callee_threadinfo[i]);
        __asm__ volatile("rdcycle %0" : "=r"(pthread_create_end));
        return ret;
      }
    }
  } else if ((type == CALL_MAPPED /* || type == CALL_MAPPED_ASYNC */) && !fn) {
    // Just need to call fork() and the runtime will take care of the rest
   ret = fork();
   if(ret > 0) {
      ret = 0;
   }
  }

  return ret;
}

/*******************************/
/** Caller-side functionality **/
/*******************************/

uint64_t call_enclave(int eid, call_type_t type, int nargs, ...) {
  int i;
  va_list ptr;
  uint64_t args[CALLER_MAX_ARGS - 1] = {0};

  // Cannot currently pass more than 5 registers
  if(nargs > CALLER_MAX_ARGS - 1) {
    return -1;
  }

  va_start(ptr, nargs);
  for(i = 0; i < nargs; i++) {
    args[i] = va_arg(ptr, uint64_t);
  }
  va_end(ptr);

  return SYSCALL_6(RUNTIME_SYSCALL_CALL_ENCLAVE, eid, type,
                   args[0], args[1], args[2], args[3]);
}

__attribute__((noreturn))
void ret_enclave(int ret) {
  SYSCALL_1(RUNTIME_SYSCALL_RET_ENCLAVE, ret);
  __builtin_unreachable();
}
