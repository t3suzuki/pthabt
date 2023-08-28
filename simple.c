#include <stdlib.h>
#include <pthread.h>
#include <abt.h>

#define N_TH (8)
#define ULT_N_TH (4*N_TH)

static ABT_xstream abt_xstreams[N_TH];
static ABT_thread abt_threads[ULT_N_TH];
static ABT_pool global_abt_pools[N_TH];
static unsigned int global_abt_tid = 0;

//#define __PTHREAD_VERBOSE__ (1)


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


typedef unsigned int my_magic_t;
typedef struct {
  my_magic_t magic;
  ABT_mutex abt_mutex;
} abt_mutex_wrap_t;

typedef struct {
  my_magic_t magic;
  ABT_cond abt_cond;
} abt_cond_wrap_t;


int pthread_mutex_init(pthread_mutex_t *mutex,
		       const pthread_mutexattr_t *attr) {
#if __PTHREAD_VERBOSE__
  printf("%s %d %p\n", __func__, __LINE__, mutex);
#endif
  abt_mutex_wrap_t *abt_mutex_wrap = (abt_mutex_wrap_t *)malloc(sizeof(abt_mutex_wrap_t));
  abt_mutex_wrap->magic = 0xdeadcafe;
  int ret = ABT_mutex_create(&abt_mutex_wrap->abt_mutex);
  *(abt_mutex_wrap_t **)mutex = abt_mutex_wrap;
  return ret;
}

inline static ABT_mutex *get_abt_mutex(pthread_mutex_t *mutex)
{
  if (*(my_magic_t *)mutex == 0) {
    pthread_mutex_init(mutex, NULL);
  }
  abt_mutex_wrap_t *abt_mutex_wrap = *(abt_mutex_wrap_t **)mutex;
  return &abt_mutex_wrap->abt_mutex;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
#if __PTHREAD_VERBOSE__
  printf("%s %d %p\n", __func__, __LINE__, mutex);
#endif
  ABT_mutex *abt_mutex = get_abt_mutex(mutex);
  return ABT_mutex_lock(*abt_mutex);
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
#if __PTHREAD_VERBOSE__
  printf("%s %d\n", __func__, __LINE__);
#endif
  ABT_mutex *abt_mutex = get_abt_mutex(mutex);
  return ABT_mutex_trylock(*abt_mutex);
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
#if __PTHREAD_VERBOSE__
  printf("%s %d\n", __func__, __LINE__);
#endif
  ABT_mutex *abt_mutex = get_abt_mutex(mutex);
  return ABT_mutex_unlock(*abt_mutex);
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
#if __PTHREAD_VERBOSE__
  printf("%s %d\n", __func__, __LINE__);
#endif
  ABT_mutex *abt_mutex = get_abt_mutex(mutex);
  return ABT_mutex_free(abt_mutex);
}



int pthread_cond_init(pthread_cond_t *cond,
		      const pthread_condattr_t *attr) {
#if __PTHREAD_VERBOSE__
  printf("%s %d\n", __func__, __LINE__);
#endif
  abt_cond_wrap_t *abt_cond_wrap = (abt_cond_wrap_t *)malloc(sizeof(abt_cond_wrap_t));
  abt_cond_wrap->magic = 0xdeadbeef;
  int ret = ABT_cond_create(&abt_cond_wrap->abt_cond);
  *(abt_cond_wrap_t **)cond = abt_cond_wrap;
  return ret;
}


inline static ABT_cond *get_abt_cond(pthread_cond_t *cond)
{
  if (*(my_magic_t *)cond == 0) {
    pthread_cond_init(cond, NULL);
  }
  abt_cond_wrap_t *abt_cond_wrap = *(abt_cond_wrap_t **)cond;
  return &abt_cond_wrap->abt_cond;
}

int pthread_cond_signal(pthread_cond_t *cond) {
#if __PTHREAD_VERBOSE__
  printf("%s %d\n", __func__, __LINE__);
#endif
  ABT_cond *abt_cond = get_abt_cond(cond);
  return ABT_cond_signal(*abt_cond);
}

int pthread_cond_destroy(pthread_cond_t *cond) {
#if __PTHREAD_VERBOSE__
  printf("%s %d\n", __func__, __LINE__);
#endif
  ABT_cond *abt_cond = get_abt_cond(cond);
  return ABT_cond_free(abt_cond);
}

int pthread_cond_wait(pthread_cond_t *cond,
		      pthread_mutex_t *mutex) {
#if __PTHREAD_VERBOSE__
  printf("%s %d\n", __func__, __LINE__);
#endif
  ABT_cond *abt_cond = get_abt_cond(cond);
  ABT_mutex *abt_mutex = get_abt_mutex(mutex);
  return ABT_cond_wait(*abt_cond, *abt_mutex);
}

int pthread_cond_timedwait(pthread_cond_t *cond,
			   pthread_mutex_t *mutex,
			   const struct timespec *abstime) {
#if __PTHREAD_VERBOSE__
  printf("%s %d\n", __func__, __LINE__);
#endif
  ABT_cond *abt_cond = get_abt_cond(cond);
  ABT_mutex *abt_mutex = get_abt_mutex(mutex);
  return ABT_cond_timedwait(*abt_cond, *abt_mutex, abstime);
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







