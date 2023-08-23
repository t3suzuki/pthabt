#include <pthread.h>
#include <abt.h>
#include <map>
#include <set>
#include <cassert>
#include <dlfcn.h>
#include <stdlib.h>
//#include "tbb/concurrent_hash_map.h"

#define MAX_CORE (1)
#define MAX_TH (1024)


int n_abt_thread = 0;
ABT_xstream abt_xstreams[MAX_CORE];
ABT_thread abt_threads[MAX_TH];
ABT_pool abt_pools[MAX_CORE];
ABT_mutex mutex_map_mutex;
ABT_mutex cond_map_mutex;

static std::map<pthread_cond_t *, ABT_cond *> cond_map;
static std::map<pthread_mutex_t *, ABT_mutex *> mutex_map;
static std::map<pthread_key_t, ABT_key *> key_map;
//tbb::concurrent_hash_map<pthread_cond_t *, ABT_cond *> cond_map;
//tbb::concurrent_hash_map<pthread_mutex_t *, ABT_mutex *> mutex_map2;
//tbb::concurrent_hash_map<pthread_key_t *, ABT_key *> key_map;

void
ensure_abt_initialized()
{
  if (ABT_initialized() == ABT_ERR_UNINITIALIZED) {
    int ret;
    printf("%s %d\n", __func__, __LINE__);
    ret = ABT_init(0, NULL);
    printf("%s %d %d\n", __func__, __LINE__, ret);
    for (int i=0; i<MAX_CORE; i++) {
      ret = ABT_xstream_create(ABT_SCHED_NULL, &abt_xstreams[i]);
      //printf("%s %d %d\n", __func__, __LINE__, ret);
      ret = ABT_xstream_get_main_pools(abt_xstreams[i], 1, &abt_pools[i]);
      //printf("%s %d %d\n", __func__, __LINE__, ret);
    }
    ABT_mutex_create(&mutex_map_mutex);
    ABT_mutex_create(&cond_map_mutex);
  }
}

int pthread_create(pthread_t *pth, const pthread_attr_t *attr,
		   void *(*start_routine) (void *), void *arg) {
  ensure_abt_initialized();
  int tid = n_abt_thread++;
  *pth = tid;
  
  int ret = ABT_thread_create(abt_pools[tid % MAX_CORE], start_routine, arg,
			      ABT_THREAD_ATTR_NULL, &abt_threads[tid]);
  printf("%s %d %d\n", __func__, __LINE__, ret);
  if (ret) {
    printf("error %d\n", ret);
  }
  assert(ret == 0);
  return ret;
}

int pthread_join(pthread_t pth, void **retval) {
  assert(retval == NULL);
  int ret = ABT_thread_join(abt_threads[pth]);
  assert(ret == 0);
  return ret;
}

int sched_yield() {
  return ABT_thread_yield();
}


int pthread_cond_init(pthread_cond_t *cond,
		      const pthread_condattr_t *attr) {
  ABT_cond *abt_cond = (ABT_cond *)malloc(sizeof(ABT_cond));
  printf("%s %d\n", __func__, __LINE__);
  int ret = ABT_cond_create(abt_cond);
  ABT_mutex_lock(cond_map_mutex);
  cond_map[cond] = abt_cond;
  ABT_mutex_unlock(cond_map_mutex);
  printf("%s %d\n", __func__, __LINE__);
  return ret;
}

int pthread_cond_signal(pthread_cond_t *cond) {
  return ABT_cond_signal(*(cond_map[cond]));
}

int pthread_cond_destroy(pthread_cond_t *cond) {
  return ABT_cond_free(cond_map[cond]);
}

int pthread_cond_wait(pthread_cond_t *cond,
		      pthread_mutex_t *mutex) {
#if 0
  typename tbb::concurrent_hash_map<pthread_mutex_t *, ABT_mutex *>::const_accessor ac;
  mutex_map.find(ac, mutex);
  ABT_mutex *abt_mutex = ac->second;
  ac.release();
  
  return ABT_cond_wait(*(cond_map[cond]), *abt_mutex);
#else
  auto it = cond_map.find(cond);
  if (it == cond_map.end())
    pthread_cond_init(cond, NULL);
  printf("%s %d\n", __func__, __LINE__);
  return ABT_cond_wait(*(cond_map[cond]), *(mutex_map[mutex]));
#endif
}

