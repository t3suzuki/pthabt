#include <stdlib.h>
#include <pthread.h>
#include <abt.h>
#include <map>

extern "C" {

  static std::map<pthread_key_t, ABT_key *> key_map;

  int pthread_key_create(pthread_key_t *key, void (*destructor)(void*)) {
    ABT_key *abt_key = (ABT_key *)malloc(sizeof(ABT_key));
#if __PTHREAD_VERBOSE__
    printf("%s %d %p %p\n", __func__, __LINE__, key, abt_key);
#endif
    int ret = ABT_key_create(destructor, abt_key);
    key_map[*key] = abt_key;
    return ret;
  }

  int pthread_setspecific(pthread_key_t key, const void *value) {
#if __PTHREAD_VERBOSE__
    printf("%s %d %p\n", __func__, __LINE__, key);
#endif
    return ABT_key_set(*(key_map[key]), (void *)value);
  }

  void * pthread_getspecific(pthread_key_t key) {
    void *ret;
    ABT_key_get(*(key_map[key]), &ret);
    return ret;
  }

}
