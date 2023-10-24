#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <linux/aio_abi.h>

#include "real_pthread.h"
#include "nvme.h"
#include "myfs.h"
#include "common.h"

#define N_HELPER (32)

typedef long (*syscall_fn_t)(long, long, long, long, long, long, long);
static syscall_fn_t next_sys_call = NULL;

extern void (*debug_print4)(long, long, long, long, long);
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

//#define FOR_KVELL (1)
#define FOR_ROCKSDB (1)
//#define FOR_FIO (1)
#define FOR_WT (1)

int openat_file(char *filename)
{
  int ret = 0;
#if FOR_FIO
#define MYFILE ("myfile")
  ret |= strncmp(MYFILE, filename, strlen(MYFILE)) == 0;
#endif

#if FOR_ROCKSDB
#define MYDIR ("/home/tomoya-s/mountpoint/tomoya-s/rocksdb_abt400m/")
#define MYSUFFIX (".sst")  
  ret |= ((strncmp(MYDIR, filename, strlen(MYDIR)) == 0) &&
	  (strncmp(MYSUFFIX, filename + strlen(MYDIR) + 6, strlen(MYSUFFIX)) == 0));

#define MYDIR5 ("/tmp/myfile5/")
  ret |= ((strncmp(MYDIR5, filename, strlen(MYDIR5)) == 0) &&
	  (strncmp(MYSUFFIX, filename + strlen(MYDIR5) + 6, strlen(MYSUFFIX)) == 0));
#endif

#if FOR_KVELL
#define MYDIR ("/home/tomoya-s/mountpoint/tomoya-s/KVell/db/")
#define MYPREFIX ("slab")  
  ret |= ((strncmp(MYDIR, filename, strlen(MYDIR)) == 0) &&
	  (strncmp(MYPREFIX, filename + strlen(MYDIR), strlen(MYPREFIX)) == 0));
#endif
  
#if FOR_WT
#define MYFILE ("/home/tomoya-s/mountpoint/tomoya-s/wt_abtOK250m/test.wt")
  ret |= (strncmp(MYFILE, filename, strlen(MYFILE)) == 0);
#endif
  return ret;
}


static struct iocb *cur_aios[1024];
static int cur_aio_wp;
static int cur_aio_rp;
static int cur_aio_max;

#define MAX_HOOKFD (1024)
//int hookfd = -1;
int hookfds[MAX_HOOKFD];
size_t cur_pos[MAX_HOOKFD];

void
read_impl(int hookfd, loff_t len, loff_t pos, char *buf)
{
  int tid = get_tid();
#if 0
  if (pos % 512 != 0) {
    printf("error len %lu pos %lu %lu\n", len, pos, cur_pos[hookfd]);
    assert(0);
  }
#else
  assert(pos % 512 == 0);
#endif
  int blksz = ((len % BLKSZ != 0) || (pos % BLKSZ != 0)) ? 512 : BLKSZ;
  if ((pos % BLKSZ) + len <= BLKSZ) {
    blksz = len;
  }
  //printf("%s %d %lu %lu\n", __func__, __LINE__, len, pos);
  
  int j;
  for (j=0; j<len; j+=blksz) {
    int64_t lba = myfs_get_lba(hookfd, pos + j, 0);
    int rid = nvme_read_req(lba, blksz/512, tid, MIN(blksz, len - j), buf + j);
    while (1) {
      if (nvme_check(rid))
	break;
      ABT_thread_yield();
    }
  }
}

