#include <stdint.h>
#include "e9patch/examples/stdlib.c"

void
sched_yield()
{
}

void pref_yield(int64_t a)
{
  //printf("prefetching... %p\n", (void *)a);
  __builtin_prefetch(a);
  //sched_yield();
}
