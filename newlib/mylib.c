#include <stdlib.h>
#include <pthread.h>
#include <abt.h>
#include <errno.h>
#include "real_pthread.h"
#include "common.h"


static ABT_xstream abt_xstreams[N_CORE];
static ABT_thread abt_threads[N_ULT];
static ABT_pool global_abt_pools[N_CORE];
static unsigned int global_my_tid = 0;
#if USE_PREEMPT
static ABT_preemption_group abt_preemption_group;
#endif

//#define __PTHREAD_VERBOSE__ (1)

#include <execinfo.h>

void
print_bt()
{
  size_t i;
  void *trace[128];
  char **ss_trace;
  size_t size = backtrace(trace, sizeof(trace) / sizeof(trace[0]));
  ss_trace = backtrace_symbols(trace, size);
  if (ss_trace == NULL) {
    /*Failure*/
    return;
  }
  
  for (i = 0; i < size; i++) {
    printf("%s\n", ss_trace[i]);
  }
  free(ss_trace);
}

int pthread_create(pthread_t *pth, const pthread_attr_t *attr,
		   void *(*start_routine) (void *), void *arg) {
  int my_tid = __sync_fetch_and_add(&my_tid, 1);
  //int my_tid = global_my_tid++;
  ABT_thread *abt_thread = (ABT_thread *)malloc(sizeof(ABT_thread));
#if USE_PREEMPT
  ABT_thread_attr abt_attr;
  ABT_thread_attr_create(&abt_attr);
  ABT_thread_attr_set_preemption_type(abt_attr, ABT_PREEMPTION_NEW_ES);
  int ret = ABT_thread_create(global_abt_pools[my_tid % N_CORE],
			      (void (*)(void*))start_routine,
			      arg,
			      abt_attr,
			      abt_thread);
#else
  int ret = ABT_thread_create(global_abt_pools[my_tid % N_CORE],
			      (void (*)(void*))start_routine,
			      arg,
			      ABT_THREAD_ATTR_NULL,
			      abt_thread);
#endif
  
#if __PTHREAD_VERBOSE__
  unsigned long long abt_id;
  ABT_thread_get_id(*abt_thread, (ABT_unit_id *)&abt_id);
  printf("%s %d ABT_id %llu @ core %d\n", __func__, __LINE__, abt_id, my_tid % N_CORE);
  //print_bt();
#endif
  *pth = (pthread_t)abt_thread;
  return 0;
}

int pthread_join(pthread_t pth, void **retval) {
  ABT_thread_join(*(ABT_thread *)pth);
  free((ABT_thread *)pth);
  return 0;
}

int pthread_detach(pthread_t pth) {
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
  int ret = ABT_mutex_free(abt_mutex);
  abt_mutex_wrap_t *abt_mutex_wrap = *(abt_mutex_wrap_t **)mutex;
  free(abt_mutex_wrap);
  return ret;
}



int pthread_cond_init(pthread_cond_t *cond,
		      const pthread_condattr_t *attr) {
#if __PTHREAD_VERBOSE__
  printf("%s %d %p\n", __func__, __LINE__, cond);
#endif
  abt_cond_wrap_t *abt_cond_wrap = (abt_cond_wrap_t *)malloc(sizeof(abt_cond_wrap_t));
  abt_cond_wrap->magic = 0xdeadbeef;
#if 1
  int ret = ABT_cond_create(&abt_cond_wrap->abt_cond);
#else
  clockid_t clock_id;
  real_pthread_condattr_getclock(attr, &clock_id);
  int ret = ABT_cond_create2(&abt_cond_wrap->abt_cond, clock_id);
#endif
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
  printf("%s %d %p\n", __func__, __LINE__, cond);
#endif
  ABT_cond *abt_cond = get_abt_cond(cond);
  return ABT_cond_signal(*abt_cond);
}

int pthread_cond_destroy(pthread_cond_t *cond) {
#if __PTHREAD_VERBOSE__
  printf("%s %d %p\n", __func__, __LINE__, cond);
#endif
  ABT_cond *abt_cond = get_abt_cond(cond);
  int ret = ABT_cond_free(abt_cond);
  abt_cond_wrap_t *abt_cond_wrap = *(abt_cond_wrap_t **)cond;
  free(abt_cond_wrap);
  return ret;
}