void
write_impl(int hookfd, loff_t len, loff_t pos, char *buf)
{
  int tid = get_tid();
  assert(pos % 512 == 0);
  int blksz = ((len % BLKSZ != 0) || (pos % BLKSZ != 0)) ? 512 : BLKSZ;
  
  int j;
  for (j=0; j<len; j+=blksz) {
    int64_t lba = myfs_get_lba(hookfd, pos + j, 1);
    //printf("%s %d hookfd=%d len=%lu %lu pos =%lu\n", __func__, __LINE__, hookfd, len, j, pos);
    int rid = nvme_write_req(lba, blksz/512, tid, MIN(blksz, len - j), buf + j);
    while (1) {
      if (nvme_check(rid))
	break;
      ABT_thread_yield();
    }
  }
}



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

    /*
    if ((a1 != 1) && (a1 != 17) && (a1 != 18)) {
      printf("call %ld %ld\n", a1, abt_id);
    }
    */

    if (debug_print) {
      debug_print(1, a1, abt_id);
    }
    if (a1 == 230) { // sleep
      if (debug_print)
	debug_print(666, a2, a3);
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
	  //printf("diff_nsec %f\n", diff_nsec);
	  if (diff_nsec > 0)
	    return 0;
	  ABT_thread_yield();
	}
      }
    }
    else if (a1 == 441) {
      struct timespec tsz = {.tv_sec = 0, .tv_nsec = 0};
      struct timespec *ts = (struct timespec *) a5;
      if (ts) {
	struct timespec tsc;
	clock_gettime(CLOCK_MONOTONIC_COARSE, &tsc);
	ts->tv_sec += tsc.tv_sec;
	ts->tv_nsec += tsc.tv_nsec;
      }
      while (1) {
	int ret = next_sys_call(a1, a2, a3, a4, (long)&tsz, a6, a7);
	if (ret > 0) {
	  return ret;
	}
	if (ts) {
	  struct timespec ts2;
	  clock_gettime(CLOCK_MONOTONIC_COARSE, &ts2);
	  double diff_nsec = (ts2.tv_sec - ts->tv_sec) * 1e9 + (ts2.tv_nsec - ts->tv_nsec);
	  if (diff_nsec > 0)
	    return 0;
	}
	ABT_thread_yield();
      }
    } else if (a1 == 257) { // openat
      char *filename = (char*)a3;
      if (openat_file((char*)a3)) {
	ret = next_sys_call(a1, a2, a3, a4, a5, a6, a7);
	printf("openat with mylib: fd=%d %s\n", ret, filename);
	if (ret < MAX_HOOKFD) {
	  hookfds[ret] = myfs_open((char *)filename);
	  cur_pos[ret] = 0;
	  //hookfds[ret] = 1;
	}
	if (debug_print)
	  debug_print(884, ret, 0);
	return ret;
      }
      return next_sys_call(a1, a2, a3, a4, a5, a6, a7);
    } else if (a1 == 3) { // close
      if (hookfds[a2] >= 0) {
	printf("close for mylib: fd=%ld\n", a2);
	//myfs_set_size(hookfds[a2], cur_pos[a2]);
	hookfds[a2] = -1;
      }
      return next_sys_call(a1, a2, a3, a4, a5, a6, a7);
    } else if (a1 == 270) { //select
      //printf("%ld %lx %lx %lx %lx\n", a2, a3, a4, a5, a6);
      struct timespec *ats = (struct timespec *) a6;
      //printf("%ld %ld\n", ats->tv_sec, ats->tv_nsec);
      if (debug_print)
	debug_print(870, ats->tv_sec, ats->tv_nsec);
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
      if (debug_print)
	debug_print(870, ts.tv_sec, ts.tv_nsec);
      ts.tv_sec += ats->tv_sec;
      ts.tv_nsec += ats->tv_nsec;
      int ret;
      while (1) {
	struct timespec zts = {.tv_sec = 0, .tv_nsec = 0};
	if (a6) {
	  struct timespec ts2;
	  clock_gettime(CLOCK_MONOTONIC_COARSE, &ts2);
	  double diff_nsec = (ts2.tv_sec - ts.tv_sec) * 1e9 + (ts2.tv_nsec - ts.tv_nsec);
	  if (diff_nsec > 0)
	    return 0;
	}
	ret = next_sys_call(a1, a2, a3, a4, a5, (long)&zts, a7);
	if (ret) {
	  return ret;
	}
	ABT_thread_yield();
      }
    } else if (a1 == 0) { // read
      int hookfd = hookfds[a2];
      char *buf = (char *)a3;
      loff_t len = a4;
      //printf("read %d\n", a2);
      if (hookfd >= 0) {
	read_impl(hookfd, len, cur_pos[a2], buf);
	cur_pos[a2] += len;
	return len;
      } else {
	return next_sys_call(a1, a2, a3, a4, a5, a6, a7);
      }
    } else if (a1 == 17) { // pread64
      int hookfd = hookfds[a2];
      char *buf = (char *)a3;
      loff_t len = a4;
      loff_t pos = a5;
      //printf("pread64 fd %d len %ld pos %ld\n", a2, len, pos);
      if (hookfd >= 0) {
	read_impl(hookfd, len, pos, buf);
	return len;
      } else {
	return next_sys_call(a1, a2, a3, a4, a5, a6, a7);
      }
    } else if (a1 == 1) { // write
      int hookfd = hookfds[a2];
      char *buf = (char *)a3;
      loff_t len = a4;
      if (hookfd >= 0) {
	write_impl(hookfd, len, cur_pos[a2], buf);
	cur_pos[a2] += len;
	return len;
      } else {
	return next_sys_call(a1, a2, a3, a4, a5, a6, a7);
      }
    } else if (a1 == 186) { // gettid
      return abt_id;
    } else if (a1 == 18) { // pwrite64
      int hookfd = hookfds[a2];
      char *buf = (char *)a3;
      loff_t len = a4;
      loff_t pos = a5;
      //printf("pwrite64 %d hookfd=%d, len=%ld, pos=%ld\n", a2, hookfd, len, pos);
      if (hookfd >= 0) {
	//printf("pwrite64 %d hookfd=%d, len=%ld, pos=%ld\n", a2, hookfd, len, pos);
	write_impl(hookfd, len, pos, buf);
	return len;
      } else {
	return next_sys_call(a1, a2, a3, a4, a5, a6, a7);
      }
    } else if (a1 == 262) { // fstat
      if (((int32_t)a2 >= 0) && (hookfds[a2] >= 0)) {
	uint64_t sz = myfs_get_size(hookfds[a2]);
	struct stat *statbuf = (struct stat*)a4;
	statbuf->st_size = sz;
	statbuf->st_blocks = 512;
	statbuf->st_blksize = sz / 512;
	printf("fstat: file size = %ld fd=%ld\n", sz, a2);
	return 0;
      } else {
	return next_sys_call(a1, a2, a3, a4, a5, a6, a7);
      }
    } else if (a1 == 208) { // io_getevents
      int min_nr = a3;
      int nr = a4;
      int completed = 0;
      //printf("hogehoge\n");
      struct io_event *events = (struct io_event *)a5;
      while (completed < nr) {
	if (cur_aio_wp == cur_aio_rp) {
	  break;
	}
	//printf("cur_aio_wp = %d, cur_aio_rp = %d\n", cur_aio_wp, cur_aio_rp);
	int rid = cur_aios[cur_aio_rp]->aio_reserved2;
	int fd = cur_aios[cur_aio_rp]->aio_fildes;
	uint64_t pos = cur_aios[cur_aio_rp]->aio_offset;
	int64_t lba = myfs_get_lba(hookfds[fd], pos, 0);
	//printf("io_getevents fd=%d cid=%d pos=%lu lba=%u qid=%d\n", fd, cid, pos, lba, qid);
	while (1) {
	  if (nvme_check(rid))
	    break;
	  ABT_thread_yield();
	}
	//printf("%s %d completed%d\n", __func__, __LINE__, completed);
	events[completed].data = cur_aios[cur_aio_rp]->aio_buf;
	events[completed].obj = (uint64_t)cur_aios[cur_aio_rp];
	//printf("io_submitted callback %lx rp %d\n", cur_aios[cur_aio_rp], cur_aio_rp);
	if (0) {
	  struct iocb *cb = (void*)events[completed].obj;
	  struct slab_callback *callback = (void*)cb->aio_data;
	  int op = cb->aio_lio_opcode;
	  printf("io_getevents callback %p rd=%d rid=%d\n", callback, op == IOCB_CMD_PREAD, rid);
	  //debug_item(111, cb->aio_data);
	}
	events[completed].res = cur_aios[cur_aio_rp]->aio_nbytes;
	events[completed].res2 = 0;
	completed++;
	cur_aio_rp = (cur_aio_rp + 1) % cur_aio_max;
	if (completed == nr)
	  break;
      }
      //printf("completed %d\n", completed);
      return completed;
    } else if (a1 == 209) { // io_submit
      int tid = get_tid();
      struct iocb **ios = (struct iocb **)a4;
      int n_io = a3;
      int i;
      for (i=0; i<n_io; i++) {
	if ((cur_aio_wp + 1) % cur_aio_max == cur_aio_rp) {
	  break;
	}

	int fd = ios[i]->aio_fildes;
	int op = ios[i]->aio_lio_opcode;
	//printf("io_submitted %p callback %lx wp %d rd?=%d\n", ios[i], ios[i]->aio_data, cur_aio_wp, op == IOCB_CMD_PREAD);
	char *buf = (char *)ios[i]->aio_buf;
	uint64_t len = ios[i]->aio_nbytes;
	uint64_t pos = ios[i]->aio_offset;
	int blksz = BLKSZ;
	assert(hookfds[fd] >= 0);
	
	if (op == IOCB_CMD_PREAD) {
	  int64_t lba = myfs_get_lba(hookfds[fd], pos, 0);
	  int rid = nvme_read_req(lba, blksz/512, tid, MIN(blksz, len), buf);
	  //printf("io_submit read op=%d fd=%d, sz=%ld, pos=%ld lba=%d rid=%d\n", op, fd, len, pos, lba, rid);
	  ios[i]->aio_reserved2 = rid;
#if 0
	  while (1) {
	    if (nvme_check(rid))
	      break;
	    ABT_thread_yield();
	  }
	  ios[i]->aio_reserved2 = JUST_ALLOCATED;
#endif
	}
	if (op == IOCB_CMD_PWRITE) {
	  int64_t lba = myfs_get_lba(hookfds[fd], pos, 1);
	  int rid = nvme_write_req(lba, blksz/512, tid, MIN(blksz, len), buf);
	  //printf("io_submit write op=%d fd=%d, sz=%ld, pos=%ld lba=%d rid=%d\n", op, fd, len, pos, lba, rid);
	  ios[i]->aio_reserved2 = rid;
#if 0
	  //debug_item(114, ios[i]->aio_data);
	  while (1) {
	    if (nvme_check(rid))
	      break;
	    ABT_thread_yield();
	  }
	  ios[i]->aio_reserved2 = JUST_ALLOCATED;
	  //debug_item(115, ios[i]->aio_data);
#endif
	  
	}
	//printf("cur_aio_wp = %d\n", cur_aio_wp);
	cur_aios[cur_aio_wp] = ios[i];
	//debug_item(112, ios[i]->aio_data);
	cur_aio_wp = (cur_aio_wp + 1) % cur_aio_max;
      }
      return i;//return next_sys_call(a1, a2, a3, a4, a5, a6, a7);
    } else if (a1 == 206) { // io_setup
      cur_aio_max = a2;
      cur_aio_wp = 0;
      cur_aio_rp = 0;
      cur_aio_max = a2;
      printf("io_setup %d %p %p\n", cur_aio_max, (void *)a3, cur_aios);
      return 0;
    } else if (a1 == 207) { // io_destroy
      printf("io_destroy %ld %p\n", a2, (void *)a3);
      return 0;
    } else if (1) {
      return next_sys_call(a1, a2, a3, a4, a5, a6, a7);
    } else {
      req_helper(abt_id, a1, a2, a3, a4, a5, a6, a7);
      while (1) {
	if (helpers[abt_id].done)
	  break;
	ABT_thread_yield();
	//uint64_t pre_id;
	//int ret = ABT_self_get_pre_id(&pre_id);
      }
      return helpers[abt_id].ret;
    }
  } else {
    if ((a1 == 1) || (a1 == 18)) {
      printf("outside write %ld %ld\n", a1, a2);
    }
    return next_sys_call(a1, a2, a3, a4, a5, a6, a7);
  }
}




