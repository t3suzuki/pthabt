#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

typedef struct {
  int fd;
  int tid;
} arg_t;

void worker(void *a)
{
  arg_t *arg = (arg_t *)a;
  int fd = arg->fd;
  int tid = arg->tid;
  char *wbuf = aligned_alloc(4096, 512);
  char *rbuf = aligned_alloc(4096, 512);
  int i;
  int n;
#if 0
  for (i=0; i<128; i++) {
    wbuf[i] = i+0x7+tid;
  }
  n = pwrite64(fd, wbuf, 512, tid*512);
#endif

  
  for (i=0; i<128; i++) {
    rbuf[i] = 0;
  }
  n = pread64(fd, rbuf, 512, tid*512);
  printf("tid = %d %d %d\n", tid, wbuf[0], rbuf[0]);
}

#define N_TH (2)

int
main()
{
  int fd = open("myfile", O_RDWR|O_DIRECT);
  printf("fd = %d\n", fd);
  int i;
  pthread_t pth[N_TH];
  arg_t arg[N_TH];

  for (i=0; i<N_TH; i++) {
    arg[i].tid = i;
    arg[i].fd = fd;
  }
  for (i=0; i<N_TH; i++) {
    pthread_create(&pth[i], NULL, worker, &arg[i]);
  }
  for (i=0; i<N_TH; i++) {
    pthread_join(pth[i], NULL);
  }
  
  close(fd);
  return 0;
}