int pthread_cond_wait(pthread_cond_t *cond,
		      pthread_mutex_t *mutex) {
#if __PTHREAD_VERBOSE__
  printf("%s %d %p\n", __func__, __LINE__, cond);
#endif
  ABT_cond *abt_cond = get_abt_cond(cond);
  ABT_mutex *abt_mutex = get_abt_mutex(mutex);
  //printf("%s %d %p %p\n", __func__, __LINE__, cond, abt_cond);
  int ret = ABT_cond_wait(*abt_cond, *abt_mutex);
  return ret;
}

int pthread_cond_broadcast(pthread_cond_t *cond)
{
#if __PTHREAD_VERBOSE__
  printf("%s %d\n", __func__, __LINE__);
#endif
  ABT_cond *abt_cond = get_abt_cond(cond);
  return ABT_cond_broadcast(*abt_cond);
}

int pthread_cond_timedwait(pthread_cond_t *cond,
			   pthread_mutex_t *mutex,
			   const struct timespec *abstime) {
#if __PTHREAD_VERBOSE__
  printf("%s %d %p\n", __func__, __LINE__, cond);
#endif
  ABT_cond *abt_cond = get_abt_cond(cond);
  ABT_mutex *abt_mutex = get_abt_mutex(mutex);
  //printf("%s %d %p %p\n", __func__, __LINE__, cond, abt_cond);
  int ret = ABT_cond_timedwait(*abt_cond, *abt_mutex, abstime);
  //printf("%s %d %p %d\n", __func__, __LINE__, cond, ret);
  if (ret == ABT_ERR_COND_TIMEDOUT) {
    return ETIMEDOUT;
  } else {
    return 0;
  }
}

int pthread_cond_clockwait(pthread_cond_t *cond,
			   pthread_mutex_t *mutex,
			   clockid_t clk,
			   const struct timespec *abstime) {
#if __PTHREAD_VERBOSE__
  printf("%s %d\n", __func__, __LINE__);
#endif
  return pthread_cond_timedwait(cond, mutex, abstime);
}


#define N_KEY (1024)
static ABT_key *abt_keys[N_KEY];
static ABT_mutex abt_key_mutex;

int pthread_key_create(pthread_key_t *key, void (*destructor)(void*)) {
#if __PTHREAD_VERBOSE__
  printf("%s %d %p\n", __func__, __LINE__, key);
#endif
  
  ABT_mutex_lock(abt_key_mutex);
  int i_key;
  for (i_key=0; i_key<N_KEY; i_key++) {
    if (abt_keys[i_key] == 0) {
      break;
    }
  }
  if (i_key < N_KEY) {
    abt_keys[i_key] = (ABT_key *)malloc(sizeof(ABT_key));
    int ret = ABT_key_create(destructor, abt_keys[i_key]);
    *key = i_key;
    ABT_mutex_unlock(abt_key_mutex);
    return ret;
  } else {
    ABT_mutex_unlock(abt_key_mutex);
    return EAGAIN;
  }
}

int pthread_setspecific(pthread_key_t key, const void *value) {
#if __PTHREAD_VERBOSE__
  printf("%s %d key=%d\n", __func__, __LINE__, key);
#endif
  return ABT_key_set(*(abt_keys[key]), (void *)value);
}

void * pthread_getspecific(pthread_key_t key) {
#if __PTHREAD_VERBOSE__
  printf("%s %d key=%d\n", __func__, __LINE__, key);
#endif
  void *ret;
  ABT_key_get(*(abt_keys[key]), &ret);
  return ret;
}

int pthread_key_delete(pthread_key_t key) {
  int ret = ABT_key_free(abt_keys[key]);
  free(abt_keys[key]);
  abt_keys[key] = 0;
  return ret;
}

int pthread_rwlock_init(pthread_rwlock_t *rwlock,
			const pthread_rwlockattr_t *attr) {

  ABT_rwlock *abt_rwlock = (ABT_rwlock *)malloc(sizeof(ABT_rwlock));
  ABT_rwlock_create(abt_rwlock);
  *(ABT_rwlock **)rwlock = abt_rwlock;
  return 0;
}

