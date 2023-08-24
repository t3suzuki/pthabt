#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <abt.h>

#include "common.h"

extern "C" {
typedef long (*syscall_fn_t)(long, long, long, long, long, long, long);

static syscall_fn_t next_sys_call = NULL;

static pthread_mutex_t mutex;
static pthread_cond_t cond;
int done;

long arg[8];
long ret;

void *
syscall_helper()
{

  pthread_mutex_init(&mutex, NULL);
  pthread_cond_init(&cond, NULL);
  
  while (1) {
    pthread_mutex_lock(&mutex);
    pthread_cond_wait(&cond, &mutex);
    pthread_mutex_unlock(&mutex);
    printf("[helper] go to sleep...\n");
    ret = next_sys_call(arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7]);
    printf("[helper] wakeup!\n");
    done = 1;
  }
}

static long hook_function(long a1, long a2, long a3,
			  long a4, long a5, long a6,
			  long a7)
{
  //printf("output from hook_function: syscall number %ld\n", a1);
  if (a1 == 230) {
    int64_t id;
    int ret2 = ABT_self_get_thread_id(&id);
    printf("%s %d %d %d\n", __func__, __LINE__, id, ret2);
    //printf("[main] call syscall...\n");
    done = 0;
    arg[1] = a1;
    arg[2] = a2;
    arg[3] = a3;
    arg[4] = a4;
    arg[5] = a5;
    arg[6] = a6;
    arg[7] = a7;
#if 1
    return next_sys_call(a1, a2, a3, a4, a5, a6, a7);
#else
    pthread_mutex_lock(&mutex);
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
    while (1) {
      if (done)
	break;
      ABT_thread_yield();
    }
#endif
    printf("[main] syscall done!\n");
    return ret;
  } else {
    return next_sys_call(a1, a2, a3, a4, a5, a6, a7);
  }
}


pthread_t pth;

int __hook_init(long placeholder __attribute__((unused)),
		void *sys_call_hook_ptr)
{
  //pthread_create(&pth, NULL,  syscall_helper, NULL);
  sleep(1);
  printf("output from __hook_init: we can do some init work here\n");

  next_sys_call = *((syscall_fn_t *) sys_call_hook_ptr);
  *((syscall_fn_t *) sys_call_hook_ptr) = hook_function;

  return 0;
}
}