int __hook_init(long placeholder __attribute__((unused)),
		void *sys_call_hook_ptr)
{

#ifdef MYFILE
  printf("hooked file name : %s\n", MYFILE);
#endif
#ifdef MYDIR
  printf("hooked rocksdb name : %s\n", MYDIR);
#endif
  myfs_mount("/root/myfs_superblock");

  int i;
  for (i=0; i<MAX_HOOKFD; i++) {
    hookfds[i] = -1;
    //cur_pos[i] = 0;
  }
  
  load_debug();

  
  for (i=0; i<N_HELPER; i++) {
    helpers[i].id = i;
    real_pthread_mutex_init(&helpers[i].mutex, NULL);
    real_pthread_cond_init(&helpers[i].cond, NULL);
    real_pthread_create(&helpers[i].pth, NULL, (void *(*)(void *))do_helper, &helpers[i]);
  }

  next_sys_call = *((syscall_fn_t *) sys_call_hook_ptr);
  *((syscall_fn_t *) sys_call_hook_ptr) = hook_function;

  nvme_init(0, 16);
  nvme_init(1, 18);
  /*
  nvme_init(2, 18);
  nvme_init(3, 20);
  */
  
  return 0;

}

__attribute__((destructor(0xffff))) static void
hook_deinit()
{
  myfs_umount();
}