inline static ABT_rwlock *get_abt_rwlock(pthread_rwlock_t *rwlock)
{
  ABT_rwlock *abt_rwlock = *(ABT_rwlock **)rwlock;
  return abt_rwlock;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock)
{
  ABT_rwlock *abt_rwlock = get_abt_rwlock(rwlock);
  return ABT_rwlock_rdlock(*abt_rwlock);
}

int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock)
{
  ABT_rwlock *abt_rwlock = get_abt_rwlock(rwlock);
  return ABT_rwlock_wrlock(*abt_rwlock);
}

int pthread_rwlock_unlock(pthread_rwlock_t *rwlock)
{
  ABT_rwlock *abt_rwlock = get_abt_rwlock(rwlock);
  return ABT_rwlock_unlock(*abt_rwlock);
}

int pthread_rwlock_destory(pthread_rwlock_t *rwlock)
{
  ABT_rwlock *abt_rwlock = get_abt_rwlock(rwlock);
  int ret = ABT_rwlock_free(abt_rwlock);
  free(abt_rwlock);
  return ret;
}

int pthread_once(pthread_once_t *once_control,
		 void (*init_routine)(void)) {
#if __PTHREAD_VERBOSE__
  printf("%s %d\n", __func__, __LINE__);
#endif
  int old_val = 0;
  int new_val = 1;
  if (__sync_bool_compare_and_swap(once_control, 0, 1)) {
    printf("%s calling %p controlled by %p.\n", __func__, init_routine, once_control);
    init_routine();
    return 0;
  } else {
    return 1;
  }
}

#if 0
pthread_t pthread_self(void)
{
  printf("OK? %s %d\n", __func__, __LINE__);
  return real_pthread_self();
}
#endif

#if 1
int sched_yield() {
  if (0) {
    int pool_id;
    uint64_t abt_id;
    ABT_self_get_last_pool_id(&pool_id);
    ABT_thread_self_id(&abt_id);
    printf("%lu pool_id %d\n", abt_id, pool_id);
  }
  return ABT_thread_yield();
}
#endif


void
abt_init()
{
  int i;
  ABT_init(0, NULL);
#if USE_PREEMPT
  printf(".so argobots with preempt!\n");
  for (i=0; i<N_CORE; i++) {
    ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC, ABT_TRUE, &global_abt_pools[i]);
  }

  ABT_xstream_self(&abt_xstreams[0]);
  ABT_sched sched;
  ABT_sched_create_basic(ABT_SCHED_BASIC, 1, &global_abt_pools[0], ABT_SCHED_CONFIG_NULL, &sched);
  ABT_xstream_set_main_sched(abt_xstreams[0], sched);

  for (i=1; i<N_CORE; i++) {
    ABT_xstream_create_basic(ABT_SCHED_BASIC, 1, &global_abt_pools[i], ABT_SCHED_CONFIG_NULL, &abt_xstreams[i]);
    ABT_xstream_start(abt_xstreams[i]);
  }
  for (i=0; i<N_CORE; i++) {
    ABT_xstream_set_cpubind(abt_xstreams[i], i);
  }

  ABT_preemption_timer_create_groups(1, &abt_preemption_group);
  ABT_preemption_timer_set_xstreams(abt_preemption_group, N_CORE, abt_xstreams);
  ABT_preemption_timer_start(abt_preemption_group);
#else
  printf(".so argobots!\n");
  ABT_xstream_self(&abt_xstreams[0]);
  for (i=1; i<N_CORE; i++) {
    ABT_xstream_create(ABT_SCHED_NULL, &abt_xstreams[i]);
  }
  for (i=0; i<N_CORE; i++) {
    ABT_xstream_set_cpubind(abt_xstreams[i], i);
    ABT_xstream_get_main_pools(abt_xstreams[i], 1, &global_abt_pools[i]);
  }
#endif
  ABT_mutex_create(&abt_key_mutex);
}


void __zpoline_init(void);

__attribute__((constructor(0xffff))) static void
mylib_init()
{
  __zpoline_init();

  abt_init();
}


