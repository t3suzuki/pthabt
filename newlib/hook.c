#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <abt.h>
#include "real_pthread.h"
#include "nvme.h"

#define N_HELPER (32)

typedef long (*syscall_fn_t)(long, long, long, long, long, long, long);
static syscall_fn_t next_sys_call = NULL;

extern void (*debug_print)(int, int, int);
extern int (*debug_printf)(const char *format, ...);
void load_debug(void);

typedef struct {
  int id;
  pthread_t pth;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  volatile int done;
  volatile int ready;
  volatile long arg[7];
  volatile long ret;
} helper_t;
static helper_t helpers[N_HELPER];

inline static void req_helper(int id, long a1, long a2, long a3, long a4, long a5, long a6, long a7) {
  helpers[id].done = false;
  helpers[id].arg[0] = a1;
  helpers[id].arg[1] = a2;
  helpers[id].arg[2] = a3;
  helpers[id].arg[3] = a4;
  helpers[id].arg[4] = a5;
  helpers[id].arg[5] = a6;
  helpers[id].arg[6] = a7;
  real_pthread_mutex_lock(&helpers[id].mutex);
  helpers[id].ready = true;
  real_pthread_cond_signal(&helpers[id].cond);
  real_pthread_mutex_unlock(&helpers[id].mutex);
};

void do_helper(void *arg) {
  helper_t *h = (helper_t *)arg;
  while (1) {
    real_pthread_mutex_lock(&(h->mutex));
    while (h->ready == false) {
      real_pthread_cond_wait(&h->cond, &h->mutex);
    }
    h->ready = false;
    real_pthread_mutex_unlock(&h->mutex);
    h->ret = next_sys_call(h->arg[0], h->arg[1], h->arg[2], h->arg[3], h->arg[4], h->arg[5], h->arg[6]);
    //if (debug_print)
    //debug_print(8, h->arg[0], h->ret);
    h->done = true;
  }
}


int hookfd = -1;
size_t cur_lba = 0;

