#include <stdlib.h>

typedef long (*syscall_fn_t)(long, long, long, long, long, long, long);

static syscall_fn_t next_sys_call = NULL;

long hook_function(long a1, long a2, long a3,
		   long a4, long a5, long a6,
		   long a7)
{
  return next_sys_call(a1, a2, a3, a4, a5, a6, a7);
}


int __hook_init(long placeholder __attribute__((unused)),
		void *sys_call_hook_ptr)
{
  next_sys_call = *((syscall_fn_t *) sys_call_hook_ptr);
  *((syscall_fn_t *) sys_call_hook_ptr) = hook_function;
  return 0;

}
