#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#define FILESZ (1024*1024*512)

typedef struct {
  int fd;
  int tid;
} arg_t;

void worker(void *a)
{
  arg_t *arg = (arg_t *)a;
  int fd = arg->fd;
  int tid = arg->tid;
  char *rbuf = aligned_alloc(4096, 512);
  int i;
  int n;

  
  for (i=0; i<128; i++) {
    rbuf[i] = 0;
  }
  int r = rand() % (FILESZ / 512);
  n = pread64(fd, rbuf, 512, r*512);
  //printf("tid = %d %d %d\n", tid, wbuf[0], rbuf[0]);
  printf("tid = %d %d %d\n", tid, (char)r, rbuf[0]);
}

#define N_TH (1)

int
main()
{
  int fd = open("myfile", O_RDWR|O_DIRECT);
  printf("fd = %d\n", fd);
  int i;
  char *wbuf = aligned_alloc(4096, 512);
  pthread_t pth[N_TH];
  arg_t arg[N_TH];
  int j;

#if 1
  printf("writing...\n");
  for (j=0;j<(FILESZ/512);j++) {
    wbuf[0] = j;
    pwrite64(fd, wbuf, 512, j*512);
    if (j % 16384 == 0)
      printf("readetest write lba %d\n", j);
  }
  printf("write done\n");
#endif
  
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
