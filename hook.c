#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <abt.h>
#include <stack>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <dis-asm.h>
#include <sched.h>
#include <dlfcn.h>
#include <pthread.h>
#include <abt.h>
#include <map>
#include <set>
#include <cassert>
#include <dlfcn.h>
#include <stdlib.h>

#include "common.h"
#include "real_pthread.h"

#define N_HELPER (128)


extern "C" {
void (*debug_print)(int) = NULL;

static void load_libmy(void)
{
	void *handle;
	{
	  const char *filename;
	  filename = getenv("LIBMY");
	  if (!filename) {
	    fprintf(stderr, "env LIBMY is empty, so skip to load a hook library\n");
	    return;
	  }
	  
	  handle = dlmopen(LM_ID_NEWLM, filename, RTLD_NOW | RTLD_LOCAL);
	  if (!handle) {
	    fprintf(stderr, "dlmopen failed: %s\n\n", dlerror());
	    fprintf(stderr, "NOTE: this may occur when the compilation of your hook function library misses some specifications in LDFLAGS. or if you are using a C++ compiler, dlmopen may fail to find a symbol, and adding 'extern \"C\"' to the definition may resolve the issue.\n");
	    exit(1);
	  }
	  printf("ok!\n");
	}
	{
	  debug_print = dlsym(handle, "__debug_print");
	}
}

typedef long (*syscall_fn_t)(long, long, long, long, long, long, long);

static syscall_fn_t next_sys_call = NULL;

  typedef struct {
    int id;
    pthread_t pth;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool done;
    bool ready;
    long arg[7];
    int ret;
  } ScHelper;
  ScHelper helpers[N_HELPER];
  
  inline void req_helper(int id, long a1, long a2, long a3, long a4, long a5, long a6, long a7) {
    helpers[id].done = false;
    helpers[id].arg[0] = a1;
    helpers[id].arg[1] = a2;
    helpers[id].arg[2] = a3;
    helpers[id].arg[3] = a4;
    helpers[id].arg[4] = a5;
    helpers[id].arg[5] = a6;
    helpers[id].arg[6] = a7;
    //printf("%d %d\n", id, a1);
    real_pthread_mutex_lock(&helpers[id].mutex);
    helpers[id].ready = true;
    real_pthread_cond_signal(&helpers[id].cond);
    real_pthread_mutex_unlock(&helpers[id].mutex);
  };
  void do_helper(void *arg) {
    ScHelper *h = (ScHelper *)arg;
    while (1) {
      real_pthread_mutex_lock(&(h->mutex));
      while (h->ready == false) {
	real_pthread_cond_wait(&h->cond, &h->mutex);
      }
      h->ready = false;
      real_pthread_mutex_unlock(&h->mutex);
#if 1
      h->ret = next_sys_call(h->arg[0], h->arg[1], h->arg[2], h->arg[3], h->arg[4], h->arg[5], h->arg[6]);
#else
      printf("id=%d, syscall=%d\n", h->id, h->arg[0]);
#endif
      h->done = true;
    }
  }
  

static long hook_function(long a1, long a2, long a3,
			  long a4, long a5, long a6,
			  long a7)
{
  
  //printf("output from hook_function: syscall number %ld\n", a1);
  //return next_sys_call(a1, a2, a3, a4, a5, a6, a7);
  
  uint64_t abt_id;
  int ret = ABT_self_get_thread_id(&abt_id);
  if (ret == ABT_SUCCESS && (abt_id > 0)) {
    if (a1 == 24) { // sched_yield
      ABT_thread_yield();
    } else if ((a1 == 202) || // futex
	       (a1 == 1)) { // write
      return next_sys_call(a1, a2, a3, a4, a5, a6, a7);
    } else {
      debug_print(a1);
      req_helper(abt_id, a1, a2, a3, a4, a5, a6, a7);
      while (1) {
	if (helpers[abt_id].done)
	  break;
	ABT_thread_yield();
      }
      //printf("[main] syscall done! %d\n", abt_id);
      return helpers[abt_id].ret;
    }
  } else {
    return next_sys_call(a1, a2, a3, a4, a5, a6, a7);
  }
}



int __hook_init(long placeholder __attribute__((unused)),
		void *sys_call_hook_ptr)
{
  /* real_pthread_create(&pth, NULL,  syscall_helper, NULL); */
  /* sleep(1); */
  /* printf("output from __hook_init: we can do some init work here\n"); */

  load_libmy();
  
  int i;
  for (i=0; i<N_HELPER; i++) {
    helpers[i].id = i;
    real_pthread_mutex_init(&helpers[i].mutex, NULL);
    real_pthread_cond_init(&helpers[i].cond, NULL);
    real_pthread_create(&helpers[i].pth, NULL,  do_helper, &helpers[i]);
  }

  sleep(1);
  next_sys_call = *((syscall_fn_t *) sys_call_hook_ptr);
  *((syscall_fn_t *) sys_call_hook_ptr) = hook_function;

  return 0;
}
}