int pthread_cond_timedwait(pthread_cond_t *cond,
			   pthread_mutex_t *mutex,
			   const struct timespec *abstime) {
#if 0
  typename tbb::concurrent_hash_map<pthread_mutex_t *, ABT_mutex *>::const_accessor ac;
  mutex_map.find(ac, mutex);
  ABT_mutex *abt_mutex = ac->second;
  ac.release();
  
  return ABT_cond_timedwait(*(cond_map[cond]), *abt_mutex, abstime);
#else
  return ABT_cond_timedwait(*(cond_map[cond]), *(mutex_map[mutex]), abstime);
#endif
}

int pthread_mutex_init(pthread_mutex_t *mutex,
		       const pthread_mutexattr_t *attr) {
  ABT_mutex *abt_mutex = (ABT_mutex *)malloc(sizeof(ABT_mutex));
  int ret = ABT_mutex_create(abt_mutex);
#if 0
  typename tbb::concurrent_hash_map<pthread_mutex_t *, ABT_mutex *>::accessor ac;
  mutex_map2.insert(ac, mutex);
  ac->second = abt_mutex;
  ac.release();
#endif
  ABT_mutex_lock(mutex_map_mutex);
  mutex_map[mutex] = abt_mutex;
  ABT_mutex_unlock(mutex_map_mutex);
  //printf("%s %d %p %p\n", __func__, __LINE__, mutex, abt_mutex);
  return ret;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
#if 0
  printf("%s %d %p\n", __func__, __LINE__, mutex);
  typename tbb::concurrent_hash_map<pthread_mutex_t *, ABT_mutex *>::accessor ac;
  bool found = mutex_map.find(ac, mutex);
  ABT_mutex *abt_mutex;
  if (found) {
    abt_mutex = ac->second;
    ac.release();
  } else {
    abt_mutex = (ABT_mutex *)malloc(sizeof(ABT_mutex));
    int ret = ABT_mutex_create(abt_mutex);
    mutex_map.insert(ac, mutex);
    ac->second = abt_mutex;
    ac.release();
  }
  printf("%s %d %p\n", __func__, __LINE__, mutex);
  return ABT_mutex_lock(*abt_mutex);
#else
  auto it = mutex_map.find(mutex);
  if (it == mutex_map.end())
    pthread_mutex_init(mutex, NULL);
  printf("%s %d %p\n", __func__, __LINE__, mutex);
  return ABT_mutex_lock(*(mutex_map[mutex]));
#endif
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
#if 0
  typename tbb::concurrent_hash_map<pthread_mutex_t *, ABT_mutex *>::const_accessor ac;
  mutex_map.find(ac, mutex);
  ABT_mutex *abt_mutex = ac->second;
  ac.release();
  
  return ABT_mutex_trylock(*abt_mutex);
#else
  auto it = mutex_map.find(mutex);
  if (it == mutex_map.end())
    pthread_mutex_init(mutex, NULL);
  return ABT_mutex_trylock(*(mutex_map[mutex]));
#endif
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
#if 0
  typename tbb::concurrent_hash_map<pthread_mutex_t *, ABT_mutex *>::const_accessor ac;
  mutex_map.find(ac, mutex);
  ABT_mutex *abt_mutex = ac->second;
  ac.release();
  
  return ABT_mutex_unlock(*abt_mutex);
#else
  return ABT_mutex_unlock(*(mutex_map[mutex]));
#endif
}

int pthread_attr_init(pthread_attr_t *attr) {
  return 0;
};

int pthread_key_create(pthread_key_t *key, void (*destructor)(void*)) {
  ABT_key *abt_key = (ABT_key *)malloc(sizeof(ABT_key));
  printf("%s %d %p %p\n", __func__, __LINE__, key, abt_key);
  int ret = ABT_key_create(destructor, abt_key);
  key_map[*key] = abt_key;
  return ret;
}

int pthread_setspecific(pthread_key_t key, const void *value) {
  printf("%s %d %p\n", __func__, __LINE__, key);
  return ABT_key_set(*(key_map[key]), value);
}

void * pthread_getspecific(pthread_key_t key) {
  void *ret;
  ABT_key_get(*(key_map[key]), &ret);
  return ret;
}

int pthread_setname_np(pthread_t thread, const char *name) {
  return 0;
}
