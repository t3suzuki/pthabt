#include <pthread.h>
#include <stdio.h>
#include <iostream>
#include <unistd.h>

#define RUN_TIME (5)
#define N_TH (4)

uint64_t g_iter[N_TH];
static bool quit = false;

struct pthread_arg_t {
  int i_th;
};

void
worker(void *_arg)
{
  printf("%s %d\n", __func__, __LINE__);
  pthread_arg_t *arg = (pthread_arg_t *)_arg;
  
  uint64_t iter = 0;
  while (!quit) {
    sched_yield();
    //sleep(1);
    iter ++;
  }
  g_iter[arg->i_th] = iter;
  return;
}


pthread_arg_t pthread_args[N_TH];

int
main()
{
  pthread_t pth[N_TH];
  printf("%s %d\n", __func__, __LINE__);
  for (int i=0; i<N_TH; i++) {
    pthread_args[i].i_th = i;
    pthread_create(&pth[i], NULL, worker, &pthread_args[i]);
  }
  printf("created.\n");
  for (auto i=0; i<RUN_TIME; i++) {
    sleep(1);
    printf("elapsed %d sec...\n", i+1);
  }
  quit = true;
  for (int i=0; i<N_TH; i++) {
    pthread_join(pth[i], NULL);
  }
  printf("joined.\n");

  uint64_t iter = 0;
  for (int i=0; i<N_TH; i++) {
    iter += g_iter[i];
  }
  double mps = (double)iter / RUN_TIME / 1000 / 1000;
  printf("%d, %f MPS, %f ns\n", iter, mps, 1000 / mps);
}