long hook_function(long a1, long a2, long a3,
		   long a4, long a5, long a6,
		   long a7)
{
  /*
  if (debug_print) {
    debug_print(1, a1, 9999);
  }
  */
  uint64_t abt_id;
  int ret = ABT_self_get_thread_id(&abt_id);
  if (ret == ABT_SUCCESS && (abt_id >= 0)) {

    if (debug_print) {
      debug_print(1, a1, abt_id);
    }
    if (a1 == 230) {
      if (a3 == 0) {
	struct timespec *ts = (struct timespec *) a4;
	struct timespec tsc;
	clock_gettime(CLOCK_MONOTONIC_COARSE, &tsc);
	ts->tv_sec += tsc.tv_sec;
	ts->tv_nsec += tsc.tv_nsec;
	while (1) {
	  struct timespec ts2;
	  clock_gettime(CLOCK_MONOTONIC_COARSE, &ts2);
	  double diff_nsec = (ts2.tv_sec - ts->tv_sec) * 1e9 + (ts2.tv_nsec - ts->tv_nsec);
	  if (diff_nsec > 0)
	    return 0;
	  ABT_thread_yield();
	}
      }
    }
    else if (a1 == 441) {
      /*
      if (debug_print) {
	debug_print(1, a1, abt_id);
      }
      */
      struct timespec tsz = {.tv_sec = 0, .tv_nsec = 0};
      struct timespec *ts = (struct timespec *) a5;
      if (ts) {
	struct timespec tsc;
	clock_gettime(CLOCK_MONOTONIC_COARSE, &tsc);
	ts->tv_sec += tsc.tv_sec;
	ts->tv_nsec += tsc.tv_nsec;
	/*
	if (debug_print) {
	  debug_print(888, ts1->tv_sec, ts1->tv_nsec);
	}
	*/
      }
      while (1) {
	/*
	if (debug_print) {
	  debug_print(5, abt_id, a5);
	}
	*/
	int ret = next_sys_call(a1, a2, a3, a4, (long)&tsz, a6, a7);
	/*
	if (debug_print) {
	  debug_print(6, abt_id, ret);
	}
	*/
	if (ret > 0) {
	  return ret;
	}
	if (ts) {
	  struct timespec ts2;
	  clock_gettime(CLOCK_MONOTONIC_COARSE, &ts2);
	  /*
	  if (debug_print) {
	    debug_print(889, ts2.tv_sec, ts2.tv_nsec);
	  }
	  */
	  double diff_nsec = (ts2.tv_sec - ts->tv_sec) * 1e9 + (ts2.tv_nsec - ts->tv_nsec);
	  if (diff_nsec > 0)
	    return 0;
	}
	ABT_thread_yield();
      }
    } else if (a1 == 257) { // openat
#define MYFILE ("myfile")
      if (strncmp(MYFILE, (char *)a3, strlen(MYFILE)) == 0) {
	ret = next_sys_call(a1, a2, a3, a4, a5, a6, a7);
	hookfd = ret;
	if (debug_print)
	  debug_print(884, hookfd, 0);
	return ret;
      }
      return next_sys_call(a1, a2, a3, a4, a5, a6, a7);
    } else if (a1 == 3) { // close
      if (a2 == hookfd)
	hookfd = -1;
      return next_sys_call(a1, a2, a3, a4, a5, a6, a7);
    } else if (a1 == 270) { //select
      struct timeval *tv = (struct timeval *) a6;
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
      ts.tv_sec += tv->tv_sec;
      ts.tv_nsec += tv->tv_usec * 1000;
      int ret;
      while (1) {
	struct timeval ztv = {.tv_sec = 0, .tv_usec = 0};
	if (a6) {
	  struct timespec ts2;
	  clock_gettime(CLOCK_MONOTONIC_COARSE, &ts2);
	  double diff_nsec = (ts2.tv_sec - ts.tv_sec) * 1e9 + (ts2.tv_nsec - ts.tv_nsec);
	  if (diff_nsec > 0)
	    return 0;
	}
	ret = next_sys_call(a1, a2, a3, a4, a5, (long)&ztv, a7);
	if (ret) {
	  return ret;
	}
	ABT_thread_yield();
      }
    } else if (a1 == 0) { // read
      if (a2 == hookfd) {
	int rank;
	ABT_xstream_self_rank(&rank);
	int qid = rank + 1;
	if (debug_print)
	  debug_print(883, rank, 0);
	size_t count = a4;
	int cid = nvme_read_req(0, 1, qid);
	while (1) {
	  if (nvme_read_check(qid, cid, count, a3))
	    break;
	  ABT_thread_yield();
	}
	return count;
      } else {
	return next_sys_call(a1, a2, a3, a4, a5, a6, a7);
      }
    } else if (a1 == 17) { // pread64
      if (a2 == hookfd) {
	int rank;
	ABT_xstream_self_rank(&rank);
	int qid = rank + 1;
	if (debug_print)
	  debug_print(883, rank, 0);
	size_t count = a4;
	loff_t pos = a5;
	int cid = nvme_read_req(pos / 512, 1, qid);
	while (1) {
	  if (nvme_read_check(qid, cid, count, a3))
	    break;
	  ABT_thread_yield();
	}
	if (debug_print)
	  debug_print(882, 0, 0);
	return count;
      } else {
	return next_sys_call(a1, a2, a3, a4, a5, a6, a7);
      }
    } else if (a1 == 1) { // write
      if (a2 == hookfd) {
	int rank;
	ABT_xstream_self_rank(&rank);
	int qid = rank + 1;
	if (debug_print)
	  debug_print(883, rank, 0);
	size_t count = a4;
	int cid = nvme_write_req(cur_lba++, 1, qid, count, a3);
	while (1) {
	  if (nvme_write_check(qid, cid))
	    break;
	  ABT_thread_yield();
	}
	return count;
      } else {
	return next_sys_call(a1, a2, a3, a4, a5, a6, a7);
      }
    } else if (a1 == 18) { // pwrite64
      if (a2 == hookfd) {
	int rank;
	ABT_xstream_self_rank(&rank);
	int qid = rank + 1;
	if (debug_print)
	  debug_print(883, rank, 0);
	size_t count = a4;
	loff_t pos = a5;
	int cid = nvme_write_req(pos / 512, 1, qid, count, a3);
	while (1) {
	  if (nvme_write_check(qid, cid))
	    break;
	  ABT_thread_yield();
	}
	return count;
      } else {
	return next_sys_call(a1, a2, a3, a4, a5, a6, a7);
      }
    } else if ((a1 == 1) || // write
	(a1 == 9) || // mmap
	(a1 == 12) || // brk
	(a1 == 202) || // futex
	       true ||
	false) {
      //ABT_thread_yield();
      return next_sys_call(a1, a2, a3, a4, a5, a6, a7);
    } else {
      /*
      if (debug_print) {
	debug_print(1, a1, abt_id);
      }
      */
      req_helper(abt_id, a1, a2, a3, a4, a5, a6, a7);
      while (1) {
	if (helpers[abt_id].done)
	  break;
	/*
	if (debug_print) {
	  if (abt_id % 4 == 1)
	    debug_print(77, a1, abt_id);
	}
	*/
	ABT_thread_yield();
	//uint64_t pre_id;
	//int ret = ABT_self_get_pre_id(&pre_id);
	/*
	{
	  if (debug_print) {
	    if (abt_id % 4 == 1) {
	      debug_print(7, a1, abt_id);
	      //debug_print(7, a1, pre_id);
	    }
	  }
	}
	*/
      }
      /*
      if (debug_print) {
	debug_print(2, a1, helpers[abt_id].ret);
	//debug_print(2, a1, abt_id);
      }
      */
      return helpers[abt_id].ret;
    }
  } else {
    return next_sys_call(a1, a2, a3, a4, a5, a6, a7);
  }
}




int __hook_init(long placeholder __attribute__((unused)),
		void *sys_call_hook_ptr)
{
  load_debug();
  
  int i;
  for (i=0; i<N_HELPER; i++) {
    helpers[i].id = i;
    real_pthread_mutex_init(&helpers[i].mutex, NULL);
    real_pthread_cond_init(&helpers[i].cond, NULL);
    real_pthread_create(&helpers[i].pth, NULL, (void *(*)(void *))do_helper, &helpers[i]);
  }

  next_sys_call = *((syscall_fn_t *) sys_call_hook_ptr);
  *((syscall_fn_t *) sys_call_hook_ptr) = hook_function;

  nvme_init();
  
  return 0;

}
