#include <stdint.h>
#define LIBDL
#include "e9patch/examples/stdlib.c"

static int (*dl_ABT_thread_yield)(void) = NULL;

void pref_yield(int64_t a)
{
  __builtin_prefetch((void *)a);
  dl_ABT_thread_yield();
}

void init(int argc, char **argv, char **envp, void *dynamic)
{
  dlinit(dynamic);
  
  void *dl_handle = dlopen("libabt.so.1", RTLD_NOW);
  if (dl_handle == NULL) {
    printf("dlopen error\n");
    exit(1);
  }
  
  dl_ABT_thread_yield = dlsym(dl_handle, "ABT_thread_yield");
  if (dl_ABT_thread_yield == NULL) {
    printf("dlsym error\n");
    exit(1);
  }
}
