#include <stdlib.h>
#include <pthread.h>
#include <abt.h>

#define N_TH (4)
#define ULT_N_TH (4*N_TH)

static ABT_xstream abt_xstreams[N_TH];
static ABT_thread abt_threads[ULT_N_TH];
static ABT_pool global_abt_pools[N_TH];
static unsigned int global_abt_tid = 0;


int pthread_create(pthread_t *pth, const pthread_attr_t *attr,
		   void *(*start_routine) (void *), void *arg) {
  int abt_tid = global_abt_tid++;
  ABT_thread *abt_thread = (ABT_thread *)malloc(sizeof(ABT_thread));
  int ret = ABT_thread_create(global_abt_pools[abt_tid % N_TH],
			      (void (*)(void*))start_routine,
			      arg,
			      ABT_THREAD_ATTR_NULL,
			      abt_thread);
  *pth = (pthread_t)abt_thread;
  return 0;
}

int pthread_join(pthread_t pth, void **retval) {
  ABT_thread_join(*(ABT_thread *)pth);
  return 0;
}


int pthread_mutex_init(pthread_mutex_t *mutex,
		       const pthread_mutexattr_t *attr) {
  ABT_mutex *abt_mutex = (ABT_mutex *)malloc(sizeof(ABT_mutex));
  int ret = ABT_mutex_create(abt_mutex);
  *(ABT_mutex **)mutex = abt_mutex;
  return ret;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
  ABT_mutex *abt_mutex = *(ABT_mutex **)mutex;
  return ABT_mutex_lock(*abt_mutex);
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
  ABT_mutex *abt_mutex = *(ABT_mutex **)mutex;
  return ABT_mutex_trylock(*abt_mutex);
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
  ABT_mutex *abt_mutex = *(ABT_mutex **)mutex;
  return ABT_mutex_unlock(*abt_mutex);
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
  ABT_mutex *abt_mutex = *(ABT_mutex **)mutex;
  return ABT_mutex_free(abt_mutex);
}


__attribute__((constructor(0xffff))) static void
mylib_init()
{
  int i;
  printf(".so argobots!\n");
  ABT_init(0, NULL);
  ABT_xstream_self(&abt_xstreams[0]);
  for (i=1; i<N_TH; i++) {
    ABT_xstream_create(ABT_SCHED_NULL, &abt_xstreams[i]);
  }
  for (i=0; i<N_TH; i++) {
    ABT_xstream_get_main_pools(abt_xstreams[i], 1, &global_abt_pools[i]);
  }
}







